#!/usr/bin/env python3
"""Capture the WhatsApp Web content of a running Whatly instance over the
Chrome DevTools Protocol, and (optionally) blur regions to anonymise it.

Whatly must be running with a remote-debugging port open, e.g.:
    QTWEBENGINE_CHROMIUM_FLAGS="--remote-debugging-port=9222 --remote-allow-origins=*" whatly

Usage:
    python3 tools/cdp-screenshot.py OUT.png [--port 9222] [--blur X,Y,W,H[,radius] ...]

Each --blur applies a Gaussian blur to a rectangle (belt-and-suspenders on top
of the app's own privacy blur, e.g. for the label-filter pills the blur feature
does not cover). Turn the privacy blur on in Settings first so the chat rows are
already blurred.

Requires: websockets, Pillow.
"""
import sys, json, base64, argparse, urllib.request
from urllib.parse import urlparse


def _is_whatsapp(url):
    # Match on the URL host, not a substring: "whatsapp.com" in the raw URL
    # would also match e.g. evil.com/whatsapp.com (CodeQL
    # py/incomplete-url-substring-sanitization).
    host = (urlparse(url or "").hostname or "").lower()
    return host == "whatsapp.com" or host.endswith(".whatsapp.com")


def capture(port):
    data = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json" % port))
    ws_url = next(t["webSocketDebuggerUrl"] for t in data
                  if t.get("type") == "page" and _is_whatsapp(t.get("url")))
    from websockets.sync.client import connect
    with connect(ws_url, max_size=None) as ws:
        def cmd(i, method, params=None):
            ws.send(json.dumps({"id": i, "method": method, "params": params or {}}))
            while True:
                m = json.loads(ws.recv())
                if m.get("id") == i:
                    return m
        cmd(1, "Page.enable")
        r = cmd(2, "Page.captureScreenshot", {"format": "png", "captureBeyondViewport": False})
        return base64.b64decode(r["result"]["data"])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", type=int, default=9222)
    ap.add_argument("--blur", action="append", default=[],
                    help="X,Y,W,H[,radius] region to blur (repeatable)")
    a = ap.parse_args()
    png = capture(a.port)
    open(a.out, "wb").write(png)
    if a.blur:
        from PIL import Image, ImageFilter
        im = Image.open(a.out).convert("RGB")
        for spec in a.blur:
            v = [int(x) for x in spec.split(",")]
            x, y, w, h = v[:4]
            radius = v[4] if len(v) > 4 else 10
            box = (x, y, x + w, y + h)
            im.paste(im.crop(box).filter(ImageFilter.GaussianBlur(radius)), box)
        im.save(a.out)
    print("wrote", a.out)

if __name__ == "__main__":
    main()
