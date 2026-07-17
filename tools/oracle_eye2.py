#!/usr/bin/env python3
"""Reconstruct the hardware blue-eye POSITION from mmx_trace.jsonl.

Finds, in the object region, the 16-bit word with the largest value-span
during the flight (the eye's position, which marches out toward X and back).
Reports its integer value (both as a raw 16-bit word and as a >>8 fixed-point
integer) so we can tell level-absolute (~5000s) from screen-relative (~100s).
"""
import os, sys, json, collections
DEF = r"F:\Projects\mmxref\mmx_trace.jsonl"
path = sys.argv[1] if len(sys.argv) > 1 else DEF
tl = collections.defaultdict(list)
with open(path) as fh:
    for line in fh:
        line = line.strip()
        if not line: continue
        e = json.loads(line); a = int(e["adr"], 16)
        tl[a].append((e["f"], int(e["val"], 16)))
def val_at(a, f):
    v = None
    for ff, vv in tl.get(a, []):
        if ff <= f: v = vv
        else: break
    return v
# Frames where any object byte changed
allf = sorted({f for a, seq in tl.items() if 0x0E00 <= a <= 0x1FFF for f, _ in seq})
# For each 16-bit word base in the eye cluster, reconstruct word over frames.
print("# 16-bit words in $0E00-$0F40 by value-span during capture:")
rows = []
for base in range(0x0E00, 0x0F40):
    if base not in tl and base+1 not in tl: continue
    vals = []
    for f in allf:
        l = val_at(base, f); h = val_at(base+1, f)
        if l is None or h is None: continue
        vals.append(l | (h << 8))
    if len(vals) < 5: continue
    span = max(vals) - min(vals)
    rows.append((span, base, min(vals), max(vals), vals[0], vals[-1]))
rows.sort(reverse=True)
for span, base, vmin, vmax, v0, v1 in rows[:12]:
    print(f"   ${base:05x}: span={span:6d}  min={vmin} max={vmax}  "
          f"first={v0} last={v1}   intX(>>8)={vmin>>8}..{vmax>>8}")
