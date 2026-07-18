#!/usr/bin/env python3

import os
import sqlite3
import sys
import time
import random
from datetime import datetime, timezone


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


def open_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def get_products():
    conn = open_db()
    rows = conn.execute("SELECT NAME FROM PRODUCT ORDER BY NAME;").fetchall()
    conn.close()
    return [r[0] for r in rows]


def insert_purchase(items, purchased_at=None):
    ts = purchased_at or int(time.time())
    conn = open_db()
    with conn:
        conn.executemany(
            "INSERT INTO PURCHASES(purchased_at, name, units) VALUES (?, ?, ?);",
            [(ts, name, units) for name, units in items],
        )
        cutoff = ts - 365 * 86400
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
        rebuild_sales_history(conn)
    conn.close()
    return ts


def rebuild_sales_history(conn):
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


def clear_test_purchases(since_ts):
    conn = open_db()
    with conn:
        conn.execute("DELETE FROM PURCHASES WHERE purchased_at >= ?;", (since_ts,))
        cutoff = int(time.time()) - 365 * 86400
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
        rebuild_sales_history(conn)
    conn.close()


def offer_cleanup(start_ts):
    cleanup = input("Clean up test purchases? (y/n): ").strip().lower()
    if cleanup == "y":
        clear_test_purchases(start_ts)
        print("Cleaned up.")


def separator(title):
    print(f"\n{'─' * 60}")
    print(f"  {title}")
    print("─" * 60)


def test_single_item():
    separator("TEST: Single item purchase")
    products = get_products()
    if not products:
        print("No products in DB. Run sync.py first.")
        return
    start_ts = int(time.time())
    name = random.choice(products)
    print(f"Inserting: {name} x1")
    t0 = time.time()
    insert_purchase([(name, 1)], purchased_at=start_ts)
    print(f"Written to DB in {(time.time() - t0) * 1000:.1f}ms")
    print("Watch the LATEST PURCHASE panel — should update within 1-2s.")
    input("Press Enter when done observing...")
    offer_cleanup(start_ts)


def test_multi_item():
    separator("TEST: Multi-item purchase (basket grouping)")
    products = get_products()
    if len(products) < 3:
        print("Need at least 3 products.")
        return
    start_ts = int(time.time())
    items = [(p, random.randint(1, 3)) for p in random.sample(products, 3)]
    print("Inserting basket:")
    for name, qty in items:
        print(f"  {name} x{qty}")
    insert_purchase(items, purchased_at=start_ts)
    print("All items share the same timestamp — should appear as one purchase.")
    input("Press Enter when done observing...")
    offer_cleanup(start_ts)


def test_rapid_sequential():
    separator("TEST: Rapid sequential purchases (10 purchases, 1s apart)")
    products = get_products()
    if not products:
        print("No products in DB.")
        return
    start_ts = int(time.time())
    for i in range(10):
        name = random.choice(products)
        qty = random.randint(1, 3)
        ts = start_ts + i
        insert_purchase([(name, qty)], purchased_at=ts)
        print(f"  [{i + 1}/10] {name} x{qty}")
        time.sleep(1)
    print("Done. Check that each purchase appeared correctly.")
    offer_cleanup(start_ts)


def test_stress():
    separator("TEST: Stress — 20 purchases as fast as possible")
    products = get_products()
    if not products:
        print("No products in DB.")
        return
    start_ts = int(time.time())
    times = []
    for i in range(20):
        name = random.choice(products)
        t0 = time.time()
        insert_purchase([(name, 1)], purchased_at=start_ts + i)
        times.append((time.time() - t0) * 1000)
    avg = sum(times) / len(times)
    print(f"20 inserts complete. Avg: {avg:.1f}ms  Max: {max(times):.1f}ms")
    offer_cleanup(start_ts)


def test_long_name():
    separator("TEST: Long product name (truncation/alignment)")
    long_name = "Superlang produktnavn med masse tekst som ikke får plass"
    conn = open_db()
    with conn:
        conn.execute(
            "INSERT OR IGNORE INTO PRODUCT(NAME, AMOUNT, ID) "
            "VALUES (?, 0, (SELECT COALESCE(MAX(ID),0)+1 FROM PRODUCT));",
            (long_name,),
        )
    conn.close()
    start_ts = int(time.time())
    insert_purchase([(long_name, 2)], purchased_at=start_ts)
    print(f"Inserted: '{long_name}'")
    print("Check truncation with … in leaderboard and latest panel.")
    input("Press Enter when done observing...")
    conn = open_db()
    with conn:
        conn.execute("DELETE FROM PURCHASES WHERE purchased_at >= ?;", (start_ts,))
        conn.execute("DELETE FROM PRODUCT WHERE NAME = ?;", (long_name,))
        cutoff = int(time.time()) - 365 * 86400
        conn.execute(
            """
            UPDATE PRODUCT SET AMOUNT = (
                SELECT COALESCE(SUM(units), 0) FROM PURCHASES
                WHERE PURCHASES.name = PRODUCT.NAME AND PURCHASES.purchased_at >= ?
            );
        """,
            (cutoff,),
        )
    conn.close()
    print("Cleaned up.")


def test_max_items():
    separator("TEST: Maximum items in one purchase (page cycling)")
    products = get_products()
    n = min(len(products), 16)
    start_ts = int(time.time())
    items = [(p, 1) for p in products[:n]]
    insert_purchase(items, purchased_at=start_ts)
    print(f"Inserted {n} items in one purchase.")
    print("Latest purchase panel should cycle through pages of items.")
    input("Press Enter when done observing...")
    offer_cleanup(start_ts)


def test_leaderboard_reorder():
    separator("TEST: Leaderboard reorder — boosting a low-ranked product")
    products = get_products()
    if len(products) < 6:
        print("Need at least 6 products.")
        return
    conn = open_db()
    row = conn.execute(
        "SELECT NAME, AMOUNT FROM PRODUCT ORDER BY AMOUNT ASC LIMIT 1;"
    ).fetchone()
    conn.close()
    if not row:
        print("No product data.")
        return
    name, current = row[0], row[1]
    start_ts = int(time.time())
    print(f"Boosting '{name}' (currently {current} units) with 9999 units.")
    insert_purchase([(name, 9999)], purchased_at=start_ts)
    print("Watch the Hall of Fame — should reorder within 1s.")
    input("Press Enter when done observing...")
    offer_cleanup(start_ts)


def test_response_rate():
    separator("TEST: DB write latency benchmark (10 samples)")
    products = get_products()
    if not products:
        print("No products in DB.")
        return
    start_ts = int(time.time())
    times = []
    for i in range(10):
        name = random.choice(products)
        t0 = time.time()
        insert_purchase([(name, 1)], purchased_at=start_ts + i)
        times.append((time.time() - t0) * 1000)
        time.sleep(0.1)
    print(f"Min:  {min(times):.1f}ms")
    print(f"Max:  {max(times):.1f}ms")
    print(f"Avg:  {sum(times) / len(times):.1f}ms")
    print(f"\nTUI polls every 1s, so visual latency = write time + up to 1s.")
    offer_cleanup(start_ts)


def test_add_specific():
    separator("TEST: Add specific item, quantity and date")
    products = get_products()
    if not products:
        print("No products in DB. Run sync.py first.")
        return

    print("Available products:")
    for i, name in enumerate(products):
        print(f"  {i + 1}. {name}")

    try:
        idx = int(input("\nSelect product number: ").strip()) - 1
        if idx < 0 or idx >= len(products):
            print("Invalid selection.")
            return
        qty = int(input("Units to add: ").strip())
        if qty <= 0:
            print("Must be > 0.")
            return
        date_str = input("Date (YYYY-MM-DD, leave blank for today): ").strip()
        if date_str:
            dt = datetime.strptime(date_str, "%Y-%m-%d").replace(tzinfo=timezone.utc)
            ts = int(dt.timestamp())
        else:
            ts = int(time.time())
    except ValueError as e:
        print(f"Invalid input: {e}")
        return

    name = products[idx]
    start_ts = ts
    insert_purchase([(name, qty)], purchased_at=ts)
    print(
        f"Inserted: {name} x{qty} at {datetime.fromtimestamp(ts, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}"
    )
    print("Watch the chart update.")
    input("Press Enter when done observing...")
    offer_cleanup(start_ts)


def test_bulk_purchases():
    separator("TEST: Inject X purchases")

    products = get_products()
    if not products:
        print("No products in DB.")
        return

    try:
        count = int(input("Number of purchases to inject: ").strip())
        if count <= 0:
            print("Must be > 0.")
            return
    except ValueError:
        print("Invalid number.")
        return

    start_ts = int(time.time())

    print(f"Inserting {count} purchases...")

    for i in range(count):
        name = random.choice(products)
        qty = random.randint(1, 3)

        insert_purchase([(name, qty)], purchased_at=start_ts + i)

        if (i + 1) % 50 == 0 or i + 1 == count:
            print(f"  {i + 1}/{count}")

    print("Done.")
    offer_cleanup(start_ts)


def get_db_data():
    conn = open_db()
    count = conn.execute(
        "SELECT COUNT(DISTINCT purchased_at) FROM PURCHASES"
    ).fetchone()[0]
    latest = conn.execute(
        "SELECT purchased_at, name, units FROM PURCHASES WHERE purchased_at = (SELECT MAX(purchased_at) FROM PURCHASES) ORDER BY units DESC"
    ).fetchone()
    distinct_products = conn.execute(
        "SELECT COUNT(DISTINCT name) FROM PURCHASES"
    ).fetchone()[0]
    conn.close()
    print()
    print(f"Total purchase count \n{count}\n")
    print(f"Distinct products \n{distinct_products}\n")
    print(f"Latest purchase \n {tuple(latest)}\n")

    print()


TESTS = {
    "1": ("Single item purchase", test_single_item),
    "2": ("Multi-item basket grouping", test_multi_item),
    "3": ("Rapid sequential (10x, 1s apart)", test_rapid_sequential),
    "4": ("Stress test (20x, max speed)", test_stress),
    "5": ("Long product name truncation", test_long_name),
    "6": ("Max items in one purchase", test_max_items),
    "7": ("Leaderboard reorder", test_leaderboard_reorder),
    "8": ("DB write latency benchmark", test_response_rate),
    "9": ("Add specific item and quantity", test_add_specific),
    "10": ("Add bulk purchases", test_bulk_purchases),
    "h": ("View summary of data stored in database", get_db_data),
}


def menu():
    while True:
        for key, (desc, _) in TESTS.items():
            print(f"  {key}. {desc}")
        print("  q. Quit")

        choice = input("\nSelect test: ").strip().lower()
        if choice == "q":
            break
        if choice in TESTS:
            TESTS[choice][1]()
        else:
            print("Invalid choice.")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        name = sys.argv[1]
        match = [
            (k, v) for k, (d, v) in TESTS.items() if d.lower().startswith(name.lower())
        ]
        if match:
            match[0][1]()
        else:
            print(f"Unknown test: {name}")
            print("Available:", ", ".join(d for d, _ in TESTS.values()))
    else:
        menu()
