#!/usr/bin/env python3
"""Find MMX's screen/mode discriminator byte empirically.

Samples low WRAM ($0000-$01FF) from the always-on frame ring across the
whole session history (which spans boot -> intro -> title -> menus ->
stage select -> in-stage gameplay), then reports bytes that are STABLE
within long runs but DIFFER across runs — game-mode shaped signals.

Usage: python tools/find_mode_byte.py [stride] [len_hex]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))


def main():
    stride = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    length = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x200
    c = DebugClient(PORT, name=f'dbg:{PORT}')
    h = c.get_history()
    oldest, newest = h['oldest'], h['newest']
    print(f"# history {oldest}..{newest}, sampling every {stride}", file=sys.stderr)

    frames = list(range(oldest, newest + 1, stride))
    snaps = {}
    for f in frames:
        r = json.loads(c.query_raw(f'dump_frame_wram {f} 0 {length}'))
        if 'error' in r:
            continue
        snaps[f] = bytes.fromhex(r['hex'])
    fs = sorted(snaps)
    print(f"# got {len(fs)} snapshots", file=sys.stderr)

    # For each byte: count distinct values and how often it changes between
    # consecutive samples. Mode-byte shape: few distinct values (2..8),
    # very few transitions (changes only at screen boundaries).
    n = length
    cands = []
    for i in range(n):
        vals = [snaps[f][i] for f in fs]
        distinct = sorted(set(vals))
        trans = sum(1 for a, b in zip(vals, vals[1:]) if a != b)
        if 2 <= len(distinct) <= 8 and 1 <= trans <= 12:
            cands.append((trans, i, distinct, vals))

    cands.sort()
    for trans, i, distinct, vals in cands[:40]:
        # Render the run-length pattern: value@startframe..
        runs = []
        cur, start = vals[0], fs[0]
        for f, v in zip(fs[1:], vals[1:]):
            if v != cur:
                runs.append((cur, start, f))
                cur, start = v, f
        runs.append((cur, start, fs[-1]))
        pat = ' '.join(f'{v:02X}@{a}-{b}' for v, a, b in runs)
        print(f'${i:04X} trans={trans} vals={[f"{v:02X}" for v in distinct]} | {pat}')


if __name__ == '__main__':
    main()
