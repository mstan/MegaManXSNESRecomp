#!/usr/bin/env python3
"""Find eye-region 16-bit words that reach the LEVEL range (~4800-5500) on
hardware — the eye's true AI position field (must be ~5143 to match $0BAD).
"""
import os, sys, json, collections
DEF = r"F:\Projects\mmxref\mmx_trace.jsonl"
path = sys.argv[1] if len(sys.argv) > 1 else DEF
tl = collections.defaultdict(list)
for line in open(path):
    line = line.strip()
    if not line:
        continue
    e = json.loads(line)
    tl[int(e["adr"], 16)].append((e["f"], int(e["val"], 16)))

def val_at(a, f):
    v = None
    for ff, vv in tl.get(a, []):
        if ff <= f:
            v = vv
        else:
            break
    return v

allf = sorted({f for a, seq in tl.items() if 0xE00 <= a <= 0x1FFF for f, _ in seq})
print("# eye-region 16-bit words reaching LEVEL range (4800-5500):")
hits = []
for base in range(0x0E00, 0x1FF0):
    if base not in tl and base + 1 not in tl:
        continue
    vals = []
    for f in allf:
        l = val_at(base, f)
        h = val_at(base + 1, f)
        if l is None or h is None:
            continue
        vals.append((f, l | (h << 8)))
    inr = [(f, v) for f, v in vals if 4800 <= v <= 5500]
    if len(inr) >= 5:
        vv = [v for _, v in vals]
        hits.append((base, len(vals), min(vv), max(vv), inr))
for base, n, vmin, vmax, inr in hits:
    fr = f"{inr[0][0]}..{inr[-1][0]}"
    print(f"  ${base:05x}: n={n} min={vmin} max={vmax}  in-range {inr[0][1]}..{inr[-1][1]} over f{fr}")
if not hits:
    print("  (none) -> hardware eye AI position is NOT level-absolute in $0E00-$1FFF")
