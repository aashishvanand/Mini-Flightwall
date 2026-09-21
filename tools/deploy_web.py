#!/usr/bin/env python3
"""Builds the admin console (matrix64/03_aircraft_display/web, a React/MUI
SPA) and pushes it to a board's internal FATFS partition, where the
firmware's handleWebStatic() serves it from (see the "Admin console static
assets" comment block in 03_aircraft_display.ino).

Every file is gzipped before upload -- the firmware only ever looks up
<path>.gz on FATFS and serves it with Content-Encoding: gzip, so there's no
uncompressed fallback to keep in sync.

/api/web/clear and /api/web/upload are both gated behind HTTP Basic Auth
(same credentials as /api/ota and /api/config -- ADMIN_USER/ADMIN_PASSWORD
in secrets.h). This script does not read secrets.h itself; pass
--user/--password or answer the interactive prompt.

Usage:
    python3 tools/deploy_web.py <board-ip> [--user admin] [--skip-build]
"""

import argparse
import gzip
import sys
from getpass import getpass
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("requests is required: pip install requests")

WEB_DIR = Path(__file__).resolve().parent.parent / "matrix64" / "03_aircraft_display" / "web"
DIST_DIR = WEB_DIR / "dist"


def run_build():
    import subprocess

    print(f"Building {WEB_DIR}...")
    subprocess.run(["npm", "install"], cwd=WEB_DIR, check=True)
    subprocess.run(["npm", "run", "build"], cwd=WEB_DIR, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("board_ip", help="board's IP or hostname, e.g. 192.168.1.42")
    parser.add_argument("--user", default="admin", help="admin username (default: admin)")
    parser.add_argument("--password", default=None, help="admin password (prompted if omitted)")
    parser.add_argument("--skip-build", action="store_true", help="deploy the existing web/dist without rebuilding")
    args = parser.parse_args()

    if not args.skip_build:
        run_build()

    if not DIST_DIR.is_dir():
        sys.exit(f"{DIST_DIR} not found -- run without --skip-build, or `npm run build` in {WEB_DIR} first")

    password = args.password or getpass(f"Admin password for {args.user}@{args.board_ip}: ")
    auth = (args.user, password)
    base = f"http://{args.board_ip}"

    print("Clearing /web/ on the board...")
    r = requests.post(f"{base}/api/web/clear", auth=auth, timeout=10)
    if r.status_code == 401:
        sys.exit("401 Unauthorized -- check --user/--password against ADMIN_USER/ADMIN_PASSWORD in secrets.h")
    r.raise_for_status()

    files = [p for p in DIST_DIR.rglob("*") if p.is_file()]
    if not files:
        sys.exit(f"no files found under {DIST_DIR}")

    for path in files:
        rel = path.relative_to(DIST_DIR).as_posix()
        gz_name = f"{rel}.gz"  # the firmware builds the FATFS path as "/web/" + this filename
        gz_body = gzip.compress(path.read_bytes())
        print(f"  {rel} ({len(gz_body)} bytes gzipped) -> /web/{gz_name}")
        r = requests.post(
            f"{base}/api/web/upload",
            auth=auth,
            files={"file": (gz_name, gz_body)},
            timeout=30,
        )
        if not r.ok or not r.json().get("ok"):
            sys.exit(f"upload failed for {rel}: {r.status_code} {r.text}")

    print("Verifying...")
    r = requests.get(base, timeout=10)
    r.raise_for_status()
    print(f"Deployed. Admin console: {base}/")


if __name__ == "__main__":
    main()
