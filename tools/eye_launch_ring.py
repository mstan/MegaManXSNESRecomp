#!/usr/bin/env python3
"""Decisive Rangda Bangda blue-eye launch capture via the ALWAYS-ON ring.

Per the project ring-buffer rule: `step` is used ONLY as a control primitive
to drive the game into the fight (loadstate -> dash -> advance). All OBSERVATION
is done by querying the always-on WRAM write ring (`wram_writes_at`) for the
launch window AFTER the fact -- so we never "step over" the single launch frame
the way coarse dump_ram snapshots do.

Flow:
  1. loadstate 1 ; dash through the door ; advance ~N chunks, snapshotting the
     object region each chunk ONLY to locate the flying eye's marching word and
     the approximate launch-frame window.
  2. From the marching word, derive the eye struct base D.
  3. Query the ring (`wram_writes_at`) for every field D+0x00..D+0x40 over the
     launch window, printing old->val->func for each write. This reveals:
       - the +0x400 discontinuity in the eye position (which field, who wrote it)
       - the AED9 fly-timer write [D+0x34] (value = the computed timer)
       - the launch velocity write (0xFE28)
  4. Read $0BAD/$0BB0 (stable in this non-scrolling room) and compute CE9A on
     the position/target the ring shows, comparing to timer*2.

Usage: python tools/eye_launch_ring.py [chunk=12] [iters=70]
Saves tools/_launch_ring.json.
"""
import os, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))
OBJ_LO, OBJ_LEN = 0x0E00, 0x1200      # $0E00 .. $2000


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def s16(v):
    return v - 65536 if v > 32768 else v


def main():
    chunk = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    iters = int(sys.argv[2]) if len(sys.argv) > 2 else 70
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    def hx(cmd):
        return bytes.fromhex(json.loads(c.query_raw(cmd))["hex"])

    # Confirm the ring is alive / which ranges (do NOT arm, do NOT reset).
    raw = c.query_raw("get_wram_trace")
    import re
    m = re.search(r'"ranges":\[(.*?)\]', raw)
    print(f"# ring ranges: {m.group(1) if m else 'NONE'}", file=sys.stderr)

    # ---- control: drive into the fight ----
    print(json.dumps({"loadstate": json.loads(c.query_raw("loadstate 1"))}),
          file=sys.stderr)
    c.query_raw("set_controller a")
    c.query_raw("step 60")
    c.query_raw("clear_controller")

    # ---- locate the flying eye (snapshots only to FIND it, not to measure) ----
    snaps, frames = [], []
    for i in range(iters):
        st = json.loads(c.query_raw(f"step {chunk}"))
        frames.append(st.get("frame_after"))
        snaps.append(hx(f"dump_ram {OBJ_LO:x} {OBJ_LEN}"))

    with open(os.path.join(HERE, "_launch_ring.json"), "w") as fh:
        json.dump({"frames": frames, "snaps": [s.hex() for s in snaps]}, fh)

    N = len(snaps)
    print(f"# {N} snapshots, frames {frames[0]}..{frames[-1]}", file=sys.stderr)

    # Marching word: 16-bit word that is LEVEL-range (~4800-5500) early then
    # runs away monotonically (the malformed blue eye's X integer position).
    cands = []
    for o in range(0, OBJ_LEN - 1):
        vals = [u16(b, o) for b in snaps]
        early = vals[:max(3, N // 6)]
        if not any(4700 <= v <= 5500 for v in early):
            continue
        d = [s16(b - a) for a, b in zip(vals, vals[1:])]
        mv = [x for x in d if x]
        if len(mv) < 3:
            continue
        mono = max(sum(1 for x in mv if x > 0),
                   sum(1 for x in mv if x < 0)) / len(mv)
        span = max(vals) - min(vals)
        if mono >= 0.7 and span >= 100:
            cands.append((span * mono, OBJ_LO + o, vals, mono, span))
    cands.sort(reverse=True)

    if not cands:
        print("# NO runaway level-range word found -- no blue-eye launch in "
              "this window. Check _launch_ring.json / re-run.")
        return

    print("# runaway candidates (addr, mono, span, first->last):")
    for score, addr, vals, mono, span in cands[:5]:
        print(f"  ${addr:05x} mono={mono:.2f} span={span} "
              f"{vals[0]}->{vals[-1]}  traj={vals[::max(1,N//10)]}")

    _, axaddr, axvals, _, _ = cands[0]
    # Find the snapshot index where the runaway begins (launch window).
    launch_i = 0
    for i in range(1, N):
        if abs(s16(axvals[i] - axvals[i - 1])) > 40:
            launch_i = i
            break
    f_lo = frames[max(0, launch_i - 2)]
    f_hi = frames[min(N - 1, launch_i + 2)]
    print(f"\n# marching word ${axaddr:05x}; launch ~snapshot {launch_i} "
          f"frames [{f_lo}..{f_hi}]")

    # The integer X word AED9 reads is [D+0x05]; struct base ambiguous by 1.
    # Query the whole struct around the marching word over the launch window.
    base = axaddr - 0x05
    lo = base - 0x08
    hi = base + 0x40
    print(f"\n# ring writes over launch window [{f_lo-20}..{f_hi+20}] for "
          f"struct ${base:05x}..${hi:05x}:")
    fields = {}
    for a in range(lo, hi + 1):
        r = json.loads(c.query_raw(
            f"wram_writes_at {a:x} {f_lo-20} {f_hi+20} 200"))
        if r.get("matches"):
            fields[a] = r["matches"]

    for a in sorted(fields):
        ms = fields[a]
        off = a - base
        # collapse: show writes where val changes, with func
        line = f"  ${a:05x} (D+0x{off & 0xff:02x}): "
        seg = []
        for mm in ms:
            seg.append(f"f{mm['f']}:{mm['old']}->{mm['val']}[{mm['func']}]")
        print(line + "  ".join(seg[:12]) + (" ..." if len(seg) > 12 else ""))

    # ---- targets (stable in non-scrolling room) ----
    tg = hx("dump_ram bad 4")
    bad = u16(tg, 0)
    bb0 = u16(tg, 3) if len(tg) >= 5 else None
    tg2 = hx("dump_ram bb0 2")
    bb0 = u16(tg2, 0)
    print(f"\n# targets: $0BAD={bad} (0x{bad:04x})  $0BB0={bb0} (0x{bb0:04x})")


if __name__ == "__main__":
    main()
