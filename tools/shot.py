#!/usr/bin/env python3
"""Capture a PPU screenshot from the always-on debug server and emit a PNG.

Usage: python tools/shot.py [out.png]

Drives the `screenshot` debug command (renders the live PPU state to a 24-bit
BMP, NO pause required), then converts to PNG so it can be viewed inline.
Default output: <repo>/tools/_shot.png. Prints the captured frame number.
"""
import os
import sys
import json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))


def main():
    out_png = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "_shot.png")
    out_png = os.path.abspath(out_png)
    bmp = os.path.splitext(out_png)[0] + ".bmp"
    c = DebugClient(PORT, name=f"dbg:{PORT}")
    # The server echoes the path verbatim into its JSON reply; Windows
    # backslashes would make that invalid JSON. Forward slashes work for
    # fopen on Windows and stay JSON-safe.
    raw = c.query_raw(f"screenshot {bmp.replace(os.sep, '/')}")
    try:
        j = json.loads(raw)
    except json.JSONDecodeError:
        j = {"ok": os.path.exists(bmp)}
    if not j.get("ok"):
        print(raw)
        return 1
    from PIL import Image
    Image.open(bmp).save(out_png)
    print(json.dumps({"ok": True, "png": out_png, "frame": j.get("frame")}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
