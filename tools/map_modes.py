#!/usr/bin/env python3
"""Drive MMX through its screen phases and find the mode discriminator.

Free-running the whole time (RULE 0: no pause/step) — input is injected via
set_controller and low WRAM ($0000-$03FF) is live-snapshotted (3x per phase,
1s apart) at each labeled phase. Afterwards, reports bytes that are stable
within every phase but split the phases into >= N groups — game-mode shaped.

Run with the game freshly launched (sitting in the boot logos):
  python tools/map_modes.py
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
LEN = 0x400


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')

    def tap(buttons, hold=0.15):
        c.query_raw(f'set_controller {buttons}')
        time.sleep(hold)
        c.query_raw('clear_controller')
        time.sleep(0.3)

    snaps = []  # (phase, frame, bytes)

    def snap(phase, count=3, gap=1.0):
        for _ in range(count):
            fr = json.loads(c.query_raw('ping'))['frame']
            r = json.loads(c.query_raw(f'dump_ram 0 {LEN:x}'))
            hexkey = 'hex' if 'hex' in r else 'data'
            snaps.append((phase, fr, bytes.fromhex(r[hexkey])))
            time.sleep(gap)
        print(f'  [{phase}] sampled around frame {fr}', file=sys.stderr)

    print('phase: boot logos', file=sys.stderr)
    snap('logos')

    print('waiting for title (~12s from boot)...', file=sys.stderr)
    time.sleep(12)
    snap('title')

    print('Start -> main menu', file=sys.stderr)
    tap('start')
    time.sleep(2)
    snap('menu')

    print('Start on GAME START -> intro stage', file=sys.stderr)
    tap('start')
    print('waiting for stage load + fade-in (~8s)...', file=sys.stderr)
    time.sleep(8)
    snap('stage_idle')

    print('walking right in stage', file=sys.stderr)
    c.query_raw('set_controller right')
    time.sleep(2)
    snap('stage_walk', count=2)
    c.query_raw('clear_controller')

    print('Start -> pause menu', file=sys.stderr)
    time.sleep(0.5)
    tap('start')
    time.sleep(2)
    snap('pause_menu')

    print('Start -> unpause', file=sys.stderr)
    tap('start')
    time.sleep(2)
    snap('stage_after_pause', count=2)

    # --- analysis -------------------------------------------------------
    phases = []
    for p, _, _ in snaps:
        if p not in phases:
            phases.append(p)
    stagey = {'stage_idle', 'stage_walk', 'stage_after_pause'}

    print('\n=== candidate discriminator bytes ===')
    for i in range(LEN):
        by_phase = {}
        ok = True
        for p in phases:
            vals = {s[i] for (ph, _, s) in snaps if ph == p}
            if len(vals) != 1:        # must be rock-stable within a phase
                ok = False
                break
            by_phase[p] = vals.pop()
        if not ok:
            continue
        distinct = set(by_phase.values())
        if len(distinct) < 2:
            continue
        stage_vals = {v for p, v in by_phase.items() if p in stagey}
        other_vals = {v for p, v in by_phase.items() if p not in stagey}
        # The byte must give stage frames a value no other phase has.
        clean = len(stage_vals) >= 1 and not (stage_vals & other_vals)
        tag = ' <== SEPARATES STAGE' if clean and len(stage_vals) == 1 else ''
        if len(distinct) >= 3 or clean:
            desc = ' '.join(f'{p}={v:02X}' for p, v in by_phase.items())
            print(f'${i:04X}: {desc}{tag}')


if __name__ == '__main__':
    main()
