#!/usr/bin/env python3
"""Diff phase-labeled timeline samples for mode-discriminator bytes.

Usage:
  python tools/diff_timeline.py title=0-5 menu=8-12 stage=40-80 ...

Each label=lo-hi names an inclusive SAMPLE-INDEX range from
tools/_timeline.json (record_timeline.py output). Prints bytes stable
within every labeled phase whose 'stage'-labeled value is unique, plus
all >=3-value bytes for context. Ranges may also be a single index (n).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    with open(os.path.join(HERE, '_timeline.json')) as f:
        timeline = json.load(f)
    by_n = {e['n']: bytes.fromhex(e['page0']) for e in timeline}

    phases = {}
    for arg in sys.argv[1:]:
        label, _, rng = arg.partition('=')
        lo, _, hi = rng.partition('-')
        lo = int(lo)
        hi = int(hi) if hi else lo
        phases[label] = [by_n[n] for n in range(lo, hi + 1) if n in by_n]

    # Merge the screenshot-verified live player-stage phases.
    sp = os.path.join(HERE, '_stage_phases.json')
    if os.path.isfile(sp):
        with open(sp) as f:
            for label, hexes in json.load(f).items():
                phases[label] = [bytes.fromhex(h) for h in hexes]
    for label, snaps in phases.items():
        print(f'# {label}: {len(snaps)} samples', file=sys.stderr)

    length = min(len(s) for snaps in phases.values() for s in snaps)
    stagey = [p for p in phases if p.startswith('stage')]

    for i in range(length):
        by_phase = {}
        ok = True
        for p, snaps in phases.items():
            vals = {s[i] for s in snaps}
            if len(vals) != 1:
                ok = False
                break
            by_phase[p] = vals.pop()
        if not ok or len(set(by_phase.values())) < 2:
            continue
        stage_vals = {v for p, v in by_phase.items() if p in stagey}
        other_vals = {v for p, v in by_phase.items() if p not in stagey}
        clean = stagey and len(stage_vals) == 1 and not (stage_vals & other_vals)
        if clean or len(set(by_phase.values())) >= 3:
            desc = ' '.join(f'{p}={v:02X}' for p, v in by_phase.items())
            tag = ' <== SEPARATES STAGE' if clean else ''
            print(f'${i:04X}: {desc}{tag}')


if __name__ == '__main__':
    main()
