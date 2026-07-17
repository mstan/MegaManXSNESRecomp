import sys, json, re
d = json.load(sys.stdin)
log = d.get('log', [])
if log:
    print('frames span:', log[0]['f'], '->', log[-1]['f'], '| shown', len(log))
pat = re.compile('AF21|DB4B|DB8A|DBA9|DBE0|DBF5|DC2D|DC71|820A')
seen = [m for m in log if pat.search(m['func']) or pat.search(m.get('parent', ''))]
for m in seen[:40]:
    print('f%s d%s %s <- %s' % (m['f'], m['d'], m['func'], m['parent']))
print('eye-handler hits:', len(seen))
