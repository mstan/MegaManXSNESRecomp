#!/usr/bin/env python3
"""One-shot: reproduce a blue-eye launch, then (no gaps, before ring eviction)
report WHICH functions write the eye's position field — to separate the
spawn/idle positioner (sets the wrong screen-space coord) from the flight
mover (bank_02_820A). Prints earliest writes + the value they set.

Usage: python tools/eye_creation.py [step=12] [iters=45]
"""
import os, sys, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200

def hexb(c, cmd):
    return bytes.fromhex(json.loads(c.query_raw(cmd))["hex"])
def u16(b, o):
    return b[o] | (b[o+1] << 8)

def main():
    step = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    iters = int(sys.argv[2]) if len(sys.argv) > 2 else 45
    c = DebugClient(PORT, name=f"dbg:{PORT}")
    c.query_raw("trace_wram 0e00 1fff")
    c.query_raw("loadstate 1")
    c.query_raw("set_controller a"); c.query_raw("step 60"); c.query_raw("clear_controller")
    snaps = []
    for i in range(iters):
        c.query_raw(f"step {step}")
        snaps.append(bytes.fromhex(json.loads(c.query_raw(f"dump_ram {OBJ_LO:x} {OBJ_LEN}"))["hex"]))
    # locate marching low word -> eye base D = addr-4
    best = None
    for o in range(0, OBJ_LEN-1):
        vals = [u16(b, o) for b in snaps]
        d = [b-a for a, b in zip(vals, vals[1:])]
        d = [x-65536 if x > 32768 else x+65536 if x < -32768 else x for x in d]
        mv = [x for x in d if x]
        if len(mv) < len(snaps)//3:
            continue
        ups = sum(1 for x in mv if x > 0); mono = max(ups, len(mv)-ups)/len(mv)
        disp = abs(sum(d))
        if best is None or disp*mono > best[0]:
            best = (disp*mono, OBJ_LO+o)
    eyex = best[1]; D = eyex - 0x04
    print(f"# eye low-word ${eyex:05x} -> base D=${D:05x}")
    # immediately (no gaps) ask the ring who wrote the position low word + integer
    for label, addr in [("pos-low $D+04", D+0x04), ("intX $D+05", D+0x05)]:
        j = json.loads(c.query_raw(f"wram_writes_at {addr:x} 0 999999999 4096"))
        m = j.get("matches", [])
        agg = collections.defaultdict(lambda: {"n": 0, "fmin": 1<<30, "ex": []})
        for e in m:
            k = (e["func"], e["parent"])
            a = agg[k]; a["n"] += 1; a["fmin"] = min(a["fmin"], e["f"])
            if len(a["ex"]) < 3: a["ex"].append((e["f"], e["old"], e["val"]))
        print(f"\n## writers of {label} (${addr:05x}), {len(m)} writes:")
        for (func, parent), a in sorted(agg.items(), key=lambda kv: kv[1]["fmin"]):
            print(f"  [{a['fmin']}] func={func} parent={parent} n={a['n']}")
            for f, o, v in a["ex"]:
                print(f"       f{f}: {o} -> {v}")

if __name__ == "__main__":
    main()
