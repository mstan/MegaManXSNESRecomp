#!/usr/bin/env python3
"""Summarize each slot's lifecycle from a sprite_cap jsonl.

For each slot: list contiguous "active" spans (st != '00') with their
frame range, object id(s) n, position range, and the graphics fields.
Helps spot a stationary, persistent enemy (the turtle).

Usage: python tools/sprite_life.py <cap.jsonl> [slot]
"""
import sys, json

path = sys.argv[1]
only = int(sys.argv[2]) if len(sys.argv) > 2 else None
GFX = ('bl', 'sl', 'dir', 't1540', 't1558', 'm1fe2', 'noobj')

for line in open(path, encoding='utf-8-sig'):
    line = line.strip()
    if not line:
        continue
    rec = json.loads(line)
    slot = rec['slot']
    if only is not None and slot != only:
        continue
    ents = rec['entries']
    if not ents:
        continue
    print(f"\n===== slot {slot}  ({len(ents)} change-entries) =====")
    for e in ents:
        xs = e['x']
        ys = e['y']
        g = ' '.join(f"{k}={e[k]}" for k in GFX)
        print(f"  f{e['f']}: st={e['st']} n={e['n']} x={xs} y={ys} "
              f"xs={e['xs']} ys={e['ys']} | {g}")
