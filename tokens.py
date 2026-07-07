#!/usr/bin/env python3
"""
Zettle token manager.
Reads credentials from .env in the same directory as this script.
Stores tokens in tokens.json in the same directory.
Since the assertion grant does not return a refresh token, the API key
is used directly to obtain a new access token whenever the current one
expires.
"""

import json
import os
import time
import urllib.request
import urllib.parse

CURRENT_DIR = os.getcwd()
CREDENTIALS_FILE = os.path.join(CURRENT_DIR, ".env")
TOKENS_FILE = os.path.join(CURRENT_DIR, "tokens.json")
TOKEN_ENDPOINT = "https://oauth.zettle.com/token"
EXPIRY_MARGIN = 300  # seconds before expiry to pre-emptively refresh


def load_credentials():
    creds = {}
    with open(CREDENTIALS_FILE) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, _, val = line.partition("=")
            creds[key.strip()] = val.strip()
    return creds


def load_tokens():
    try:
        with open(TOKENS_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_tokens(tokens):
    os.makedirs(os.path.dirname(TOKENS_FILE), exist_ok=True)
    with open(TOKENS_FILE, "w") as f:
        json.dump(tokens, f, indent=2)


def fetch_token(creds: dict) -> dict:
    data = {
        "grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer",
        "client_id": creds["ZETTLE_CLIENT_ID"],
        "assertion": creds["ZETTLE_API_KEY"],
    }
    body = urllib.parse.urlencode(data).encode()
    headers = {"Content-Type": "application/x-www-form-urlencoded"}
    req = urllib.request.Request(TOKEN_ENDPOINT, data=body, headers=headers)
    with urllib.request.urlopen(req) as resp:
        result = json.loads(resp.read())
    result["obtained_at"] = int(time.time())
    return result


def get_valid_access_token() -> str:
    creds = load_credentials()
    tokens = load_tokens()

    obtained_at = tokens.get("obtained_at", 0)
    expires_in = tokens.get("expires_in", 0)
    age = int(time.time()) - obtained_at
    still_valid = tokens.get("access_token") and age < expires_in - EXPIRY_MARGIN

    if not still_valid:
        print("Fetching new access token...")
        tokens = fetch_token(creds)
        save_tokens(tokens)

    return tokens["access_token"]


if __name__ == "__main__":
    token = get_valid_access_token()
    print(
        f"Access token valid. Expires in ~{7200 - (int(time.time()) - load_tokens().get('obtained_at', 0))}s"
    )
