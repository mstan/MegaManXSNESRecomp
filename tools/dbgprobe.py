#!/usr/bin/env python3
"""Ad-hoc probe against the running Debug-build debug server (port 4379).

Usage:
  python tools/dbgprobe.py ping
  python tools/dbgprobe.py history
  python tools/dbgprobe.py frames <start> <end>   # raw per-frame snapshots
  python tools/dbgprobe.py tail [N]                # last N frames (default 30)
  python tools/dbgprobe.py raw "<server command>" # send any raw command
"""
import sys, os, json

# Make sneslib importable regardless of cwd.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))


def main():
    c = DebugClient(PORT, name=f'dbg:{PORT}')
    args = sys.argv[1:]
    cmd = args[0] if args else 'history'

    if cmd == 'ping':
        print(json.dumps(c.query('ping')))
    elif cmd == 'history':
        print(json.dumps(c.get_history()))
    elif cmd == 'frames':
        start, end = int(args[1]), int(args[2])
        fr = c.get_frame_range(start, end)
        for f in sorted(fr):
            print(json.dumps(fr[f]))
    elif cmd == 'tail':
        n = int(args[1]) if len(args) > 1 else 30
        h = c.get_history()
        start = max(h['oldest'], h['newest'] - n + 1)
        fr = c.get_frame_range(start, h['newest'])
        print(f"# history oldest={h['oldest']} newest={h['newest']}", file=sys.stderr)
        for f in sorted(fr):
            print(json.dumps(fr[f]))
    elif cmd == 'raw':
        print(c.query_raw(args[1]))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
