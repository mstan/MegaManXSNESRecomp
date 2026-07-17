#!/usr/bin/env python3
"""Query the always-on WRAM object ring and rank candidate movers in-process.

Avoids the PowerShell native-pipe UTF-16 mangling that corrupts
`get_wram_trace | eye_find.py`. Connects to the debug server directly,
pulls the ring, and prints the same span*monotonicity ranking eye_find.py
produces (a flying eye = big span + high monotonic coordinate).

Usage: python tools/eye_scan.py [topN]
"""
import os
import sys
import json
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "snesrecomp", "tools"))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get("DBG_PORT", "4379"))


def main():
    topn = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    c = DebugClient(PORT, name=f"dbg:{PORT}")
    raw = c.query_raw("get_wram_trace")
    d = json.loads(raw)
    log = d.get("log", [])
    by = collections.defaultdict(list)
    for m in log:
        by[m["adr"]].append((m["f"], int(m["val"], 16), int(m["old"], 16), m.get("w", 1)))
    rows = []
    for adr, seq in by.items():
        seq.sort()
        vals = [v for _, v, _, _ in seq]
        width = seq[0][3]
        vmin, vmax = min(vals), max(vals)
        span = vmax - vmin
        ups = sum(1 for a, b in zip(vals, vals[1:]) if b > a)
        dns = sum(1 for a, b in zip(vals, vals[1:]) if b < a)
        steps = max(1, ups + dns)
        mono = max(ups, dns) / steps
        rows.append((adr, len(seq), width, vmin, vmax, span, round(mono, 2),
                     vals[0], vals[-1], seq[0][0], seq[-1][0]))
    rows.sort(key=lambda r: r[5] * r[6], reverse=True)
    print(f"# entries={d.get('entries')} ranges={d.get('ranges')}")
    print("adr      n    w   min    max    span  mono  first  last   f0     f1")
    for r in rows[:topn]:
        print("%s %4d %2d %6d %6d %6d %5.2f %6d %6d %6d %6d" % r)


if __name__ == "__main__":
    main()
