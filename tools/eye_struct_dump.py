#!/usr/bin/env python3
"""Dump ALL ring writes for an address range over a frame window (no filter).
Usage: python tools/eye_struct_dump.py <lo_hex> <hi_hex> <f_from> <f_to>"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient
c = DebugClient(int(os.environ.get("DBG_PORT", "4379")))
lo = int(sys.argv[1], 16); hi = int(sys.argv[2], 16)
ff = int(sys.argv[3]); ft = int(sys.argv[4])
print(f"# ALL ring writes [{ff}..{ft}] for ${lo:05x}..${hi:05x}")
for a in range(lo, hi + 1):
    r = json.loads(c.query_raw(f"wram_writes_at {a:x} {ff} {ft} 400"))
    ms = r.get("matches", [])
    if not ms:
        continue
    seg = [f"f{m['f']}:{m['old']}->{m['val']}(w{m['w']})[{m['func']}]" for m in ms]
    print(f"  ${a:05x} (+0x{(a-lo):02x}): " + "  ".join(seg[:18]) +
          (" ..." if len(seg) > 18 else ""))
