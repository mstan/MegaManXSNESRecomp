#!/usr/bin/env python3
"""Record-first phase mapper: no in-band phase assumptions.

Every 2s for ~3min: screenshot (_tl_<n>.bmp in run dir) + page-0 WRAM dump,
recorded as a timeline. Injects exactly two Start taps early on (exit
attract, select GAME START) and otherwise leaves the game alone — if the
game start lands, the tail of the recording is real in-stage gameplay.
Timeline saved to tools/_timeline.json; phases are labeled OFFLINE from the
screenshots and diffed by tools/diff_timeline.py.
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
SAMPLES = 90
GAP = 2.0


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')

    def tap(buttons, hold=0.25):
        c.query_raw(f'set_controller {buttons}')
        time.sleep(hold)
        c.query_raw('clear_controller')

    timeline = []
    for n in range(SAMPLES):
        fr = json.loads(c.query_raw('ping'))['frame']
        c.query_raw(f'screenshot _tl_{n:03d}.bmp')
        r = json.loads(c.query_raw(f'dump_ram 0 {LEN:x}'))
        data = r['hex'] if 'hex' in r else r['data']
        timeline.append({'n': n, 'frame': fr, 'page0': data})
        if n == 6 or n == 9:           # ~12s and ~18s in
            tap('start')
            print(f'  tapped Start at sample {n} (frame {fr})', file=sys.stderr)
        # Hold right for a stretch late in the recording so, if we are
        # in-stage by then, the camera scrolls (stream + walk variation).
        if n == 55:
            c.query_raw('set_controller right')
        if n == 65:
            c.query_raw('clear_controller')
        time.sleep(GAP)

    with open(os.path.join(HERE, '_timeline.json'), 'w') as f:
        json.dump(timeline, f)
    print(f'recorded {len(timeline)} samples -> tools/_timeline.json')


if __name__ == '__main__':
    main()
