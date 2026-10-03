#!/usr/bin/env python3
"""Load airline logos into the n8n `airline_icons` data table that the
workflow's Lookup Icon node reads.

Creates the table on first run, then upserts one row per CSV line, keyed by
`code`, so re-running it (or importing a CSV of new logos from
`convert_tiles.py --csv`) only adds or replaces those rows.

Usage:
    export N8N_URL=http://192.168.1.10:5678      # your n8n base URL
    export N8N_API_KEY=...                        # Settings > n8n API
    python3 tools/import_icons_n8n.py                        # n8n/airline_icons.csv
    python3 tools/import_icons_n8n.py --csv new_icons.csv

After the first run, open the workflow's Lookup Icon node and pick the
`airline_icons` table -- table IDs differ per n8n instance.

Standard library only.
"""

import argparse
import base64
import csv
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

TABLE_NAME = "airline_icons"
COLUMNS = ("code", "icao", "iata", "name", "icon24")
ICON_BYTES = 24 * 24 * 2
DEFAULT_CSV = Path(__file__).resolve().parent.parent / "n8n" / "airline_icons.csv"


def api(method, path, body=None, query=None):
    base = os.environ.get("N8N_URL", "").rstrip("/")
    key = os.environ.get("N8N_API_KEY", "")
    if not base or not key:
        sys.exit("Set N8N_URL and N8N_API_KEY first (see --help)")
    if not base.startswith("http"):
        base = "http://" + base
    url = f"{base}/api/v1{path}" + (f"?{urllib.parse.urlencode(query)}" if query else "")
    req = urllib.request.Request(
        url, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"X-N8N-API-KEY": key, "Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        sys.exit(f"n8n {method} {path} failed: HTTP {e.code} {e.read().decode(errors='replace')[:500]}")
    except urllib.error.URLError as e:
        sys.exit(f"Can't reach n8n at {base}: {e.reason}")


def find_or_create_table():
    tables = api("GET", "/data-tables", query={"limit": 250})["data"]
    match = [t for t in tables if t["name"] == TABLE_NAME]
    if match:
        table_id = match[0]["id"]
        have = {c["name"] for c in match[0].get("columns", [])}
        for col in COLUMNS:
            if col not in have:
                api("POST", f"/data-tables/{table_id}/columns", {"name": col, "type": "string"})
                print(f"Added column '{col}'")
        print(f"Using existing '{TABLE_NAME}' table (id {table_id})")
        return table_id
    table = api("POST", "/data-tables", {"name": TABLE_NAME,
                                         "columns": [{"name": c, "type": "string"} for c in COLUMNS]})
    print(f"Created '{TABLE_NAME}' table (id {table['id']}) -- select it in the workflow's Lookup Icon node")
    return table["id"]


def read_csv(path):
    rows, errors = [], []
    with open(path, newline="", encoding="utf-8") as f:
        for n, row in enumerate(csv.DictReader(f), start=2):
            row = {c: (row.get(c) or "").strip() for c in COLUMNS}
            if not row["code"]:
                errors.append(f"line {n}: empty code")
                continue
            try:
                size = len(base64.b64decode(row["icon24"], validate=True))
            except ValueError:
                size = -1
            if size != ICON_BYTES:
                errors.append(f"line {n} ({row['code']}): icon24 must decode to {ICON_BYTES} bytes")
                continue
            rows.append(row)
    icaos = [r["icao"] for r in rows if r["icao"]]
    dupes = sorted({i for i in icaos if icaos.count(i) > 1})
    if dupes:
        errors.append(f"duplicate icao codes (each must belong to one row): {', '.join(dupes)}")
    return rows, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV, help=f"CSV to import (default: {DEFAULT_CSV.name})")
    parser.add_argument("--dry-run", action="store_true", help="Validate the CSV without touching n8n")
    args = parser.parse_args()

    rows, errors = read_csv(args.csv)
    if errors:
        sys.exit("CSV problems, nothing imported:\n  " + "\n  ".join(errors))
    print(f"{len(rows)} valid row(s) in {args.csv}")
    if args.dry_run:
        return

    table_id = find_or_create_table()
    for i, row in enumerate(rows, start=1):
        api("POST", f"/data-tables/{table_id}/rows/upsert", {
            "filter": {"type": "and", "filters": [{"columnName": "code", "condition": "eq", "value": row["code"]}]},
            "data": row,
            "returnData": False,
        })
        if i % 50 == 0 or i == len(rows):
            print(f"  {i}/{len(rows)}")
    print(f"Upserted {len(rows)} row(s) into '{TABLE_NAME}'")


if __name__ == "__main__":
    main()
