#!/usr/bin/env python3
"""Read mmxref's mmx_trace.jsonl and extract the hardware blue-eye operands.

mmxref logs per-byte WRAM CHANGES (delta-encoded: {f, adr, old, val}) for the
retargeted set ($0BAD/$0BB0 targets + the $0E00-$1FFF object region). This
reconstructs value timelines and reports:
  - target X $0BAD (16-bit) and target Y $0BB0 (16-bit) over the capture
  - the flying eye's position: the byte that changes ~every frame in the
    object region (fixed-point low byte) and the adjacent integer byte, so we
    can read the eye's integer X during flight.

Compare to the recomp: eye integer X ~83 (screen) vs $0BAD 5143 (level).
If hardware eye integer X ~= $0BAD (both ~5100s) -> recomp eye position is the
bug. If hardware $0BAD ~= eye screen X (both small) -> recomp $0BAD is the bug.

Usage: python tools/oracle_eye.py [path-to-mmx_trace.jsonl]
"""
import os, sys, json, collections

DEF = r"F:\Projects\mmxref\mmx_trace.jsonl"
path = sys.argv[1] if len(sys.argv) > 1 else DEF

# Reconstruct per-address (frame -> value) from deltas.
tl = collections.defaultdict(list)   # adr -> [(f, val)]
frames = set()
with open(path) as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        e = json.loads(line)
        a = int(e["adr"], 16)
        tl[a].append((e["f"], int(e["val"], 16)))
        frames.add(e["f"])
if not frames:
    print("# empty trace"); sys.exit(0)
fmin, fmax = min(frames), max(frames)
print(f"# trace {path}: frames {fmin}..{fmax}, {len(tl)} addrs touched")

def val_at(a, f):
    """value of byte a at/just-before frame f (last write <= f), else None."""
    seq = tl.get(a)
    if not seq:
        return None
    v = None
    for ff, vv in seq:
        if ff <= f:
            v = vv
        else:
            break
    return v

def u16_series(lo):
    """list of (f, 16-bit val) at each frame where either byte changed."""
    fs = sorted({ff for ff, _ in tl.get(lo, [])} | {ff for ff, _ in tl.get(lo+1, [])})
    out = []
    for f in fs:
        l = val_at(lo, f); h = val_at(lo+1, f)
        if l is None: l = 0
        if h is None: h = 0
        out.append((f, l | (h << 8)))
    return out

print("\n# target X $0BAD (16-bit):")
for f, v in u16_series(0x0BAD)[-12:]:
    print(f"   f{f}: {v}  (0x{v:04x})")
print("# target Y $0BB0 (16-bit):")
for f, v in u16_series(0x0BB0)[-12:]:
    print(f"   f{f}: {v}  (0x{v:04x})")

# Find the flying eye: the byte in $0E00-$1FFF that changes on the most frames
# (the position fixed-point low byte ticks ~every frame while flying).
churn = []
for a, seq in tl.items():
    if 0x0E00 <= a <= 0x1FFF:
        churn.append((len(seq), a))
churn.sort(reverse=True)
print("\n# busiest object-region bytes (candidate eye position low byte):")
for n, a in churn[:8]:
    print(f"   ${a:05x}: {n} writes")
if churn:
    lo = churn[0][1]
    # integer X likely 1-2 bytes above the fixed-point low byte
    print(f"\n# reconstructing position around busiest byte ${lo:05x}:")
    for off in (0, 1, 2):
        seq = sorted(tl.get(lo+off, []))[-10:]
        vs = " ".join(f"f{f}={v}" for f, v in seq)
        print(f"   ${lo+off:05x}: {vs}")
    # 24-bit integer X from (lo .. lo+2): X = (b2<<16 | b1<<8 | b0) >> 8
    fs = sorted({f for f, _ in tl.get(lo, [])})[-10:]
    print("   integer X (=(b2:b1:b0)>>8) over last frames:")
    for f in fs:
        b0 = val_at(lo, f) or 0; b1 = val_at(lo+1, f) or 0; b2 = val_at(lo+2, f) or 0
        xi = ((b2 << 16) | (b1 << 8) | b0) >> 8
        print(f"     f{f}: intX={xi}")
