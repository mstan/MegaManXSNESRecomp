#!/usr/bin/env python3
"""Capture the alloc-vs-bind execution order for the Spark Mandrill turtle.

Methodology (ring-buffer discipline): arm WRAM write-watchpoints on BOTH the
turtle's VRAM-slot alloc ($7F:828d, written by B15B) and its tile-base bind
($7E:1040, written by 827D). Park on the first event, dump the always-on rings
WHILE FROZEN, clear the fired watch, continue to the second event, dump again.
By the second park both writes are in the s_wram_trace ring with their global
block_idx (bi) -> definitive ordering, with the writing func+parent for each.

Run this, THEN drive the approach (walk or dash-jump). It freezes/dumps itself.

  python tools/capture_order.py <label> [--reload] [--timeout SEC]

<label> tags the output file: cap_order_<label>.json
--reload : loadstate 0 + clear_controller before arming (clean start)
"""
import sys, os, json, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'snesrecomp', 'tools'))
from sneslib.client import DebugClient  # noqa: E402

PORT = int(os.environ.get('DBG_PORT', '4379'))
ALLOC = '1828d'   # $7F:828d turtle VRAM slot (B15B writes 0x30)
BIND  = '1040'    # $7E:1040 turtle tile-base latch (827D)


def dump_rings(c, frame):
    """Snapshot the always-on rings around the current (frozen) frame."""
    lo = max(0, frame - 6)
    snap = {'frame': frame}
    snap['parked'] = c.query('parked')
    snap['alloc_writes'] = c.query(f'wram_writes_at {ALLOC} {lo} 999999999 64').get('matches', [])
    snap['bind_writes']  = c.query(f'wram_writes_at {BIND} {lo} 999999999 64').get('matches', [])
    # dispatcher entries in the frozen call ring (frame-bounded)
    ct = c.query(f'get_call_trace from={lo} contains=953F')
    snap['dispatch_953F'] = ct.get('log', [])[-30:]
    return snap


def wait_park(c, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        p = c.query('parked')
        if p.get('parked'):
            return p
        time.sleep(0.05)
    return None


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else 'run'
    reload0 = '--reload' in sys.argv
    timeout = 120
    if '--timeout' in sys.argv:
        timeout = float(sys.argv[sys.argv.index('--timeout') + 1])

    c = DebugClient(PORT, name=f'cap:{PORT}')
    if reload0:
        c.query('clear_controller')
        c.query('loadstate 0')
        time.sleep(1.0)
    c.query('watch_clear')
    c.query(f'watch_add {ALLOC}')   # idx 0
    c.query(f'watch_add {BIND}')    # idx 1
    print(f'[armed] watches: alloc={ALLOC} bind={BIND}. DRIVE THE APPROACH NOW '
          f'(label={label}, timeout={timeout}s).', flush=True)

    captures = []
    fired_addrs = []
    for stage in (1, 2):
        p = wait_park(c, timeout)
        if not p:
            print(f'[stage {stage}] TIMEOUT waiting for park', flush=True)
            break
        frame = c.query('ping').get('frame')
        addr = p.get('watch_addr')
        val = p.get('watch_val')
        writer = p.get('writer')
        fired_addrs.append(addr)
        print(f'[stage {stage}] PARKED frame={frame} addr={addr} val={val} writer={writer}', flush=True)
        snap = dump_rings(c, frame)
        snap['stage'] = stage
        snap['fired_addr'] = addr
        captures.append(snap)
        # Clear the watch that just fired so we advance to the OTHER event,
        # not re-park on a steady-state rewrite of the same address.
        c.query('watch_clear')
        other = BIND if addr and addr.endswith('828d') else ALLOC
        if stage == 1:
            c.query(f'watch_add {other}')
            print(f'[stage {stage}] cleared {addr}, now watching {other}; continuing', flush=True)
        c.query('break_continue')

    out = {'label': label, 'fired_order': fired_addrs, 'captures': captures}
    path = os.path.join('.', f'cap_order_{label}.json')
    with open(path, 'w') as f:
        json.dump(out, f, indent=2)
    print(f'[done] wrote {path}', flush=True)

    # Quick verdict: compare bi of the alloc vs bind writes seen in stage 2.
    alloc_bi = bind_bi = None
    for snap in captures:
        for w in snap.get('alloc_writes', []):
            alloc_bi = w.get('bi')
        for w in snap.get('bind_writes', []):
            bind_bi = w.get('bi')
    print(f'[verdict] alloc bi={alloc_bi}  bind bi={bind_bi}', flush=True)
    if alloc_bi is not None and bind_bi is not None:
        order = 'ALLOC before BIND (correct/walk)' if alloc_bi < bind_bi else 'BIND before ALLOC (inverted/broken)'
        print(f'[verdict] {order}', flush=True)


if __name__ == '__main__':
    main()
