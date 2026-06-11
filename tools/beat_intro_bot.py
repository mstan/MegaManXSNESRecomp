#!/usr/bin/env python3
"""Dumb intro-stage bot: hold right, autofire buster, periodic jumps.

Goal: reach the post-intro stage-select screen so $00D1/$003A can be read
there (the gate-leak question). Logs state every ~10s; saves _bot_NNN.bmp
screenshots for offline verification. Exits when $00D1 leaves 0x02 and
stays changed for two checks (cutscene/stage-select reached) or on timeout.
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
MAX_SECONDS = 480


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')

    def ram(addr, n=1):
        r = json.loads(c.query_raw(f'read_ram {addr:x} {n}'))
        return bytes.fromhex(r['hex'])

    t0 = time.time()
    shot_n = 0
    last_log = 0.0
    changed_checks = 0
    fire = False
    jump_until = 0.0
    next_jump = time.time() + 1.0

    print('bot running...', file=sys.stderr)
    while time.time() - t0 < MAX_SECONDS:
        now = time.time()
        buttons = ['right']
        fire = not fire                    # ~7 Hz autofire on Y
        if fire:
            buttons.append('y')
        if now >= next_jump:
            jump_until = now + 0.35
            next_jump = now + 1.1
        if now < jump_until:
            buttons.append('b')
        c.query_raw('set_controller ' + '+'.join(buttons))

        if now - last_log >= 10.0:
            last_log = now
            d1 = ram(0xD1)[0]
            a3 = ram(0x3A)[0]
            fr = json.loads(c.query_raw('ping'))['frame']
            c.query_raw(f'screenshot _bot_{shot_n:03d}.bmp')
            print(f'  t={int(now - t0):3d}s frame={fr} $00D1={d1:02X} $003A={a3:02X} shot=_bot_{shot_n:03d}',
                  file=sys.stderr)
            shot_n += 1
            if d1 != 0x02:
                changed_checks += 1
                if changed_checks >= 2:
                    print('  $00D1 left 02 (stable) — stopping bot', file=sys.stderr)
                    break
            else:
                changed_checks = 0
        time.sleep(0.15)

    c.query_raw('clear_controller')
    # Final state capture.
    time.sleep(1)
    d1 = ram(0xD1)[0]
    r = json.loads(c.query_raw('dump_ram 0 400'))
    c.query_raw('screenshot _bot_final.bmp')
    with open(os.path.join(HERE, '_bot_final_page0.json'), 'w') as f:
        json.dump({'d1': d1, 'page0': r['hex'] if 'hex' in r else r['data']}, f)
    print(f'final $00D1={d1:02X}; page0 saved; _bot_final.bmp written')


if __name__ == '__main__':
    main()
