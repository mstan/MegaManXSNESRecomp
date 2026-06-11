#!/usr/bin/env python3
"""Measure MMX's BG column streaming during the attract demo (no input).

Free-runs; polls until the demo stage starts scrolling (hScroll moves),
then arms the VRAM write trace on the active BG1/BG2 tilemaps and samples
(frame, hScroll) for a while. Afterwards correlates each tilemap write's
map column with the camera column at that frame, histogramming the offset
from the visible right edge — i.e., how far ahead the game streams columns.

Also snapshots page-0 WRAM at title/story/demo phases for mode mapping.
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


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')

    def ppu():
        return json.loads(c.query_raw('get_ppu_state'))

    def wram_page0():
        r = json.loads(c.query_raw('dump_ram 0 400'))
        return bytes.fromhex(r['hex'] if 'hex' in r else r['data'])

    # ---- phase 1: wait for the demo stage (hScroll starts moving) -------
    print('waiting for attract demo stage (hScroll motion)...', file=sys.stderr)
    page0 = {}
    last_h = None
    moving = 0
    deadline = time.time() + 150
    while time.time() < deadline:
        st = ppu()
        h = st['hScroll'][0]
        fr = json.loads(c.query_raw('ping'))['frame']
        if last_h is not None and h != last_h:
            moving += 1
        else:
            moving = 0
        last_h = h
        if moving >= 2:
            print(f'  demo scrolling at frame {fr}, hScroll={h}', file=sys.stderr)
            break
        time.sleep(2)
    else:
        print('TIMEOUT: demo never scrolled', file=sys.stderr)
        return 1

    page0['demo_stage'] = wram_page0()
    st = ppu()
    bgxsc = [int(x, 16) for x in st['bgXsc']]
    print(f'  in-demo bgXsc={st["bgXsc"]} bgmode={st["bgmode"]} '
          f'screenEnabled={st["screenEnabled"]}', file=sys.stderr)

    # Active BG1 + BG2 tilemap byte ranges from $2107/$2108.
    ranges = []
    for bg in (0, 1):
        base_words = ((bgxsc[bg] >> 2) & 0x3F) << 10
        size = bgxsc[bg] & 3
        nwords = 0x400 * (2 if size in (1, 2) else 4 if size == 3 else 1)
        lo = base_words * 2
        hi = lo + nwords * 2 - 1
        ranges.append((bg, lo, hi, size))
        print(json.loads(c.query_raw('trace_vram_reset')) if bg == 0 else '',
              file=sys.stderr)
        print(c.query_raw(f'trace_vram {lo:x} {hi:x}'), file=sys.stderr)

    # ---- phase 2: sample camera while the ring fills --------------------
    cam = []  # (frame, hscroll)
    for _ in range(40):
        fr = json.loads(c.query_raw('ping'))['frame']
        st = ppu()
        cam.append((fr, st['hScroll'][0]))
        time.sleep(0.4)
    print(f'  sampled camera over frames {cam[0][0]}..{cam[-1][0]} '
          f'h {cam[0][1]}..{cam[-1][1]}', file=sys.stderr)

    # ---- phase 3: read the ring and correlate ---------------------------
    raw = c.query_raw('get_vram_trace nostack')
    tr = json.loads(raw)
    log = tr.get('log', [])
    print(f'  vram trace entries: {tr.get("entries")} (returned {len(log)})',
          file=sys.stderr)

    def cam_at(frame):
        best = None
        for fr, h in cam:
            if best is None or abs(fr - frame) < abs(best[0] - frame):
                best = (fr, h)
        return best[1] if best and abs(best[0] - frame) <= 40 else None

    offs = Counter()
    funcs = Counter()
    for e in log:
        f = e['f']
        h = cam_at(f)
        if h is None:
            continue
        adr = int(e['adr_byte'], 16)
        for bg, lo, hi, size in ranges:
            if lo <= adr <= hi:
                word = (adr - lo) // 2
                # 64x32: two side-by-side 32x32 screens (0x000-3FF, 400-7FF)
                screen = (word >> 10) & 1
                col = (word & 0x1F) + 32 * screen
                campix = h & 0x1FF      # camera within the 512px map space
                camcol = (campix // 8) % 64
                # offset of written column from the camera's LEFT edge,
                # in the map's 64-col circular space
                d = (col - camcol) % 64
                offs[(bg, d)] += 1
                funcs[e.get('func', '?')] += 1
                break

    print('\n=== tilemap-write column offsets from camera left edge (cols) ===')
    print('(visible authentic window = cols 0..31; right margin > 32)')
    for (bg, d), n in sorted(offs.items()):
        print(f'BG{bg + 1} offset {d:+3d} cols ({d * 8:+4d}px): {n} writes')
    print('\n=== writer functions ===')
    for fn, n in funcs.most_common(10):
        print(f'{n:6d}  {fn}')

    # page-0 phase snapshot for the mode hunt
    out = {p: s.hex() for p, s in page0.items()}
    with open(os.path.join(HERE, '_demo_page0.json'), 'w') as f:
        json.dump(out, f)
    print('\npage-0 snapshots saved to tools/_demo_page0.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
