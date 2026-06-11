#!/usr/bin/env python3
"""Find bytes that cleanly separate the pause menu from live stage play,
using tools/_stage_phases.json (stage_idle/walk/pause/unpaused)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

with open(os.path.join(HERE, '_stage_phases.json')) as f:
    phases = {k: [bytes.fromhex(h) for h in v] for k, v in json.load(f).items()}

play = ['stage_idle', 'stage_walk', 'stage_unpaused']
length = min(len(s) for v in phases.values() for s in v)

for i in range(length):
    vals = {}
    ok = True
    for p, snaps in phases.items():
        vs = {s[i] for s in snaps}
        if len(vs) != 1:
            ok = False
            break
        vals[p] = vs.pop()
    if not ok:
        continue
    pv = {vals[p] for p in play}
    if len(pv) == 1 and vals['stage_pause'] not in pv:
        print(f'${i:04X}: play={pv.pop():02X} pause={vals["stage_pause"]:02X}')
