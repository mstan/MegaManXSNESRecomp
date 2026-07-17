#!/usr/bin/env python3
"""Capture the EXACT inputs CE9A divides on for the blue-eye launch.

CE9A computes the fly distance from DP $0000(eyeX) $0002(eyeY) $0004(tgtX)
$0006(tgtY) staged by AED9. We arm a block_watch at CE9A entry capturing those
8 DP bytes + the call stack, then drive a launch. The eye sits in AED9 for many
frames calling CE9A each frame, so an AED9-stack CE9A hit is easy to catch.

Decisive: is eyeX ~83 (SCREEN, |dx|~5065 -> bug) or ~5192 (LEVEL, |dx|~44 -> ok)?

Usage: python tools/eye_ce9a_inputs.py
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))


def main():
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    def q(cmd):
        return json.loads(c.query_raw(cmd))

    c.query_raw("block_watch_clear")
    # Arm CE9A at both possible bank encodings; one will fire.
    a0 = q("block_watch_arm 00CE9A 0,1,2,3,4,5,6,7 64")
    a8 = q("block_watch_arm 80CE9A 0,1,2,3,4,5,6,7 64")
    print(f"# armed: slot00={a0.get('ok')} slot80={a8.get('ok')}", file=sys.stderr)

    print(f"# loadstate: {q('loadstate 1')}", file=sys.stderr)
    c.query_raw("set_controller a")
    c.query_raw("step 60")
    c.query_raw("clear_controller")

    def scan_hits():
        out = []
        for slot in range(2):
            w = q(f"block_watch_get {slot}")
            for s in w.get("slots", []):
                for e in s.get("events", []):
                    stk = e.get("stack", [])
                    if any("AED9" in fn for fn in stk):
                        out.append((s["pc24"], e))
        return out

    found = []
    for i in range(90):
        c.query_raw("block_watch_clear")
        q("block_watch_arm 00CE9A 0,1,2,3,4,5,6,7 64")
        q("block_watch_arm 80CE9A 0,1,2,3,4,5,6,7 64")
        q("step 6")
        found = scan_hits()
        if found:
            break

    if not found:
        print("# No AED9-stack CE9A hit captured. Dumping any CE9A hits:")
        for slot in range(2):
            w = q(f"block_watch_get {slot}")
            for s in w.get("slots", []):
                print(f"  pc24={s['pc24']} hit_count={s['hit_count']}")
                for e in s.get("events", [])[:4]:
                    print(f"    f{e['frame']} vals={e['vals']} "
                          f"stack={e.get('stack', [])[-4:]}")
        return

    print(f"# captured {len(found)} AED9-stack CE9A call(s):")
    for pc24, e in found[:6]:
        v = [int(x, 16) for x in e["vals"]]
        eyeX = v[0] | v[1] << 8
        eyeY = v[2] | v[3] << 8
        tgtX = v[4] | v[5] << 8
        tgtY = v[6] | v[7] << 8
        dx = eyeX - tgtX
        dy = eyeY - tgtY
        print(f"\n  pc24={pc24} frame={e['frame']} D=0x{int(e['D'],16):04x} "
              f"m={e['m']} x={e['x']}")
        print(f"    eyeX=$0000={eyeX} (0x{eyeX:04x})   tgtX=$0004={tgtX} "
              f"(0x{tgtX:04x})   dx={dx}")
        print(f"    eyeY=$0002={eyeY} (0x{eyeY:04x})   tgtY=$0006={tgtY} "
              f"(0x{tgtY:04x})   dy={dy}")
        print(f"    stack={e.get('stack', [])}")
        # Interpret
        if abs(dx) > 2000:
            print(f"    >>> |dx|={abs(dx)} HUGE -> eyeX is in WRONG space "
                  f"(screen ~83 vs level target). BUG CONFIRMED at the input.")
        elif abs(dx) < 300:
            print(f"    >>> |dx|={abs(dx)} small -> eyeX correct here; "
                  f"divergence is elsewhere.")


if __name__ == "__main__":
    main()
