#!/usr/bin/env python3
"""Evaluate a JS expression in the WhatsApp Web page of a running Whatly
instance over CDP and print the result. Used by make-screenshots.sh to wait for
the chat list to render.

    python3 tools/cdp-eval.py '(document.querySelectorAll("#pane-side [role=\\"row\\"]").length)' [--port 9222]

Requires: websockets.
"""
import sys, json, argparse, urllib.request
from urllib.parse import urlparse


def _is_whatsapp(url):
    # Match on the URL host, not a substring: "whatsapp.com" in the raw URL
    # would also match e.g. evil.com/whatsapp.com (CodeQL
    # py/incomplete-url-substring-sanitization).
    host = (urlparse(url or "").hostname or "").lower()
    return host == "whatsapp.com" or host.endswith(".whatsapp.com")


ap = argparse.ArgumentParser()
ap.add_argument("expr")
ap.add_argument("--port", type=int, default=9222)
a = ap.parse_args()

data = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json" % a.port))
ws_url = next(t["webSocketDebuggerUrl"] for t in data
              if t.get("type") == "page" and _is_whatsapp(t.get("url")))

from websockets.sync.client import connect
with connect(ws_url, max_size=None) as ws:
    ws.send(json.dumps({"id": 1, "method": "Runtime.enable"})); ws.recv()
    ws.send(json.dumps({"id": 2, "method": "Runtime.evaluate",
                        "params": {"expression": a.expr, "returnByValue": True}}))
    while True:
        m = json.loads(ws.recv())
        if m.get("id") == 2:
            v = m.get("result", {}).get("result", {}).get("value")
            print(v if v is not None else "")
            break
