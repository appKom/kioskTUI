#!/usr/bin/env python3
"""
Zettle sync script.
Polls the Zettle Purchase API every N seconds and writes new purchases
to the local SQLite database. Maintains PRODUCT, PURCHASES, and SALES_HISTORY.
"""

import json
import os
import sqlite3
import time
import urllib.request
import urllib.parse
import urllib.error
from datetime import datetime, timezone, timedelta


def find_project_root(start, marker="Makefile"):
    path = os.path.realpath(start)
    while True:
        if os.path.exists(os.path.join(path, marker)):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            raise RuntimeError(f"Could not find project root (looking for {marker})")
        path = parent


CURRENT_DIR = os.path.dirname(os.path.realpath(__file__))
PROJECT_ROOT = find_project_root(CURRENT_DIR)
DB_PATH = os.path.join(PROJECT_ROOT, "src/backend/example.db")
STATE_FILE = os.path.join(PROJECT_ROOT, "sync_state.json")
HEARTBEAT_FILE = os.path.join(PROJECT_ROOT, "sync.heartbeat")

PURCHASE_API = "https://purchase.izettle.com/purchases/v2"
PRODUCTS_API = "https://products.izettle.com/organizations/self/products/v2"

POLL_INTERVAL = 1
PRODUCT_REFRESH_SECS = 3600
WINDOW_DAYS = 365


def load_credentials():
    creds = {}
    with open(os.path.join(PROJECT_ROOT, ".env")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, _, val = line.partition("=")
            creds[key.strip()] = val.strip()
    return creds


def load_tokens():
    try:
        with open(os.path.join(PROJECT_ROOT, "tokens.json")) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_tokens(tokens):
    with open(os.path.join(PROJECT_ROOT, "tokens.json"), "w") as f:
        json.dump(tokens, f, indent=2)


def post_form(url, data):
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request(
        url, data=body, headers={"Content-Type": "application/x-www-form-urlencoded"}
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def get_access_token():
    creds = load_credentials()
    tokens = load_tokens()
    age = int(time.time()) - tokens.get("obtained_at", 0)
    if tokens.get("access_token") and age < tokens.get("expires_in", 0) - 300:
        return tokens["access_token"]
    print("Fetching new access token...")
    result = post_form(
        "https://oauth.zettle.com/token",
        {
            "grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer",
            "client_id": creds["ZETTLE_CLIENT_ID"],
            "assertion": creds["ZETTLE_API_KEY"],
        },
    )
    result["obtained_at"] = int(time.time())
    save_tokens(result)
    return result["access_token"]


# http


def api_get(url, token, params=None):
    if params:
        url = url + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


# product whitelist


def fetch_product_whitelist(token):
    data = api_get(PRODUCTS_API, token)
    result = {}
    for p in data:
        if "uuid" not in p or "name" not in p:
            continue
        price = 0
        variants = p.get("variants", [])
        if variants:
            price_val = variants[0].get("price", 0) or 0
            if isinstance(price_val, dict):
                price = price_val.get("amount", 0) or 0
            else:
                price = int(price_val) if price_val else 0
        result[p["uuid"]] = (p["name"], price)
    return result


# state


def load_state():
    try:
        with open(STATE_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_state(state):
    with open(STATE_FILE, "w") as f:
        json.dump(state, f, indent=2)


# db


def open_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn


def ensure_product(conn, name, price):
    conn.execute(
        "INSERT OR IGNORE INTO PRODUCT(NAME, AMOUNT, PRICE, ID) "
        "VALUES (?, 0, ?, (SELECT COALESCE(MAX(ID),0)+1 FROM PRODUCT));",
        (name, price),
    )
    if price > 0:
        conn.execute(
            "UPDATE PRODUCT SET PRICE = ? WHERE NAME = ? AND PRICE = 0;", (price, name)
        )


def insert_purchase_rows(conn, purchased_at, items):
    conn.executemany(
        "INSERT INTO PURCHASES(purchased_at, name, units) VALUES (?, ?, ?);",
        [(purchased_at, name, units) for name, units in items],
    )


def recalculate_amounts(conn):
    cutoff = int(time.time()) - WINDOW_DAYS * 86400
    conn.execute(
        """
        UPDATE PRODUCT SET AMOUNT = (
            SELECT COALESCE(SUM(units), 0)
            FROM PURCHASES
            WHERE PURCHASES.name = PRODUCT.NAME
              AND PURCHASES.purchased_at >= ?
        );
    """,
        (cutoff,),
    )


def rebuild_sales_history(conn):
    print("Rebuilding SALES_HISTORY from PURCHASES...")
    rows = conn.execute(
        "SELECT purchased_at, name, units FROM PURCHASES ORDER BY purchased_at ASC"
    ).fetchall()
    cumulative = {}
    hourly = {}
    for row in rows:
        ts, name, units = row[0], row[1], row[2]
        slot = ts - (ts % 3600)
        cumulative[name] = cumulative.get(name, 0) + units
        hourly[(slot, name)] = cumulative[name]
    conn.execute("DELETE FROM SALES_HISTORY;")
    conn.executemany(
        "INSERT OR REPLACE INTO SALES_HISTORY(snapshot_time, name, amount) VALUES (?, ?, ?);",
        [(slot, name, amount) for (slot, name), amount in sorted(hourly.items())],
    )
    print(f"SALES_HISTORY rebuilt: {len(hourly)} entries.")


def update_current_hour_history(conn):
    now = int(time.time())
    slot = now - (now % 3600)
    conn.execute(
        """
        INSERT OR REPLACE INTO SALES_HISTORY(snapshot_time, name, amount)
        SELECT ?, name, SUM(units)
        FROM PURCHASES
        WHERE purchased_at <= ?
        GROUP BY name;
    """,
        (slot, slot),
    )


def prune_old_data(conn):
    cutoff = int(time.time()) - WINDOW_DAYS * 86400
    conn.execute("DELETE FROM PURCHASES WHERE purchased_at < ?;", (cutoff,))
    conn.execute("DELETE FROM SALES_HISTORY WHERE snapshot_time < ?;", (cutoff,))


# purchase fetching


def iso_to_unix(iso):
    iso = iso.replace("Z", "+00:00")
    if len(iso) > 6 and iso[-5] in ("+", "-") and ":" not in iso[-5:]:
        iso = iso[:-2] + ":" + iso[-2:]
    return int(datetime.fromisoformat(iso).astimezone(timezone.utc).timestamp())


def advance_iso(iso):
    iso_clean = iso.replace("+0000", "+00:00").replace("Z", "+00:00")
    dt = datetime.fromisoformat(iso_clean)
    return (dt + timedelta(seconds=1)).strftime("%Y-%m-%dT%H:%M:%SZ")


def fetch_new_purchases(token, since_iso, whitelist):
    params = {"startDate": since_iso, "descending": "false", "limit": 100}
    baskets = []
    newest_ts = None

    while True:
        data = api_get(PURCHASE_API, token, params)
        purchases = data.get("purchases", [])
        if not purchases:
            break
        for p in purchases:
            if p.get("refund") or p.get("refunded"):
                continue
            created = p.get("created", "")
            unix_ts = iso_to_unix(created)
            items = []
            for product in p.get("products", []):
                if product.get("type") != "PRODUCT":
                    continue
                uuid = product.get("productUuid", "")
                if uuid not in whitelist:
                    continue
                name, price = whitelist[uuid]
                try:
                    qty = int(float(product.get("quantity", "0")))
                except ValueError:
                    qty = 0
                if qty <= 0:
                    continue
                items.append((name, qty, price))
            if items:
                baskets.append((unix_ts, items))
                newest_ts = created
        last_hash = data.get("lastPurchaseHash")
        if not last_hash or len(purchases) < 100:
            break
        params["lastPurchaseHash"] = last_hash

    return baskets, newest_ts


# main


def main():
    state = load_state()
    is_first = "last_purchase_ts" not in state

    if is_first:
        since = (datetime.now(timezone.utc) - timedelta(days=WINDOW_DAYS)).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        )
        state["last_purchase_ts"] = since
        print(f"First run — fetching purchases since {since}")

    whitelist = {}
    last_product_fetch = 0

    print(f"Sync started. Polling every {POLL_INTERVAL}s...")

    while True:
        try:
            token = get_access_token()

            if time.time() - last_product_fetch > PRODUCT_REFRESH_SECS:
                whitelist = fetch_product_whitelist(token)
                last_product_fetch = time.time()
                print(f"Product whitelist refreshed: {len(whitelist)} products")

            since = state["last_purchase_ts"]
            baskets, newest_ts = fetch_new_purchases(token, since, whitelist)

            if baskets:
                conn = open_db()
                try:
                    with conn:
                        for purchased_at, items in baskets:
                            for name, _, price in items:
                                ensure_product(conn, name, price)
                            insert_purchase_rows(
                                conn, purchased_at, [(n, q) for n, q, _ in items]
                            )
                        recalculate_amounts(conn)
                        if is_first:
                            rebuild_sales_history(conn)
                            is_first = False
                        else:
                            update_current_hour_history(conn)
                    print(f"Synced {len(baskets)} purchases, newest: {newest_ts}")
                finally:
                    conn.close()

                state["last_purchase_ts"] = advance_iso(newest_ts)
                save_state(state)

            conn = open_db()
            try:
                with conn:
                    prune_old_data(conn)
                    with open(HEARTBEAT_FILE, "w") as f:
                        f.write(str(int(time.time())))
            finally:
                conn.close()

        except urllib.error.URLError as e:
            print(f"Network error (will retry in {POLL_INTERVAL}s): {e.reason}")
        except urllib.error.HTTPError as e:
            print(f"HTTP error {e.code} (will retry in {POLL_INTERVAL}s): {e.reason}")
        except Exception as e:
            print(f"Error (will retry in {POLL_INTERVAL}s): {e}")

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
