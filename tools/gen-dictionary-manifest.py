#!/usr/bin/env python3
"""Generate the spell-check dictionary manifest (issue #46).

Given a directory of Chromium `.bdic` files, write a manifest.json that the app's
DictionaryManager reads to list, download and verify each dictionary:

    {"dictionaries": [{"code": "en_US", "size": 123, "sha256": "…"}, …]}

The .bdic are produced by the normal build (qwebengine_convert_dict) into
<build>/qtwebengine_dictionaries. The `dictionaries` GitHub release then carries
those .bdic plus this manifest as assets.

Usage:
    gen-dictionary-manifest.py <dir-of-bdic> [> manifest.json]
"""
import hashlib
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: gen-dictionary-manifest.py <dir-of-bdic>\n")
        return 2
    directory = sys.argv[1]
    if not os.path.isdir(directory):
        sys.stderr.write(f"not a directory: {directory}\n")
        return 1

    entries = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".bdic"):
            continue
        path = os.path.join(directory, name)
        with open(path, "rb") as f:
            data = f.read()
        entries.append(
            {
                "code": name[: -len(".bdic")],
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )

    if not entries:
        sys.stderr.write(f"no .bdic files in {directory}\n")
        return 1

    json.dump({"dictionaries": entries}, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")
    sys.stderr.write(f"{len(entries)} dictionaries\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
