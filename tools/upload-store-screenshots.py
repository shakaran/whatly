#!/usr/bin/env python3
"""Upload screenshots (and optionally the icon) to the Whatly snap's store
listing via the Snap Store dashboard binary-metadata API.

The store keeps this media separately from the snap; snapcraft has no command
for it, so this talks to the dashboard API directly with your login macaroon.

Get a macaroon first (interactive, needs your Ubuntu One 2FA):

    snapcraft export-login snapcreds.txt

Then:

    python3 tools/upload-store-screenshots.py snapcreds.txt \
        docs/img/card-chat.png docs/img/card-themes.png docs/img/card-lightdark.png \
        docs/img/card-lock.png docs/img/card-settings.png docs/img/card-about.png

Pass --icon PATH to also set the store icon. The list of screenshots you pass
REPLACES the current set (the API is a full PUT).

Requires: requests  (pip install requests)
"""
import sys, os, json, argparse

SNAP_ID = "D5IMvJyeKAh8BunJsGQlV2Yle5acgW5q"  # whatly
BASE = "https://dashboard.snapcraft.io/dev/api/snaps"

def read_macaroon(path):
    data = open(path).read().strip()
    # snapcraft export-login writes a single-line serialized macaroon token that
    # goes straight into the Authorization header.
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("creds", help="file from `snapcraft export-login`")
    ap.add_argument("screenshots", nargs="*", help="screenshot image paths (in order)")
    ap.add_argument("--icon", help="path to the store icon (optional)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    import requests
    macaroon = read_macaroon(a.creds)
    headers = {"Authorization": f"Macaroon {macaroon}"}
    url = f"{BASE}/{SNAP_ID}/binary-metadata"

    # Read current metadata to satisfy the If-Match/updated_metadata contract.
    r = requests.get(url, headers={**headers, "Accept": "application/json"})
    print("GET binary-metadata:", r.status_code)
    if r.status_code != 200:
        print(r.text[:500]); sys.exit(1)

    info = []
    files = {}
    for i, path in enumerate(a.screenshots):
        fn = os.path.basename(path)
        info.append({"type": "screenshot", "filename": fn})
        files[fn] = (fn, open(path, "rb"), "image/png")
    if a.icon:
        fn = os.path.basename(a.icon)
        info.append({"type": "icon", "filename": fn})
        files[fn] = (fn, open(a.icon, "rb"), "image/png")

    if a.dry_run:
        print("Would upload:", json.dumps(info, indent=2)); return

    files["info"] = (None, json.dumps(info), "application/json")
    put = requests.put(url, headers={**headers, "If-Match": r.headers.get("ETag", "")}, files=files)
    print("PUT binary-metadata:", put.status_code)
    print(put.text[:800])

if __name__ == "__main__":
    main()
