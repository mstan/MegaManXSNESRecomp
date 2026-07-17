#!/usr/bin/env python3
"""Locate the OAM source buffer inside a WRAM dump by matching captured OAM.

Searches the WRAM hex for the 512-byte OAM low table (and the 32-byte high
table) from a saved dump_oam json. Prints the WRAM offset(s) where they live.

Usage: python tools/find_oam_buf.py wram.json oam.json
"""
import sys, json

wram = bytes.fromhex(json.load(open(sys.argv[1], encoding='utf-8-sig'))['hex'])
oam = bytes.fromhex(json.load(open(sys.argv[2], encoding='utf-8-sig'))['hex'])
low, high = oam[:512], oam[512:544]


def find_all(hay, needle, label):
    hits = []
    start = 0
    while True:
        i = hay.find(needle, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    print(f"{label} ({len(needle)} bytes): "
          + (", ".join(f"0x{h:05x}" for h in hits) if hits else "NOT FOUND"))
    return hits


# Try the full low table, then progressively shorter prefixes (the live OAM
# may differ from the buffer in a few moving sprites).
for n in (512, 256, 128, 64):
    if find_all(wram, low[:n], f"OAM low-table first {n}B"):
        break
find_all(wram, high, "OAM high-table")
