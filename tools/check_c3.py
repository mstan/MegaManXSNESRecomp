import json
import os
HERE = os.path.dirname(os.path.abspath(__file__))
t = json.load(open(os.path.join(HERE, '_timeline.json')))
vals = {}
for e in t:
    b = bytes.fromhex(e['page0'])
    vals.setdefault(b[0xC3], []).append(e['n'])
for v, ns in sorted(vals.items()):
    print(f'$00C3={v:02X}: {len(ns)} samples, e.g. {ns[:12]}')
sp = json.load(open(os.path.join(HERE, '_stage_phases.json')))
for label, hexes in sp.items():
    print(label, [f'{bytes.fromhex(h)[0xC3]:02X}' for h in hexes])
