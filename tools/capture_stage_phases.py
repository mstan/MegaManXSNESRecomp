#!/usr/bin/env python3
"""Capture page-0 WRAM phases from a LIVE player-controlled stage.

Run with the game sitting in the highway stage (player control verified).
Appends labeled samples to tools/_timeline.json under negative indices...
no — saves to its own tools/_stage_phases.json: {label: [hex, ...]},
plus _sp_<label>.bmp screenshots for verification.
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

    out = {}

    def snap(label, count=3, gap=0.8):
        c.query_raw(f'screenshot _sp_{label}.bmp')
        vals = []
        for _ in range(count):
            r = json.loads(c.query_raw(f'dump_ram 0 {LEN:x}'))
            vals.append(r['hex'] if 'hex' in r else r['data'])
            time.sleep(gap)
        out[label] = vals
        print(f'  [{label}] {count} samples', file=sys.stderr)

    snap('stage_idle')

    c.query_raw('set_controller right')
    time.sleep(1.5)
    snap('stage_walk', count=2)
    c.query_raw('clear_controller')
    time.sleep(0.5)

    tap('start')
    time.sleep(2)
    snap('stage_pause')

    tap('start')
    time.sleep(2)
    snap('stage_unpaused', count=2)

    with open(os.path.join(HERE, '_stage_phases.json'), 'w') as f:
        json.dump(out, f)
    print('saved tools/_stage_phases.json')


if __name__ == '__main__':
    main()
