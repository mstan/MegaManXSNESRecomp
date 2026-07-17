#!/usr/bin/env python3
"""Diff two WRAM dumps to find binding fields: bytes 0x00 in 'invisible'
but nonzero in 'visible'. Highlights tile-base (->0x30) and palette/priority
(low6 bits = pal/prio) candidates.

Usage: python tools/diff_wram.py <visible.json> <invisible.json> [hi_addr=0x800]
"""
import sys, json

vis = bytes.fromhex(json.load(open(sys.argv[1], encoding='utf-8-sig'))['hex'])
inv = bytes.fromhex(json.load(open(sys.argv[2], encoding='utf-8-sig'))['hex'])
hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x800

print(f"bytes 0x00 in invisible, nonzero in visible (addr < 0x{hi:03x}):\n")
print(f"{'addr':>6} {'vis':>4} {'inv':>4}  note")
zero_to_val = []
for a in range(min(len(vis), len(inv), hi)):
    if inv[a] == 0 and vis[a] != 0:
        zero_to_val.append(a)
        v = vis[a]
        note = ''
        if v == 0x30:
            note = '*** ==0x30  TILE-BASE candidate'
        # attr byte: bit0=tilehi, bits1-3=pal, bits4-5=prio
        pal = (v >> 1) & 7
        prio = (v >> 4) & 3
        if v in (0x2a, 0x2b, 0x0a, 0x0b) or (pal == 5 and prio == 2):
            note += f'  ATTR candidate (pal={pal} prio={prio})'
        print(f"0x{a:04x} 0x{v:02x} 0x{inv[a]:02x}  {note}")
print(f"\n{len(zero_to_val)} such bytes below 0x{hi:03x}.")

# Also: exact tile-base hits across full low RAM
t30 = [a for a in range(min(len(vis), len(inv), 0x2000))
       if vis[a] == 0x30 and inv[a] == 0x00]
print(f"\naddrs where vis==0x30 & inv==0x00 (full $0000-$1FFF): "
      + ", ".join(f"0x{a:04x}" for a in t30))
