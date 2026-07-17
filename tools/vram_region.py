#!/usr/bin/env python3
"""Report non-zero byte density of OBJ tile regions in a saved VRAM dump.

Usage: python tools/vram_region.py <vram.json>
Computes, for a set of OBJ tile numbers, the VRAM byte offset (obsel=0x03:
name base word 0x6000, name-gap 0x1000 words for tiles >=0x100) and how
many of that tile's 32 bytes are non-zero (0 = blank/unloaded tile).
"""
import sys, json

j = json.load(open(sys.argv[1], encoding='utf-8-sig'))
vram = bytes.fromhex(j['hex'])

BASE_WORD = 0x6000          # (obsel & 7) << 13
GAP_WORD = 0x1000           # ((obsel>>3 & 3)+1) << 12, obsel=0x03 -> 0x1000


def tile_byte(t):
    if t < 0x100:
        w = BASE_WORD + t * 16
    else:
        w = BASE_WORD + GAP_WORD + (t - 0x100) * 16
    return w * 2


def density(t):
    off = tile_byte(t)
    chunk = vram[off:off + 32]
    nz = sum(1 for b in chunk if b)
    return off, nz


def region(label, tiles):
    total_nz = 0
    per = []
    for t in tiles:
        off, nz = density(t)
        total_nz += nz
        per.append((t, off, nz))
    print(f"\n== {label}: tiles 0x{tiles[0]:03x}-0x{tiles[-1]:03x} ==")
    print(f"   total non-zero bytes: {total_nz} / {len(tiles)*32}")
    for t, off, nz in per[:16]:
        bar = '#' * (nz * 20 // 32)
        print(f"   tile 0x{t:03x} @ 0x{off:05x}: {nz:>2}/32 {bar}")


region("PLAYER (sanity, visible in both)", list(range(0x002, 0x020)))
region("INVISIBLE turtle OAM -> 0x100 base", list(range(0x100, 0x140)))
region("VISIBLE turtle tiles -> 0x130 base", list(range(0x130, 0x170)))
