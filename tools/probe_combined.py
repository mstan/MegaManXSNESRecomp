#!/usr/bin/env python3
"""Combined unattended probe:
1. Wait for the attract demo stage; with-stack VRAM trace of the tilemap
   seam writes -> caller chains of the uploader (bank_00_82C8).
2. Exit demo (Start), drive title -> menu -> GAME START -> player stage ->
   pause -> unpause, screenshot-verifying each phase, sampling page-0 WRAM.
3. Report discriminator bytes incl. demo-vs-player split.
"""
import json
import os
import sys
import time
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
LEN = 0x400


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')

    def ppu():
        return json.loads(c.query_raw('get_ppu_state'))

    def frame():
        return json.loads(c.query_raw('ping'))['frame']

    def tap(buttons, hold=0.25):
        c.query_raw(f'set_controller {buttons}')
        time.sleep(hold)
        c.query_raw('clear_controller')

    snaps = []

    def snap(phase, count=3, gap=1.0):
        c.query_raw(f'screenshot _mm_{phase}.bmp')
        fr = 0
        for _ in range(count):
            fr = frame()
            r = json.loads(c.query_raw(f'dump_ram 0 {LEN:x}'))
            data = bytes.fromhex(r['hex'] if 'hex' in r else r['data'])
            if len(data) == LEN:
                snaps.append((phase, fr, data))
            time.sleep(gap)
        print(f'  [{phase}] sampled around frame {fr}', file=sys.stderr)

    # ---- 1: demo stage, with-stack seam trace ---------------------------
    print('waiting for demo stage...', file=sys.stderr)
    last_h, moving = None, 0
    deadline = time.time() + 180
    while time.time() < deadline:
        h = ppu()['hScroll'][0]
        if last_h is not None and h != last_h:
            moving += 1
        else:
            moving = 0
        last_h = h
        if moving >= 2:
            break
        time.sleep(2)
    else:
        print('TIMEOUT waiting for demo', file=sys.stderr)
        return 1
    print(f'  demo scrolling, h={last_h}', file=sys.stderr)
    snap('demo_stage')

    c.query_raw('trace_vram_reset')
    print(c.query_raw('trace_vram a000 bfff'), file=sys.stderr)
    time.sleep(6)
    tr = json.loads(c.query_raw('get_vram_trace'))
    log = tr.get('log', [])
    c.query_raw('trace_vram_reset')
    stacks = Counter()
    for e in log:
        key = e.get('stack') or e.get('func') or '?'
        if isinstance(key, list):
            key = ' <- '.join(key)
        stacks[key] += 1
    print('\n=== seam-write caller chains (demo scroll) ===')
    for s, n in stacks.most_common(12):
        print(f'{n:6d}  {s}')
    if log:
        print('\nsample entry:', json.dumps(log[0]))

    # ---- 2: drive out of demo into a real game --------------------------
    print('\nexiting demo -> title', file=sys.stderr)
    tap('start')
    time.sleep(4)
    snap('title')

    print('title -> menu', file=sys.stderr)
    tap('start')
    time.sleep(2.5)
    snap('menu')

    print('menu -> GAME START', file=sys.stderr)
    tap('start')
    print('stage loading...', file=sys.stderr)
    time.sleep(10)
    snap('stage_idle')

    c.query_raw('set_controller right')
    time.sleep(2)
    snap('stage_walk', count=2)
    c.query_raw('clear_controller')
    time.sleep(0.5)

    tap('start')
    time.sleep(2.5)
    snap('pause_menu')

    tap('start')
    time.sleep(2)
    snap('stage_after_pause', count=2)

    # ---- 3: discriminator analysis --------------------------------------
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
        if len(set(by_phase.values())) < 2:
            continue
        stage_vals = {v for p, v in by_phase.items() if p in stagey}
        other_vals = {v for p, v in by_phase.items() if p not in stagey}
        clean = len(stage_vals) == 1 and not (stage_vals & other_vals)
        demo_differs = clean and by_phase.get('demo_stage') not in stage_vals
        tag = ''
        if clean:
            tag = ' <== SEPARATES STAGE'
            tag += ' (+demo differs)' if demo_differs else ' (demo SAME as stage)'
        if clean or len(set(by_phase.values())) >= 4:
            desc = ' '.join(f'{p}={v:02X}' for p, v in by_phase.items())
            print(f'${i:04X}: {desc}{tag}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
