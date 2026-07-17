#!/usr/bin/env python3
"""Offline analysis of _launch_ring.json: find the boss-piece object (stride
0x20, mover bank_02_820A) whose X starts near $0BAD and runs away, and dump its
full 0x20 struct trajectory so we can see the launch discontinuity + velocity +
timer fields."""
import os, json
HERE = os.path.dirname(os.path.abspath(__file__))
OBJ_LO = 0x0E00
d = json.load(open(os.path.join(HERE, "_launch_ring.json")))
frames = d["frames"]
snaps = [bytes.fromhex(s) for s in d["snaps"]]
N = len(snaps)


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def s16(v):
    return v - 65536 if v > 32768 else v


# Boss-piece object bases the mover writes (stride 0x20).
bases = list(range(0x0198c, 0x01ad0, 0x20))
print(f"# {N} snaps, frames {frames[0]}..{frames[-1]}")
print(f"# candidate object bases (mover-written, stride 0x20): "
      f"{[hex(b) for b in bases]}\n")

# For each base, the 24-bit X = word@+0 + byte@+2. Track integer-ish X.
# Identify the runaway one: level-range early, large motion.
for b in bases:
    o = b - OBJ_LO
    xlo = [u16(s, o) for s in snaps]            # low 16 bits of X
    xhi = [s[o + 2] for s in snaps]             # X bank/high byte
    x24 = [xhi[i] << 16 | xlo[i] for i in range(N)]
    # per-step delta of low word (wrap-aware)
    dmax = max(abs(s16(a - b2)) for a, b2 in zip(xlo, xlo[1:])) if N > 1 else 0
    early = xlo[:max(3, N // 6)]
    lvl = any(4700 <= v <= 5500 for v in early)
    span24 = max(x24) - min(x24)
    tag = "  <-- level-range start" if lvl else ""
    print(f"base ${b:05x}: xlo first={xlo[0]} hi={xhi[0]} "
          f"maxstep={dmax} span24={span24}{tag}")

print("\n# Now dump the full struct of the runaway level-range object(s):")
# Heuristic pick: level-range early AND big span24.
picks = []
for b in bases:
    o = b - OBJ_LO
    xlo = [u16(s, o) for s in snaps]
    early = xlo[:max(3, N // 6)]
    if not any(4700 <= v <= 5500 for v in early):
        continue
    x24 = [(s[o + 2] << 16 | u16(s, o)) for s in snaps]
    picks.append((max(x24) - min(x24), b))
picks.sort(reverse=True)

for span, b in picks[:3]:
    o = b - OBJ_LO
    print(f"\n==== object base ${b:05x} (span24={span}) ====")
    # show every 16-bit field 0..0x20 trajectory (sampled) + every byte for key offs
    print("  snap  frame  | " + "  ".join(f"+{k:02x}w" for k in range(0, 0x20, 2)))
    step = max(1, N // 24)
    for i in range(0, N, step):
        s = snaps[i]
        row = "  ".join(f"{u16(s, o + k):5d}" for k in range(0, 0x20, 2))
        print(f"  {i:4d}  {frames[i]:6d} | {row}")
