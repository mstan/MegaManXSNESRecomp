#!/usr/bin/env python3
"""Observe exact password UI points in native code, paired with LLE callbacks."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-PASSWORD*/'

def apply(text):
    text = ''.join(line for line in text.splitlines(keepends=True) if MARKER not in line)
    def inject(match):
        pc = match['pc']
        return match[0] + (f'    {MARKER} {{ extern void MmxPasswordHook(CpuState *, uint32_t); '
                           f'MmxPasswordHook(cpu, 0x{pc}); }}\n')
    return re.subn(r'    cpu_trace_block\(cpu, 0x(?P<pc>(?:00|80)(?:EF25|F05E))\);\n'
                  r'(?:(?!cpu_trace_block)[\s\S])*?'
                  r'    cpu->coprocessor_master_cycles = cpu->master_cycles;\n', inject, text)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    count = 0
    for path in args.gen_dir.glob('*.c'):
        original = path.read_text(encoding='utf-8')
        if not re.search(r'cpu_trace_block\(cpu, 0x(?:00|80)(?:EF25|F05E)\)', original):
            continue
        updated, sites = apply(original)
        count += sites
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='\n')
    print(f'MMX password saves: {count} native UI hooks; interpreter hooks cover other paths')

if __name__ == '__main__':
    main()
