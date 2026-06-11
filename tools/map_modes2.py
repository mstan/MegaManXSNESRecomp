#!/usr/bin/env python3
"""Phase-mapping driver v2 — screenshot-verified.

Start this with the game sitting ON THE TITLE SCREEN. Drives
title -> menu -> intro stage -> pause -> unpause, saving a screenshot per
phase (tools/_mm_<phase>.bmp in the run dir) so each label is verifiable,
and 3 live WRAM snapshots per phase. Then reports discriminator bytes.
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

    def tap(buttons, hold=0.25):
        c.query_raw(f'set_controller {buttons}')
        time.sleep(hold)
        c.query_raw('clear_controller')

    snaps = []

    def snap(phase, count=3, gap=1.0):
        c.query_raw(f'screenshot _mm_{phase}.bmp')
        fr = 0
        for _ in range(count):
            fr = json.loads(c.query_raw('ping'))['frame']
            r = json.loads(c.query_raw(f'dump_ram 0 {LEN:x}'))
            data = bytes.fromhex(r['hex'] if 'hex' in r else r['data'])
            if len(data) == LEN:
                snaps.append((phase, fr, data))
            time.sleep(gap)
        print(f'  [{phase}] sampled around frame {fr}', file=sys.stderr)

    snap('title')

    tap('start')          # title -> mode menu
    time.sleep(2.5)
    snap('menu')

    tap('start')          # GAME START -> intro stage
    print('stage loading...', file=sys.stderr)
    time.sleep(10)
    snap('stage_idle')

    c.query_raw('set_controller right')
    time.sleep(2)
    snap('stage_walk', count=2)
    c.query_raw('clear_controller')
    time.sleep(0.5)

    tap('start')          # pause menu
    time.sleep(2.5)
    snap('pause_menu')

    tap('start')          # unpause
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
            if len(vals) != 1:
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
        clean = len(stage_vals) == 1 and not (stage_vals & other_vals)
        tag = ' <== SEPARATES STAGE' if clean else ''
        if len(distinct) >= 3 or clean:
            desc = ' '.join(f'{p}={v:02X}' for p, v in by_phase.items())
            print(f'${i:04X}: {desc}{tag}')


if __name__ == '__main__':
    main()
