#!/usr/bin/env python3
"""Byte-aligned eye-struct diff: recomp (:4379) vs mmxref hardware trace.

Both sides are aligned by the X-velocity field [base+0x1a] == 0xFE28 (the
launch signature, same ROM table on both). We then read every 16-bit word at
the same offset-from-base and print them side by side, flagging level-range
(~5000-5400) values and mismatches. This pins which struct offset holds the
eye's level X on hardware and what the recomp put there instead.

Usage: python tools/struct_diff.py [mmx_trace.jsonl]
"""
import os, sys, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))
ORACLE = sys.argv[1] if len(sys.argv) > 1 else r"F:\Projects\mmxref\mmx_trace.jsonl"
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200
VEL = 0xFE28
LO_OFF, HI_OFF = -0x08, 0x42   # struct window relative to base

def u16(b, o):
    return b[o] | (b[o+1] << 8)

# ---------- recomp side ----------
def recomp_struct():
    c = DebugClient(PORT, name=f"dbg:{PORT}")
    c.query_raw("loadstate 1")
    c.query_raw("set_controller a"); c.query_raw("step 60"); c.query_raw("clear_controller")
    def dump():
        return bytes.fromhex(json.loads(c.query_raw(f"dump_ram {OBJ_LO:x} {OBJ_LEN}"))["hex"])
    prev = dump()
    for _ in range(120):
        c.query_raw("step 4")
        cur = dump()
        for o in range(0, OBJ_LEN - 1):
            if u16(cur, o) == VEL and u16(prev, o) != VEL:
                base = OBJ_LO + o - 0x1a
                bo = base - OBJ_LO
                tgtX = u16(bytes.fromhex(json.loads(c.query_raw("dump_ram bad 2"))["hex"]), 0)
                tgtY = u16(bytes.fromhex(json.loads(c.query_raw("dump_ram bb0 2"))["hex"]), 0)
                st = {off: u16(cur, bo + off) for off in range(LO_OFF, HI_OFF, 2) if 0 <= bo+off < OBJ_LEN-1}
                return base, st, tgtX, tgtY
        prev = cur
    return None, {}, None, None

# ---------- oracle side ----------
def oracle_struct(path):
    tl = collections.defaultdict(list)
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        e = json.loads(line)
        tl[int(e["adr"], 16)].append((e["f"], int(e["val"], 16)))
    def val_at(a, f):
        v = None
        for ff, vv in tl.get(a, []):
            if ff <= f: v = vv
            else: break
        return v
    # find frame where some word becomes VEL
    frames = sorted({f for seq in tl.values() for f, _ in seq})
    launch = None; base = None
    for f in frames:
        for a in list(tl.keys()):
            if 0xE00 <= a <= 0x1FFF:
                lo = val_at(a, f); hi = val_at(a+1, f)
                if lo is not None and hi is not None and (lo | (hi << 8)) == VEL:
                    launch = f; base = a - 0x1a; break
        if launch:
            break
    if launch is None:
        return None, {}, None, None, None
    def w(a):
        lo = val_at(a, launch); hi = val_at(a+1, launch)
        return (lo or 0) | ((hi or 0) << 8)
    st = {off: w(base + off) for off in range(LO_OFF, HI_OFF, 2)}
    tgtX = w(0x0BAD); tgtY = w(0x0BB0)
    return base, st, tgtX, tgtY, launch

def lvl(v):
    return " <LEVEL" if 4900 <= v <= 5500 else ""

rb, rs, rtx, rty = recomp_struct()
ob, os_, otx, oty, of = oracle_struct(ORACLE)
print(f"# RECOMP base=0x{rb:05x}" if rb else "# RECOMP: no launch captured")
print(f"# ORACLE base={('0x%05x' % ob) if ob else None} launch_f={of}")
print(f"# targetX  recomp=${rtx}  oracle=${otx}    targetY recomp=${rty} oracle=${oty}\n")
print(f"{'off':>5} {'recomp':>8} {'oracle':>8}   flag")
for off in range(LO_OFF, HI_OFF, 2):
    rv = rs.get(off); ov = os_.get(off)
    flag = ""
    if rv is not None and ov is not None and rv != ov:
        flag = "DIFF"
    print(f"{off:+#5x} {('-' if rv is None else rv):>8} {('-' if ov is None else ov):>8}   {flag}{lvl(rv) if rv else ''}{lvl(ov) if ov else ''}")
