#!/usr/bin/env python3
"""Scan the recomp capture (_eye_capture.json) for object-region 16-bit words,
reporting their value at the FIRST snapshot (socket phase) and their range, to
find whether the eye's position is ever level-absolute (~5100-5300) like
hardware, or purely screen-range (0-500).
"""
import os, json
HERE = os.path.dirname(os.path.abspath(__file__))
snaps = json.load(open(os.path.join(HERE, "_eye_capture.json")))
OBJ_LO = 0x0E00
objs = [bytes.fromhex(s["obj"]) for s in snaps]
N = len(objs)
def u16(b, o): return b[o] | (b[o+1] << 8)
print(f"# {N} snapshots; object-region 16-bit words in LEVEL range (4900-5400) at ANY snapshot:")
hits = []
for o in range(0, len(objs[0]) - 1):
    vals = [u16(b, o) for b in objs]
    lvl = [v for v in vals if 4900 <= v <= 5400]
    if len(lvl) >= 3:
        hits.append((OBJ_LO + o, vals[0], min(vals), max(vals), vals[-1]))
for addr, v0, vmin, vmax, vN in hits:
    print(f"  ${addr:05x}: first={v0} min={vmin} max={vmax} last={vN}")
if not hits:
    print("  (NONE) -> recomp eye/objects are NOT level-absolute anywhere; purely screen-range.")
# Also: the busiest (fast-marching) word = eye frac; show its neighbors' first values
print("\n# first-snapshot values around the busiest marcher (socket phase):")
churn = []
for o in range(0, len(objs[0]) - 1):
    vals = [u16(b, o) for b in objs]
    d = sum(1 for a, b in zip(vals, vals[1:]) if a != b)
    churn.append((d, OBJ_LO + o))
churn.sort(reverse=True)
top = churn[0][1]
for a in range(top - 4, top + 6):
    o = a - OBJ_LO
    if 0 <= o < len(objs[0]) - 1:
        print(f"  ${a:05x}: first={u16(objs[0], o)}  last={u16(objs[-1], o)}")
