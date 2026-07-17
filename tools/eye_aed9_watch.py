#!/usr/bin/env python3
"""Deterministic blue-eye launch capture, slot-independent.

Anchors on the eye-launch handler's BLOCK PC ($08:AED9) instead of guessing the
non-deterministic object slot. block_watch_arm captures registers (incl. the
direct-page register D = object base) + call stack on every entry to AED9 (rare;
only fires at an eye launch). Once we have D for THIS launch we query the
always-on WRAM ring for the eye's real fields and read old->val->func across the
launch window:
  X pos word [D+0x04] (+ hi byte [D+0x06]),  X vel [D+0x1a],  fly-timer [D+0x34],
  Y pos word [D+0x07].
This shows the +0x400 position discontinuity WITH attribution, and the exact
fly-timer AED9 computed -- the decisive measurement from ISSUES.md.

Usage: python tools/eye_aed9_watch.py
"""
import os, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))
AED9_PC = 0x08AED9


def main():
    c = DebugClient(PORT, name=f"dbg:{PORT}")

    def q(cmd):
        return json.loads(c.query_raw(cmd))

    # Arm the launch-handler watch (capture regs + a dummy ram byte: requires >=1).
    c.query_raw("block_watch_clear")
    r = q(f"block_watch_arm {AED9_PC:06x} 0BAD,0BAE,0BB0,0BB1 8")
    print(f"# armed AED9 watch: {r}", file=sys.stderr)

    # Drive into the fight and let the eye launch.
    print(f"# loadstate: {q('loadstate 1')}", file=sys.stderr)
    c.query_raw("set_controller a")
    c.query_raw("step 60")
    c.query_raw("clear_controller")

    hit = None
    for i in range(80):
        q("step 12")
        w = q("block_watch_get 0")
        slots = w.get("slots", [])
        if slots and slots[0].get("hit_count", 0) > 0:
            hit = slots[0]
            break
    if not hit:
        print("# AED9 never fired -- wrong PC or no launch. Trying a block "
              "trace to find the eye-AI PC range.")
        bt = c.query_raw("get_block_trace pc_lo=08AE00 pc_hi=08B000 idx_lim=40")
        print(bt[:3000])
        return

    print(f"# AED9 fired: hit_count={hit['hit_count']}")
    ev = hit["events"][0]
    D = int(ev["D"], 16)
    f_launch = ev["frame"]
    print(f"# launch event: frame={f_launch} D=0x{D:04x} "
          f"A={ev['A']} X={ev['X']} Y={ev['Y']} m={ev['m']} x={ev['x']}")
    print(f"#   stack: {ev.get('stack')}")
    print(f"#   captured [$0BAD,$0BAE,$0BB0,$0BB1] = {ev['vals']}")
    for e in hit["events"]:
        print(f"#   hit f{e['frame']} D=0x{int(e['D'],16):04x} "
              f"stack[0:3]={e.get('stack', [])[:3]}")

    # Now read the eye struct from the ring around the launch.
    flo, fhi = f_launch - 40, f_launch + 10
    fields = {
        "Xlo  [D+04]": D + 0x04,
        "Xmid [D+05]": D + 0x05,
        "Xhi  [D+06]": D + 0x06,
        "Ylo  [D+07]": D + 0x07,
        "vel  [D+1a]": D + 0x1a,
        "velhi[D+1b]": D + 0x1b,
        "timer[D+34]": D + 0x34,
        "subst[D+03]": D + 0x03,
    }
    print(f"\n# ring writes [{flo}..{fhi}] for eye struct D=0x{D:04x}:")
    for label, a in fields.items():
        r = q(f"wram_writes_at {a:x} {flo} {fhi} 200")
        ms = r.get("matches", [])
        chg = [m for m in ms if m["old"] != m["val"]]
        seg = [f"f{m['f']}:{m['old']}->{m['val']}[{m['func']}]" for m in chg]
        print(f"  {label} ${a:05x}: " +
              ("  ".join(seg[:14]) + (" ..." if len(seg) > 14 else "")
               if seg else "(no value changes)"))

    # Targets (stable).
    tg = bytes.fromhex(q("dump_ram bad 4")["hex"])
    bb = bytes.fromhex(q("dump_ram bb0 2")["hex"])
    print(f"\n# targets: $0BAD={tg[0]|tg[1]<<8}  $0BB0={bb[0]|bb[1]<<8}")
    print("# (compare: fly-timer*2 should equal CE9A(eyeX=[D+05] word, "
          "eyeY=[D+08] word, tgtX=$0BAD, tgtY=$0BB0))")


if __name__ == "__main__":
    main()
