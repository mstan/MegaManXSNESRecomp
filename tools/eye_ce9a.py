#!/usr/bin/env python3
"""Capture the EXACT operands CE9A receives at the blue-eye launch.

AED9 stages: eyeX->$0000, eyeY->$0002, targetX($0BAD)->$0004, targetY($0BB0)->
$0006, then JSL $80CE9A. We arm the trace on $0000-$0007 (plus the eye region),
reproduce a launch in one tight pass, then pull the writes to $0000/$0004 whose
func is the launch handler (bank_08_AED9) — those ARE the CE9A inputs.

Usage: python tools/eye_ce9a.py
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))

c = DebugClient(PORT, name=f"dbg:{PORT}")
c.query_raw("trace_wram 0000 0007")     # CE9A scratch (additive range; never reset)
c.query_raw("trace_wram 0e00 1fff")
c.query_raw("loadstate 1")
c.query_raw("set_controller a"); c.query_raw("step 60"); c.query_raw("clear_controller")
for _ in range(45):
    c.query_raw("step 12")

def writes(addr):
    j = json.loads(c.query_raw(f"wram_writes_at {addr} 0 999999999 4096"))
    return j.get("matches", [])

# AED9-staged CE9A inputs: filter by func mentioning AED9.
for addr, name in [("0000", "eyeX"), ("0002", "eyeY"), ("0004", "targetX"), ("0006", "targetY")]:
    ms = writes(addr)
    aed9 = [m for m in ms if "AED9" in m["func"] or "AED9" in m["parent"]]
    print(f"\n## ${addr} ({name}): {len(ms)} total writes, {len(aed9)} by AED9")
    for m in aed9[-6:]:
        print(f"   f{m['f']}: {m['old']} -> {m['val']}  func={m['func']} parent={m['parent']}")
    # also show the distinct funcs writing it (in case AED9 inlined under another name)
    funcs = {}
    for m in ms:
        funcs[m["func"]] = funcs.get(m["func"], 0) + 1
    top = sorted(funcs.items(), key=lambda kv: -kv[1])[:5]
    print(f"   top funcs: {top}")
