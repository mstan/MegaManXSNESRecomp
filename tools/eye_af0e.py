#!/usr/bin/env python3
"""Catch the REAL eye-launch staging frame and read the exact CE9A inputs.

AED9's staging is gated by `LDA $0F; BPL $AF1C` -- it only stages when D+$0F is
negative. The JSL $80CE9A at $08:AF0E executes ONLY on that true staging frame,
and at that instant $0000-$0006 hold the staged eyeX/eyeY/tgtX/tgtY (absolute,
fixed addresses). We arm a block_watch at $08:AF0E capturing those 8 bytes.

Decisive: eyeX ~83 (SCREEN) vs ~5192 (LEVEL)? And does fly-timer (=dist>>1)
match CE9A over these inputs?

Usage: python tools/eye_af0e.py
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))
AF0E = 0x08AF0E


def ce9a(dx, dy):
    """Faithful reimplementation of $00:CE9A: dist = sqrt(dx^2+dy^2) via a
    scale-to-8-bit trick. Returns the 16-bit-ish distance CE9A would produce."""
    import math
    ax, ay = abs(dx), abs(dy)
    mx, mn = (ax, ay) if ax >= ay else (ay, ax)
    sh = 0
    while mx & 0xFF00:
        mx >>= 1
        mn >>= 1
        sh += 1
    d = int(math.isqrt(mx * mx + mn * mn))
    return d << sh


def main():
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    def q(cmd):
        return json.loads(c.query_raw(cmd))

    c.query_raw("block_watch_clear")
    print(f"# armed AF0E: {q(f'block_watch_arm {AF0E:06x} 0,1,2,3,4,5,6,7 12')}",
          file=sys.stderr)
    print(f"# loadstate: {q('loadstate 1')}", file=sys.stderr)
    c.query_raw("set_controller a")
    c.query_raw("step 60")
    c.query_raw("clear_controller")

    hit = None
    for i in range(120):
        q("step 8")
        w = q("block_watch_get 0")
        sl = w.get("slots", [])
        if sl and sl[0].get("hit_count", 0) > 0:
            hit = sl[0]
            break

    if not hit:
        print("# AF0E never fired -- no real launch staging captured this run. "
              "Re-run (RNG/slot varies) or widen the drive.")
        return

    print(f"# AF0E fired {hit['hit_count']}x (real launch staging):")
    for e in hit["events"]:
        v = [int(x, 16) for x in e["vals"]]
        eyeX = v[0] | v[1] << 8
        eyeY = v[2] | v[3] << 8
        tgtX = v[4] | v[5] << 8
        tgtY = v[6] | v[7] << 8
        dx, dy = eyeX - tgtX, eyeY - tgtY
        dist = ce9a(dx, dy)
        timer = dist >> 1
        D = int(e["D"], 16)
        print(f"\n  frame={e['frame']} D=0x{D:04x}")
        print(f"    eyeX $0000 = {eyeX:5d} (0x{eyeX:04x})    "
              f"tgtX $0004 = {tgtX:5d} (0x{tgtX:04x})    dx={dx}")
        print(f"    eyeY $0002 = {eyeY:5d} (0x{eyeY:04x})    "
              f"tgtY $0006 = {tgtY:5d} (0x{tgtY:04x})    dy={dy}")
        print(f"    -> CE9A dist={dist}  fly-timer(=dist>>1)={timer}  "
              f"(~{timer/60:.1f}s of flight)")
        print(f"    stack={e.get('stack', [])}")
        if abs(dx) > 2000:
            print(f"    >>> |dx|={abs(dx)} HUGE: eyeX is SCREEN-space while "
                  f"target is LEVEL-space. ROOT-CAUSE INPUT CONFIRMED.")
        elif abs(dx) < 400:
            print(f"    >>> |dx|={abs(dx)} small: eyeX/target consistent here.")


if __name__ == "__main__":
    main()
