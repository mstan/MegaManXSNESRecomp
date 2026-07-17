#!/usr/bin/env python3
"""Definitively capture eyeX vs targetX at the blue-eye launch.

Launch signature: the X-velocity field [obj+0x1a] is set to 0xFE28 (-472).
We poll the object region in small steps; when a 16-bit word becomes 0xFE28,
that word is [obj+0x1a] -> base = addr-0x1a. ACDD sets the eye X at [obj+0x05]
(16-bit), so eyeX = u16(base+0x05). Read eyeX, eyeY([base+08]), and the
targets $0BAD/$0BB0 at that exact frame. All in one process (no eviction gap).

Usage: python tools/eye_launch.py
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200
VEL = 0xFE28

def dump(c, lo, n):
    return bytes.fromhex(json.loads(c.query_raw(f"dump_ram {lo:x} {n}"))["hex"])
def u16(b, o):
    return b[o] | (b[o+1] << 8)

def main():
    c = DebugClient(PORT, name=f"dbg:{PORT}")
    c.query_raw("loadstate 1")
    c.query_raw("set_controller a"); c.query_raw("step 60"); c.query_raw("clear_controller")
    prev = dump(c, OBJ_LO, OBJ_LEN)
    for it in range(120):
        c.query_raw("step 6")
        cur = dump(c, OBJ_LO, OBJ_LEN)
        # find a word that just became 0xFE28 (the launch velocity)
        for o in range(0, OBJ_LEN - 1):
            if u16(cur, o) == VEL and u16(prev, o) != VEL:
                base = OBJ_LO + o - 0x1a
                bo = base - OBJ_LO
                eyeX = u16(cur, bo + 0x05)
                eyeY = u16(cur, bo + 0x08)
                bad = dump(c, 0x0BAD, 2); bb0 = dump(c, 0x0BB0, 2)
                tgtX = u16(bad, 0); tgtY = u16(bb0, 0)
                frame = json.loads(c.query_raw("ping"))["frame"]
                print(json.dumps({
                    "launch_frame": frame, "iter": it,
                    "eye_base": f"0x{base:05x}", "velX_addr": f"0x{OBJ_LO+o:05x}",
                    "eyeX[D+05]": eyeX, "eyeY[D+08]": eyeY,
                    "targetX$0BAD": tgtX, "targetY$0BB0": tgtY,
                    "abs_dx": abs(eyeX - tgtX), "abs_dy": abs(eyeY - tgtY),
                }, indent=2))
                # also dump the eye struct head for sanity
                head = " ".join(f"{u16(cur, bo+2*k):04x}" for k in range(20))
                print(f"# eye struct @0x{base:05x}: {head}")
                return
        prev = cur
    print("# no 0xFE28 launch velocity seen in window (no blue-eye launch this run?)")

if __name__ == "__main__":
    main()
