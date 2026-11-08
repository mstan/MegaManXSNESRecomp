#!/usr/bin/env python3
"""Scale the final player hurt operand after native armor reduction."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-DAMAGE*/'
REQUIRED = {0x049d50, 0x849d50, 0x849f59}


def apply(text):
    output, found, pc = [], set(), 0
    for line in text.splitlines(keepends=True):
        if MARKER in line:
            continue
        output.append(line)
        block = re.search(r'cpu_trace_block\(cpu, 0x([0-9a-fA-F]+)\)', line)
        if block:
            pc = int(block[1], 16)
        if (pc & 0x7fffff) not in (0x049d50, 0x049f59):
            continue
        load = re.search(r'uint8 (_v\d+) = cpu_read8\(cpu, cpu->DB, \(uint16\)\(0x0000\)\);', line)
        if load:
            lethal = 'g_ram[(cpu->D + 0x26) & 65535]' if (pc & 65535) == 0x9d50 else 'g_ram[0xbce]'
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxDamageReceived(unsigned, bool); {load[1]} = (uint8)MmxDamageReceived({load[1]}, {lethal} >= 127); }}\n')
            found.add(pc)
    return ''.join(output), found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    found = set()
    for path in args.gen_dir.glob('bank*04*_v2.c'):
        updated, hits = apply(path.read_text(encoding='utf-8'))
        found |= hits
        if updated != path.read_text(encoding='utf-8'):
            path.write_text(updated, encoding='utf-8', newline='\n')
    for path in args.gen_dir.glob('bank84*_v2.c'):
        original = path.read_text(encoding='utf-8')
        updated, hits = apply(original)
        found |= hits
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='\n')
    if REQUIRED - found:
        raise SystemExit(f'Missing damage operand hooks: {REQUIRED - found}')
    print(f'MMX damage: {len(found)} native operand blocks verified')


if __name__ == '__main__':
    main()
