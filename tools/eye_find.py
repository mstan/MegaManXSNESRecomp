import sys, json, collections
# Reads a get_wram_trace JSON dump on stdin; reports per-address value
# trajectory so a monotonically-moving coordinate (the flying eye) stands out.
d = json.load(sys.stdin)
log = d.get('log', [])
by = collections.defaultdict(list)
for m in log:
    by[m['adr']].append((m['f'], int(m['val'], 16), int(m['old'], 16), m.get('w', 1)))
rows = []
for adr, seq in by.items():
    seq.sort()
    vals = [v for _, v, _, _ in seq]
    width = seq[0][3]
    vmin, vmax = min(vals), max(vals)
    span = vmax - vmin
    # monotonic score: fraction of steps moving the same direction
    ups = sum(1 for a, b in zip(vals, vals[1:]) if b > a)
    dns = sum(1 for a, b in zip(vals, vals[1:]) if b < a)
    steps = max(1, ups + dns)
    mono = max(ups, dns) / steps
    rows.append((adr, len(seq), width, vmin, vmax, span, round(mono, 2), vals[0], vals[-1]))
# sort by span*monotonicity — a moving coordinate has big span AND high monotonicity
rows.sort(key=lambda r: r[5] * r[6], reverse=True)
print("adr      n   w   min    max    span  mono  first  last")
for r in rows[:30]:
    print("%s %3d %2d %6d %6d %6d %5.2f %6d %6d" % r)
