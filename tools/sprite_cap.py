#!/usr/bin/env python3
"""Capture sprite_timeseries for all 12 object slots from the debug server.

Usage:
  python tools/sprite_cap.py [out.jsonl] [from] [to] [changes_only]

Writes one JSON line per slot: {"slot":N, "emitted":..,"entries":[...]}.
Also prints a per-slot summary (how many change-entries, st values seen).
"""
import sys, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
out = sys.argv[1] if len(sys.argv) > 1 else None
frm = sys.argv[2] if len(sys.argv) > 2 else ''
to = sys.argv[3] if len(sys.argv) > 3 else ''
chg = sys.argv[4] if len(sys.argv) > 4 else '1'

c = DebugClient(PORT, name=f'dbg:{PORT}')
h = c.get_history()
print(f"history oldest={h['oldest']} newest={h['newest']} count={h['count']}", file=sys.stderr)
f_from = frm if frm else h['oldest']
f_to = to if to else h['newest']

fh = open(out, 'w', encoding='utf-8') if out else None
print(f"{'slot':>4} {'emitted':>7} {'considered':>10}  st-values-seen")
for slot in range(12):
    r = c.query(f'sprite_timeseries {slot} {f_from} {f_to} {chg} 4096')
    ents = r.get('entries', [])
    sts = sorted({e['st'] for e in ents})
    ns = sorted({e['n'] for e in ents})
    print(f"{slot:>4} {r.get('emitted'):>7} {r.get('considered'):>10}  "
          f"st={sts} n={ns}")
    if fh:
        fh.write(json.dumps({'slot': slot, 'from': r.get('from'),
                             'to': r.get('to'), 'emitted': r.get('emitted'),
                             'entries': ents}) + '\n')
if fh:
    fh.close()
    print(f"\nwrote {out}", file=sys.stderr)
