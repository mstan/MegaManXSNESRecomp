#!/usr/bin/env python3
"""Targeted ring read of a candidate eye struct over a frame window.

Usage: python tools/eye_ring_probe.py <lo_hex> <hi_hex> <f_from> <f_to>
Prints, per address, every VALUE-CHANGING write (old->val) with func, so we can
see the +0x400 position discontinuity and the eye-AI (AED9/AF21/820A) writes.
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient

c = DebugClient(int(os.environ.get("DBG_PORT", "4379")))
lo = int(sys.argv[1], 16); hi = int(sys.argv[2], 16)
ff = int(sys.argv[3]); ft = int(sys.argv[4])
print(f"# ring writes [{ff}..{ft}] for ${lo:05x}..${hi:05x} (value-changing only)")
for a in range(lo, hi + 1):
    r = json.loads(c.query_raw(f"wram_writes_at {a:x} {ff} {ft} 400"))
    ms = r.get("matches", [])
    chg = [m for m in ms if m["old"] != m["val"]]
    if not chg:
        continue
    seg = [f"f{m['f']}:{m['old']}->{m['val']}[{m['func']}]" for m in chg]
    print(f"  ${a:05x}: " + "  ".join(seg[:16]) + (" ..." if len(seg) > 16 else ""))
