#!/usr/bin/env python3
"""Correlate $0BAD/$0BB0 with candidate eye positions over the whole capture,
so we can read the operands AT the blue-eye launch (not the nose phase).

Prints the $0BAD timeline (every change) and, for a chosen eye-position word,
its reconstructed value per frame — side by side in frame order.
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
def series16(lo):
    fs = sorted({ff for ff, _ in tl.get(lo, [])} | {ff for ff, _ in tl.get(lo+1, [])})
    return [(f, (val_at(lo, f) or 0) | ((val_at(lo+1, f) or 0) << 8)) for f in fs]

# $0BAD timeline, compressed to runs of equal value
print("# $0BAD (target X) timeline — [frame] value, runs:")
prev = None
for f, v in series16(0x0BAD):
    if v != prev:
        print(f"   f{f}: {v}  (0x{v:04x})")
        prev = v

print("\n# $0BB0 (target Y) timeline, runs:")
prev = None
for f, v in series16(0x0BB0):
    if v != prev:
        print(f"   f{f}: {v}")
        prev = v
