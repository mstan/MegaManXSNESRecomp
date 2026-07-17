#!/usr/bin/env python3
"""Decisive: does the JSL $80CE9A actually dispatch to CE9A, and with what
inputs? Arm block_watch at CE9A's entry block (0x00CE9A and the $80 mirror)
capturing $0000-$0007 (the staged eyeX/eyeY/tgtX/tgtY, untouched at entry), plus
$08:AEDD to confirm a real staging happened. Trigger a slot-0 launch.

If AEDD fires but neither CE9A block fires -> JSL $80CE9A is NOT dispatching to
CE9A (bank-$80 mirror dispatch bug) -> $0000 keeps the staged eyeX -> timer =
eyeX>>1. If a CE9A block fires, read its entry $0000-$0006 and stack.
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402
PORT = int(os.environ.get("DBG_PORT", "4379"))


def main():
    c = DebugClient(PORT)

    def q(cmd):
        return json.loads(c.query_raw(cmd))

    def arm():
        c.query_raw("block_watch_clear")
        q("block_watch_arm 08AEDD 0BAD,0BAE,0BB0,0BB1 16")   # slot 0: staging
        q("block_watch_arm 00CE9A 0,1,2,3,4,5,6,7 16")        # slot 1: CE9A entry
        q("block_watch_arm 80CE9A 0,1,2,3,4,5,6,7 16")        # slot 2: CE9A mirror

    arm()
    print(f"# loadstate 0: {q('loadstate 0')}", file=sys.stderr)
    c.query_raw("clear_controller")

    hit = None
    for i in range(140):
        q("step 30")
        s0 = q("block_watch_get 0").get("slots", [])
        if s0 and s0[0].get("hit_count", 0) > 0:
            hit = s0[0]
            break

    s0 = q("block_watch_get 0").get("slots", [])
    s1 = q("block_watch_get 1").get("slots", [])
    s2 = q("block_watch_get 2").get("slots", [])

    if not hit:
        print("# TRIGGER FAILURE: AEDD staging never fired this run.")
        return

    e = hit["events"][0]
    f0 = e["frame"]
    print(f"# AEDD staging fired: frame={f0} D=0x{int(e['D'],16):04x} "
          f"hit_count={hit['hit_count']}")

    c0 = s1[0]["hit_count"] if s1 else 0
    c8 = s2[0]["hit_count"] if s2 else 0
    print(f"\n# CE9A block fired?  0x00CE9A hit_count={c0}   "
          f"0x80CE9A hit_count={c8}")

    if c0 == 0 and c8 == 0:
        print("\n# >>> ROOT CAUSE: JSL $80CE9A NEVER dispatched to CE9A.")
        print("#     -> $0000 keeps the staged eyeX, timer = eyeX>>1. The")
        print("#        recompiled call into the bank-$80 ROM mirror is broken.")
        return

    for tag, sl in (("0x00CE9A", s1), ("0x80CE9A", s2)):
        if not sl or sl[0]["hit_count"] == 0:
            continue
        print(f"\n# CE9A entries via {tag}:")
        for ev in sl[0]["events"]:
            # only the one near the staging frame is the eye's
            if abs(ev["frame"] - f0) > 3:
                continue
            v = [int(x, 16) for x in ev["vals"]]
            eyeX = v[0] | v[1] << 8
            eyeY = v[2] | v[3] << 8
            tgtX = v[4] | v[5] << 8
            tgtY = v[6] | v[7] << 8
            print(f"  f{ev['frame']} D=0x{int(ev['D'],16):04x} "
                  f"m={ev['m']} x={ev['x']}")
            print(f"    eyeX $0000={eyeX}  eyeY $0002={eyeY}  "
                  f"tgtX $0004={tgtX}  tgtY $0006={tgtY}  |dx|={abs(eyeX-tgtX)}")
            print(f"    stack={ev.get('stack')}")


if __name__ == "__main__":
    main()
