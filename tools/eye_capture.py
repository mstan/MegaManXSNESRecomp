#!/usr/bin/env python3
"""One-shot Rangda Bangda blue-eye launch capture.

Runs the ENTIRE repro in a single process holding one DebugClient connection,
so there are no LLM/wall-clock gaps for the (free-running) game to drift or
kill X. Sequence:

  loadstate 1  ->  dash through door (a, step 60, clear)  ->
  tight loop { step S ; snapshot object RAM $0E00-$2000 + targets $0BA0-$0BC0 }

Then it finds the 16-bit word in the object region that MARCHES at constant
velocity (the flying eye's X position) and prints its trajectory plus the
surrounding object struct and the target globals $0BAD/$0BB0 — everything
needed to classify the ~17x fly-timer.

Snapshots are saved to tools/_eye_capture.json for offline re-analysis.
Screenshots saved at start / mid / end as tools/_cap_{0,1,2}.png.

Usage: python tools/eye_capture.py [step=15] [iters=50]
"""
import os
import sys
import json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200      # $0E00 .. $2000  (eye object tables)
TGT_LO, TGT_LEN = 0x0BA0, 0x20        # $0BAD/$0BB0 target globals live here


def hexresp(c, cmd):
    """dump_ram -> bytes."""
    j = json.loads(c.query_raw(cmd))
    return bytes.fromhex(j["hex"])


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def shot(c, path):
    p = path.replace(os.sep, "/")
    c.query_raw(f"screenshot {p}")
    try:
        from PIL import Image
        Image.open(path).save(os.path.splitext(path)[0] + ".png")
    except Exception:
        pass


def main():
    step = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    iters = int(sys.argv[2]) if len(sys.argv) > 2 else 50
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    # Ensure the always-on ring is armed (never reset it).
    c.query_raw("trace_wram 0e00 1fff")

    # Fresh boss arena: reload + single dash through the door.
    print(json.dumps({"loadstate": json.loads(c.query_raw("loadstate 1"))}))
    c.query_raw("set_controller a")
    c.query_raw("step 60")
    c.query_raw("clear_controller")
    shot(c, os.path.join(HERE, "_cap_0.bmp"))

    snaps = []  # list of {f, obj(hex), tgt(hex)}
    for i in range(iters):
        st = json.loads(c.query_raw(f"step {step}"))
        f = st.get("frame_after")
        obj = hexresp(c, f"dump_ram {OBJ_LO:x} {OBJ_LEN}")
        tgt = hexresp(c, f"dump_ram {TGT_LO:x} {TGT_LEN}")
        snaps.append({"f": f, "obj": obj.hex(), "tgt": tgt.hex()})
        if i == iters // 2:
            shot(c, os.path.join(HERE, "_cap_1.bmp"))
    shot(c, os.path.join(HERE, "_cap_2.bmp"))

    with open(os.path.join(HERE, "_eye_capture.json"), "w") as fh:
        json.dump(snaps, fh)

    # ---- analysis: find the marching 16-bit word (flying eye X position) ----
    objs = [bytes.fromhex(s["obj"]) for s in snaps]
    frames = [s["f"] for s in snaps]
    n = len(objs)
    best = []
    for o in range(0, OBJ_LEN - 1):
        vals = [u16(b, o) for b in objs]
        # signed per-step deltas (wrap-aware: treat as int16 motion)
        deltas = []
        for a, b in zip(vals, vals[1:]):
            d = b - a
            if d > 32768:
                d -= 65536
            elif d < -32768:
                d += 65536
            deltas.append(d)
        moves = [d for d in deltas if d != 0]
        if len(moves) < max(3, n // 4):
            continue
        ups = sum(1 for d in moves if d > 0)
        dns = sum(1 for d in moves if d < 0)
        mono = max(ups, dns) / len(moves)
        disp = abs(sum(deltas))
        if mono >= 0.85 and disp >= 8:
            best.append((disp * mono, OBJ_LO + o, mono, disp, len(moves), vals))
    best.sort(reverse=True)

    print(f"# snapshots={n} step={step} frames {frames[0]}..{frames[-1]}")
    if not best:
        print("# NO marching word found — no eye launch captured in this window.")
        print("# (Check _cap_*.png: eyes attached? X dead/respawned at door?)")
        return
    print("# top marching words (addr, mono, disp, nmoves):")
    for score, addr, mono, disp, nm, vals in best[:6]:
        print(f"  ${addr:05x}  mono={mono:.2f} disp={disp} nmoves={nm} "
              f"first={vals[0]} last={vals[-1]}")

    # Best candidate = eye X position word -> derive struct base D = addr-0x04.
    _, eyex_addr, _, _, _, _ = best[0]
    D = eyex_addr - 0x04
    print(f"\n# best eye-X word ${eyex_addr:05x} -> struct base D=${D:05x}")
    print("# per-snapshot: f  subst[D+03]  Xpos[D+04]  Xhi[D+06]  Ypos[D+08]  "
          "vel[D+1a]  flytimer[D+34]  tgtX$0BAD  tgtY$0BB0")
    for s in snaps:
        b = bytes.fromhex(s["obj"])
        t = bytes.fromhex(s["tgt"])

        def od(off, w=1):
            a = D - OBJ_LO + off
            if a < 0 or a + w > len(b):
                return None
            return u16(b, a) if w == 2 else b[a]

        def gd(addr, w=2):
            a = addr - TGT_LO
            return u16(t, a) if w == 2 else t[a]
        print(f"  {s['f']:6d}  {od(0x03)!s:>4}  {od(0x04,2)!s:>6}  {od(0x06,2)!s:>6}"
              f"  {od(0x08,2)!s:>6}  {od(0x1a,2)!s:>6}  {od(0x34,2)!s:>6}"
              f"  {gd(0x0BAD):6d}  {gd(0x0BB0):6d}")


if __name__ == "__main__":
    main()
