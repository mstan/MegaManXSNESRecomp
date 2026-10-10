"""Route native boss boundaries through the interpreter only in Boss Rush.

Never edit generated instruction bodies; use the same balanced entry bridge
as co-op. Re-applying this pass is idempotent and preserves all other hooks.
"""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-RUSH*/'
TARGETS = {0x01b516, 0x078a7e, 0x01c433, 0x03b144, 0x0791a7,
           0x07d85c, 0x08853e, 0x089be1, 0x049feb, 0x04a003,
           0x04aadd, 0x04a677, 0x049b03, 0x049b43, 0x00dc36,
           0x0094d1, 0x00d345, 0x00d48d, 0x00d4aa, 0x00d4ea,
           0x00d507, 0x00d1ed, 0x07923c, 0x079270,
           0x048fca, 0x048f52, 0x048f7d, 0x00e68e, 0x00dcdb,
           0x0088a2, 0x0088cd, 0x049f19, 0x049f2a, 0x049f2f, 0x049f7e,
           0x088dab, 0x088e0f, 0x088d79, 0x088d2f, 0x02827d, 0x03b151}
# The USA dispatch has no compiled body for these entries. They already
# execute in LLE; every other boundary must be routed by this pass.
INTERPRETED = {0x0094d1, 0x00d345, 0x00d4ea, 0x00d507, 0x00d1ed}


def apply(source):
    output, found, pc = [], set(), 0
    for line in source.splitlines(keepends=True):
        if MARKER in line:
            continue
        output.append(line)
        if re.match(r'RecompReturn \w+\(CpuState \*cpu\) \{',line):
            pc = 0
        entry = re.search(r'cpu_trace_func_entry\(cpu, 0x([0-9A-Fa-f]{6}),',line)
        if entry:
            address = int(entry[1], 16)
            pc = address if address & 0x7fffff in TARGETS else 0
        if pc and 'g_cpu_entry_s[g_recomp_stack_top - 1] = _entry_s;' in line:
            output.append(f'  {MARKER} {{ extern bool MmxBossRushActive(void); if (MmxBossRushActive()) {{ RecompReturn r = interp_tier_dispatch_bank_miss(cpu, 0x{pc:06x}u, _entry_s, _hrv); RecompStackPop(); return r; }} }}\n')
            found.add(pc & 0x7fffff)
            pc = 0
    return ''.join(output), found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    found, changes = set(), []
    for path in args.gen_dir.glob('*.c'):
        source = path.read_text(encoding='utf-8')
        updated, entries = apply(source)
        found |= entries
        if updated != source:
            changes.append((path, updated))
    missing = TARGETS - found - INTERPRETED
    if missing:
        raise SystemExit('Missing Boss Rush boundaries: ' +
                         ', '.join(f'${pc:06X}' for pc in sorted(missing)))
    for path, updated in changes:
        path.write_text(updated, encoding='utf-8', newline='\n')
    print(f'MMX Boss Rush: routed {len(found)} native boundaries; '
          f'{len(TARGETS - found)} use existing interpreter dispatch')


if __name__ == '__main__':
    main()
