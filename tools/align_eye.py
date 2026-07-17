#!/usr/bin/env python3
"""Robustly identify the blue eye's CE9A-input X field and track it.

Anchors on TWO signatures unique to the malformed blue eye (avoids the
velocity-only anchor that also matches the nose):
  (1) the field is in LEVEL range (~4900-5400) at the socket/creation phase
      (ACDD writes the table position there), AND
  (2) it is the field that then MARCHES the farthest (the runaway flight).
That field IS what AED9 stages into CE9A as eyeX. We print its trajectory
(socket -> launch -> flight) alongside $0BAD, so we can see exactly when/if it
leaves level space.

Works on the recomp (live, default) or on an mmxref trace (--oracle PATH).

Usage:
  python tools/align_eye.py                       # recomp, live
  python tools/align_eye.py --oracle PATH.jsonl   # mmxref hardware trace
"""
import os, sys, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200
def u16(b, o): return b[o] | (b[o+1] << 8)

def recomp_snaps():
    from sneslib.client import DebugClient
    c = DebugClient(int(os.environ.get("DBG_PORT", "4379")))
    c.query_raw("loadstate 1")
    c.query_raw("set_controller a"); c.query_raw("step 60"); c.query_raw("clear_controller")
    snaps, bads = [], []
    for _ in range(60):
        c.query_raw("step 8")
        snaps.append(bytes.fromhex(json.loads(c.query_raw(f"dump_ram {OBJ_LO:x} {OBJ_LEN}"))["hex"]))
        bads.append(u16(bytes.fromhex(json.loads(c.query_raw("dump_ram bad 2"))["hex"]), 0))
    return snaps, bads

def oracle_snaps(path):
    tl = collections.defaultdict(list)
    for line in open(path):
        line = line.strip()
        if not line: continue
        e = json.loads(line); tl[int(e["adr"],16)].append((e["f"], int(e["val"],16)))
    def val_at(a, f):
        v = None
        for ff, vv in tl.get(a, []):
            if ff <= f: v = vv
            else: break
        return v
    frames = sorted({f for seq in tl.values() for f, _ in seq})
    # sample ~60 evenly
    step = max(1, len(frames)//60)
    fs = frames[::step]
    snaps = []
    for f in fs:
        b = bytearray(OBJ_LEN)
        for a in range(OBJ_LO, OBJ_LO+OBJ_LEN):
            v = val_at(a, f)
            if v is not None: b[a-OBJ_LO] = v & 0xFF
        snaps.append(bytes(b))
    bads = []
    for f in fs:
        lo = val_at(0x0BAD, f) or 0; hi = val_at(0x0BAE, f) or 0
        bads.append(lo | (hi<<8))
    return snaps, bads

def analyze(snaps, bads, label):
    N = len(snaps)
    # candidate CE9A-input field: 16-bit word that is level-range early AND has large span
    cands = []
    for o in range(0, OBJ_LEN-1):
        vals = [u16(b, o) for b in snaps]
        early = vals[:max(3, N//6)]
        if not any(4900 <= v <= 5400 for v in early):
            continue
        span = max(vals) - min(vals)
        # monotonic-ish runaway score
        d = [b-a for a, b in zip(vals, vals[1:])]
        d = [x-65536 if x>32768 else x+65536 if x<-32768 else x for x in d]
        mv = [x for x in d if x]
        if not mv: continue
        mono = max(sum(1 for x in mv if x>0), sum(1 for x in mv if x<0))/len(mv)
        cands.append((span*mono, OBJ_LO+o, vals, mono, span))
    cands.sort(reverse=True)
    print(f"\n==== {label}: top CE9A-input candidates (level-early + runaway) ====")
    for score, addr, vals, mono, span in cands[:4]:
        traj = " ".join(str(v) for v in vals[::max(1,N//12)])
        print(f"  ${addr:05x} mono={mono:.2f} span={span} first={vals[0]} last={vals[-1]}")
        print(f"     traj: {traj}")
    print(f"  $0BAD over capture: {bads[0]} .. {bads[-1]}")
    return cands

if __name__ == "__main__":
    if "--oracle" in sys.argv:
        path = sys.argv[sys.argv.index("--oracle")+1]
        snaps, bads = oracle_snaps(path)
        analyze(snaps, bads, f"ORACLE {os.path.basename(path)}")
    else:
        snaps, bads = recomp_snaps()
        analyze(snaps, bads, "RECOMP")
