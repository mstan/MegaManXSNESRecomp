#!/usr/bin/env python3
"""Turn a co-op netplay log (logs/coop-netplay-*.csv) into a replay file.

The netplay CSV records, for every simulated tick from power-on, the input
each seat consumed and both seats' $0BA8 bodies. Rollback requires the
simulation to be deterministic, so replaying those inputs headlessly
(mmx_state_tests with MMX_COOP_REPLAY) reproduces the session exactly,
provided the build, mods and display settings match the recorded ones.

Output lines:
  # comment
  R <count> <p1_input> <p2_input>   input run, runner pad bits (decimal)
  C <tick> <p1_x> <p1_y> <p2_x> <p2_y>   body checkpoint after that tick

Inputs only: no ROM or save bytes are written.

  python -I tools/coop_replay_from_csv.py LOG.csv OUT.replay [--every N] [--until T]
"""
import argparse
import csv


def body_xy(hex_body):
    b = bytes.fromhex(hex_body)
    return b[5] | b[6] << 8, b[8] | b[9] << 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('out')
    ap.add_argument('--every', type=int, default=30, help='checkpoint interval in ticks')
    ap.add_argument('--until', type=int, default=None, help='last tick to keep')
    args = ap.parse_args()
    rows = list(csv.DictReader(open(args.log, newline='')))
    if args.until is not None:
        rows = [r for r in rows if int(r['sim_tick']) <= args.until]
    for i, r in enumerate(rows):
        if int(r['sim_tick']) != i:
            raise SystemExit(f'{args.log}: tick {r["sim_tick"]} is not contiguous at row {i}')
    with open(args.out, 'w') as out:
        out.write(f'# co-op replay from {args.log.rsplit("/", 1)[-1]}, {len(rows)} ticks\n')
        run, count = None, 0
        for r in rows:
            key = (int(r['p1_input'], 16), int(r['p2_input'], 16))
            if key == run:
                count += 1
                continue
            if run is not None:
                out.write(f'R {count} {run[0]} {run[1]}\n')
            run, count = key, 1
        if run is not None:
            out.write(f'R {count} {run[0]} {run[1]}\n')
        for r in rows:
            t = int(r['sim_tick'])
            if t % args.every == 0 or t == len(rows) - 1:
                x1, y1 = body_xy(r['p1_body'])
                x2, y2 = body_xy(r['p2_body'])
                out.write(f'C {t} {x1} {y1} {x2} {y2}\n')


if __name__ == '__main__':
    main()
