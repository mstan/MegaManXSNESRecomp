#!/usr/bin/env python3
"""Find the turtle's object slot X by matching screen position + unbound gfx.

D6A7 (D=0): worldX=[$0008+X] (16b), worldY=[$0005+X] (16b),
attr=[$0011+X], tile-base=[$0018+X]. screenX ~= worldX - cam[$1E50],
screenY ~= worldY - cam[$1E4D]. The turtle is on-screen at x~52-108,
y~51-91 (from OAM) and has tile-base 0 (unbound) on the bug.

Usage: python tools/find_turtle_slot2.py <wram.json>  (wram must cover 0..0x1FFF)
"""
import sys, json

ram = bytes.fromhex(json.load(open(sys.argv[1], encoding='utf-8-sig'))['hex'])


def w(a):
    return ram[a] | (ram[a + 1] << 8)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


camX = w(0x1e50)
camY = w(0x1e4d)
print(f"camera: X=$1E50={camX:04x}  Y=$1E4D={camY:04x}\n")
print(f"{'X':>4} {'attr':>5} {'tbase':>6} {'worldX':>7} {'worldY':>7} "
      f"{'scrX':>6} {'scrY':>6}  note")
cands = []
for X in range(0x00, 0x100):
    attr = ram[0x11 + X]
    tbase = ram[0x18 + X]
    wx = w(0x08 + X)
    wy = w(0x05 + X)
    sx = s16((wx - camX) & 0xffff)
    sy = s16((wy - camY) & 0xffff)
    onscreen = (-16 <= sx <= 272) and (-16 <= sy <= 240)
    turtleish = (40 <= sx <= 120) and (30 <= sy <= 110)
    if turtleish:
        note = '<<< TURTLE-POS' + ('  UNBOUND(tbase=0)' if tbase == 0 else '')
        cands.append((X, attr, tbase, sx, sy))
        print(f"0x{X:02x} 0x{attr:02x}  0x{tbase:02x}   {wx:6x} {wy:6x} "
              f"{sx:6d} {sy:6d}  {note}")
print(f"\n{len(cands)} candidate(s) at turtle screen position.")
