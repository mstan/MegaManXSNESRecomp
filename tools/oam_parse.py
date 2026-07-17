#!/usr/bin/env python3
"""Parse SNES OAM from the debug server's dump_oam (or a saved hex string).

SNES OAM: 512-byte low table (128 sprites x 4 bytes) + 32-byte high table
(2 bits/sprite: bit0 = X bit8, bit1 = size-select).

byte0 = X low 8 bits
byte1 = Y
byte2 = tile number low 8 bits
byte3 = vhoo pppN  (v=vflip h=hflip oo=prio ppp=palette N=tile bit8)

Usage:
  python tools/oam_parse.py                 # query live (port 4379), on-screen only
  python tools/oam_parse.py --all           # include off-screen / hidden
  python tools/oam_parse.py --hex <hexstr>  # parse a saved hex string
  python tools/oam_parse.py --save out.json # also save raw {len,hex}
Env: DBG_PORT (default 4379)
"""
import sys, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))

PORT = int(os.environ.get('DBG_PORT', '4379'))


def get_hex():
    args = sys.argv[1:]
    if '--hex' in args:
        return args[args.index('--hex') + 1]
    from sneslib.client import DebugClient
    c = DebugClient(PORT, name=f'dbg:{PORT}')
    resp = c.query('dump_oam')
    if '--save' in args:
        json.dump(resp, open(args[args.index('--save') + 1], 'w'))
    return resp['hex']


def parse(hexstr):
    b = bytes.fromhex(hexstr)
    low, high = b[:512], b[512:544]
    sprites = []
    for i in range(128):
        x = low[i * 4 + 0]
        y = low[i * 4 + 1]
        tile = low[i * 4 + 2]
        attr = low[i * 4 + 3]
        hb = (high[i // 4] >> ((i % 4) * 2)) & 0x3
        x9 = x | (0x100 if (hb & 1) else 0)
        size_large = bool(hb & 2)
        sprites.append({
            'i': i, 'x': x9, 'y': y, 'tile': tile | ((attr & 1) << 8),
            'pal': (attr >> 1) & 7, 'prio': (attr >> 4) & 3,
            'hflip': bool(attr & 0x40), 'vflip': bool(attr & 0x80),
            'large': size_large,
        })
    return sprites


def onscreen(s):
    # Y in [0,223] visible; 224..255 typically parked off the bottom.
    return s['y'] < 224 and s['x'] < 256


def main():
    show_all = '--all' in sys.argv
    sprites = parse(get_hex())
    shown = sprites if show_all else [s for s in sprites if onscreen(s)]
    print(f"{len(shown)} sprites" + ("" if show_all else " on-screen")
          + f" (of 128)")
    print(f"{'#':>3} {'x':>4} {'y':>4} {'tile':>5} pal pri fl sz")
    for s in sorted(shown, key=lambda s: (s['tile'], s['x'])):
        fl = ('H' if s['hflip'] else '-') + ('V' if s['vflip'] else '-')
        print(f"{s['i']:>3} {s['x']:>4} {s['y']:>4} 0x{s['tile']:03x} "
              f"{s['pal']:>3} {s['prio']:>3} {fl} {'L' if s['large'] else 's'}")


if __name__ == '__main__':
    main()
