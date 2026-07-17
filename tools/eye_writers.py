#!/usr/bin/env python3
"""Aggregate which functions write a given WRAM address (via the always-on
ring), to separate the spawner/init from the per-frame mover.

Usage: python tools/eye_writers.py <hex_addr> [from] [to]
"""
import os, sys, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))

addr = sys.argv[1] if len(sys.argv) > 1 else "00eef"
frm = sys.argv[2] if len(sys.argv) > 2 else "0"
to = sys.argv[3] if len(sys.argv) > 3 else "999999999"
c = DebugClient(PORT, name=f"dbg:{PORT}")
j = json.loads(c.query_raw(f"wram_writes_at {addr} {frm} {to} 4096"))
m = j.get("matches", [])
print(f"# {len(m)} writes to 0x{addr} in [{frm},{to}]")
agg = collections.defaultdict(lambda: {"n": 0, "fmin": 1 << 30, "fmax": 0, "ex": []})
for e in m:
    k = (e["func"], e["parent"])
    a = agg[k]
    a["n"] += 1
    a["fmin"] = min(a["fmin"], e["f"])
    a["fmax"] = max(a["fmax"], e["f"])
    if len(a["ex"]) < 4:
        a["ex"].append((e["f"], e["old"], e["val"]))
for (func, parent), a in sorted(agg.items(), key=lambda kv: kv[1]["fmin"]):
    print(f"\n  func={func}  parent={parent}")
    print(f"    n={a['n']} frames {a['fmin']}..{a['fmax']}")
    for f, o, v in a["ex"]:
        print(f"    f{f}: {o} -> {v}")
