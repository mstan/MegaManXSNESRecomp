#!/usr/bin/env python3
"""Catch the REAL blue-eye launch staging and build the value table.

block_watch only matches BLOCK-ENTRY PCs. $08:AF0E (the JSL CE9A) is mid-block,
so it can't be watched directly. The two staging-only block entries are:
  $08:AEDD  -- BPL fall-through (reached ONLY when D+$0F is negative = staging)
              => gives us D (eye struct base) + frame, confirms staging happened.
  $08:AF12  -- return address after `JSL $80CE9A`; at entry $0000 = CE9A distance.

Strategy: arm both, trigger a launch (loadstate 0 = RNG pre-rolled blue eye,
fallback loadstate 1 + dash), poll. On a staging hit, query the always-on WRAM
ring for every relevant D-relative field around the staging frame and print a
recomp value table (eyeX accumulator D+$05, level mirror D+$22, screen mirror
D+$24, target, distance, timer). Entirely in one script so the ring won't evict.

Usage: python tools/eye_staging.py
Writes tools/_staging.json.
"""
import os, sys, json, math
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))
AEDD = 0x08AEDD
AF12 = 0x08AF12


def arm(c):
    c.query_raw("block_watch_clear")
    # AEDD: capture targets (absolute) + dummy; D comes from registers.
    c.query_raw(f"block_watch_arm {AEDD:06x} 0BAD,0BAE,0BB0,0BB1 16")
    # AF12: capture $0000-$0007 ($0000/$0001 = distance result after CE9A).
    c.query_raw(f"block_watch_arm {AF12:06x} 0,1,2,3,4,5,6,7 16")


def get(c, slot):
    return json.loads(c.query_raw(f"block_watch_get {slot}")).get("slots", [])


def ring(c, a, flo, fhi):
    r = json.loads(c.query_raw(f"wram_writes_at {a:x} {flo} {fhi} 300"))
    return r.get("matches", [])


def main():
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    def q(cmd):
        return json.loads(c.query_raw(cmd))

    def drive(slot, dash):
        arm(c)
        print(f"# loadstate {slot}: {q(f'loadstate {slot}')}", file=sys.stderr)
        if dash:
            c.query_raw("set_controller a")
            c.query_raw("step 60")
            c.query_raw("clear_controller")
        else:
            c.query_raw("clear_controller")
        for i in range(140):
            q("step 30")
            sa = get(c, 0)  # AEDD slot
            if sa and sa[0].get("hit_count", 0) > 0:
                return sa[0], get(c, 1)
        return None, get(c, 1)

    # Trigger: prefer slot 0 (pre-rolled). Fall back to slot 1 + dash.
    aedd, af12 = drive(0, dash=False)
    if not aedd:
        print("# slot 0 did not reach AEDD staging; trying slot 1 + dash",
              file=sys.stderr)
        aedd, af12 = drive(1, dash=True)

    if not aedd:
        print("# TRIGGER FAILURE: AEDD (staging) never fired in either state.")
        print("# Diagnostic — arming AED9 + checking whether staging is ever "
              "eligible (D+$0F sign):")
        c.query_raw("block_watch_clear")
        q(f"block_watch_arm 08AED9 0BAD,0BAE 16")
        q("loadstate 0"); c.query_raw("clear_controller")
        for _ in range(60):
            q("step 30")
        w = get(c, 0)
        if w:
            print(f"#  AED9 hit_count={w[0]['hit_count']}; sample events:")
            for e in w[0].get("events", [])[:4]:
                print(f"#   f{e['frame']} D=0x{int(e['D'],16):04x}")
        else:
            print("#  AED9 never fired either -> eye AI not running / wrong state.")
        return

    ev = aedd["events"][0]
    D = int(ev["D"], 16)
    f0 = ev["frame"]
    print(f"\n# === STAGING CAUGHT === frame={f0}  D=0x{D:04x}  "
          f"A={ev['A']} X={ev['X']} Y={ev['Y']} P={ev['P']} m={ev['m']} x={ev['x']}")
    print(f"#   AEDD stack: {ev.get('stack')}")
    print(f"#   targets at AEDD [$0BAD,$0BAE,$0BB0,$0BB1] = {ev['vals']}")

    # Distance from AF12 (same staging frame).
    dist = None
    if af12 and af12[0].get("hit_count", 0) > 0:
        # pick the AF12 event nearest f0
        evs = af12[0]["events"]
        e = min(evs, key=lambda x: abs(x["frame"] - f0))
        dv = [int(x, 16) for x in e["vals"]]
        dist = dv[0] | dv[1] << 8
        print(f"#   AF12 (post-CE9A) frame={e['frame']} $0000(dist)={dist} "
              f"leftover $0002..$0006={dv[2:]}")

    flo, fhi = f0 - 90, f0 + 30
    fields = [
        ("D+$03 substate", D + 0x03, 1),
        ("D+$04 Xacc.lo ", D + 0x04, 2),
        ("D+$05 Xacc int", D + 0x05, 2),   # <- AED9 reads this as eyeX
        ("D+$06 Xacc.hi ", D + 0x06, 1),
        ("D+$07 Yacc.lo ", D + 0x07, 2),
        ("D+$08 Yacc int", D + 0x08, 2),   # <- AED9 reads this as eyeY
        ("D+$09 Yacc.hi ", D + 0x09, 1),
        ("D+$0F gate    ", D + 0x0F, 1),
        ("D+$1A Xvel    ", D + 0x1A, 2),
        ("D+$1C Yvel    ", D + 0x1C, 2),
        ("D+$22 levelX  ", D + 0x22, 2),
        ("D+$24 screenX ", D + 0x24, 2),
        ("D+$34 timer   ", D + 0x34, 2),
        ("D+$36 timer2  ", D + 0x36, 2),
    ]
    print(f"\n# ring writes [{flo}..{fhi}] for D-relative fields:")
    snapshot = {}
    for label, a, w in fields:
        ms = ring(c, a, flo, fhi)
        # value just before/at the staging frame
        atf = [m for m in ms if m["f"] <= f0]
        cur = atf[-1]["val"] if atf else (ms[0]["old"] if ms else None)
        snapshot[label] = cur
        chg = [m for m in ms if m["old"] != m["val"]]
        seg = [f"f{m['f']}:{m['old']}->{m['val']}[{m['func']}]" for m in chg]
        cs = f"  cur@stage={cur}" if cur is not None else "  (no writes in window)"
        print(f"  {label} ${a:05x}:{cs}")
        if seg:
            print(f"      changes: " + "  ".join(seg[:12]) +
                  (" ..." if len(seg) > 12 else ""))

    # Decision summary.
    print("\n# ===== VALUE TABLE =====")
    tgtX = int(ev['vals'][0], 16) | int(ev['vals'][1], 16) << 8
    tgtY = int(ev['vals'][2], 16) | int(ev['vals'][3], 16) << 8
    eyeX = snapshot.get("D+$05 Xacc int")
    eyeY = snapshot.get("D+$08 Yacc int")
    lvlX = snapshot.get("D+$22 levelX  ")
    scrX = snapshot.get("D+$24 screenX ")
    print(f"  tgtX ($0BAD)            = {tgtX}")
    print(f"  tgtY ($0BB0)            = {tgtY}")
    print(f"  eyeX (D+$05, fed CE9A)  = {eyeX}")
    print(f"  eyeY (D+$08, fed CE9A)  = {eyeY}")
    print(f"  levelX mirror (D+$22)   = {lvlX}")
    print(f"  screenX mirror (D+$24)  = {scrX}")
    print(f"  CE9A distance ($0000)   = {dist}")
    print(f"  timer (D+$34)           = {snapshot.get('D+$34 timer   ')}")
    if eyeX is not None:
        dx = abs(eyeX - tgtX)
        print(f"  |eyeX - tgtX|           = {dx}")
        if dx > 2000:
            print(f"  >>> CASE A: eyeX is in WRONG space (huge dx). "
                  f"eyeX={eyeX} vs levelX={lvlX} screenX={scrX}.")
        elif dx < 400:
            print(f"  >>> CASE B: eyeX consistent with target (small dx).")
        else:
            print(f"  >>> CASE C: ambiguous; inspect table.")

    json.dump({"D": D, "frame": f0, "tgtX": tgtX, "tgtY": tgtY,
               "dist": dist, "snapshot": snapshot, "aedd": ev,
               "af12": af12}, open(os.path.join(HERE, "_staging.json"), "w"))


if __name__ == "__main__":
    main()
