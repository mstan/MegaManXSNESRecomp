#!/usr/bin/env python3
"""Scan an address range and report which function last wrote each byte.

Uses the always-on wram write-log ring. Helps locate a routine's
direct-page footprint (e.g. find D for bank_00_D6A7 by its scratch writes).

Usage: python tools/scan_writers.py <start_hex> <end_hex> [func_substr]
Env: DBG_PORT (default 4379)
"""
import sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
start = int(sys.argv[1], 16)
end = int(sys.argv[2], 16)
want = sys.argv[3] if len(sys.argv) > 3 else None

c = DebugClient(PORT, name=f'dbg:{PORT}')
h = c.get_history()
frm = h['newest'] - 120
for a in range(start, end + 1):
    r = c.query(f'wram_writes_at 0x{a:x} {frm} {h["newest"]} 1')
    m = r.get('matches') or []
    if not m:
        continue
    func = m[0]['func']
    if want and want not in func:
        continue
    print(f"0x{a:04x}: {func:28s} val={m[0]['val']} old={m[0]['old']} w={m[0]['w']}")
