#!/usr/bin/env python3
"""Analyze a capture*.jsonl timeseries dump: per-field transitions.

Usage: python tools/an_capture.py <file.jsonl> [field ...]
Default fields: mode gfx
"""
import json, sys

path = sys.argv[1]
fields = sys.argv[2:] or ['mode', 'gfx']
rows = [json.loads(l) for l in open(path, encoding='utf-8-sig') if l.strip()]
print(f"frames {rows[0]['f']} .. {rows[-1]['f']}  n={len(rows)}")


def trans(field):
    out, prev = [], object()
    for r in rows:
        v = r.get(field)
        if v != prev:
            out.append((r['f'], v))
            prev = v
    return out


for field in fields:
    t = trans(field)
    print(f"\n== {field} transitions: {len(t)} ==")
    for f, v in t[:120]:
        print(f"  {f}: {v}")
