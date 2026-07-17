#!/usr/bin/env python3
"""Scan MMX object slots in live low RAM to find the turtle's slot index X.

D6A7 (D=0) reads per-object fields at $0011+X (palette/priority attr),
$0018+X (tile-base/VRAM page), $0008+X (world X word), $0005+X (world Y).
The invisible turtle = an ACTIVE slot whose tile-base is 0 (unbound) while
other active enemies have nonzero tile-base.

Usage: python tools/find_turtle_slot.py            # live, frames check + scan
Env: DBG_PORT (default 4379)
"""
import sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
c = DebugClient(PORT, name=f'dbg:{PORT}')

h1 = c.get_history()['newest']
h2 = c.get_history()['newest']
print(f"frame newest: {h1} -> {h2}  ({'RUNNING' if h2 != h1 else 'PAUSED/STALLED'})")

ram = bytes.fromhex(c.query('dump_ram 0 512')['hex'])


def w(a):
    return ram[a] | (ram[a + 1] << 8)


cam_x = w(0x1e50) if len(ram) > 0x1e51 else None  # (only have 0..0x200 here)
print(f"\n{'X':>4} {'attr[0x11+X]':>12} {'tbase[0x18+X]':>13} "
      f"{'posX[0x08+X]':>12} {'posY[0x05+X]':>12}")
for X in range(0x20, 0x100, 0x10):
    attr = ram[0x11 + X]
    tbase = ram[0x18 + X]
    px = w(0x08 + X)
    py = w(0x05 + X)
    active = attr or tbase or px or py
    flag = '  <- active' if active else ''
    flag += '  *** tile-base=0 (UNBOUND?)' if (active and tbase == 0) else ''
    print(f"0x{X:02x} 0x{attr:02x}         0x{tbase:02x}          "
          f"0x{px:04x}       0x{py:04x}{flag}")
