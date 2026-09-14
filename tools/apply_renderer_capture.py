#!/usr/bin/env python3
"""Read-only pre-clipping sprite observation for the custom renderer.

Run after apply_overrides.py. Idempotent, and fail closed if D76A coverage
is missing: an executable without this hook silently loses margin sprites.
"""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-RENDER-PIECE*/'

def apply(text):
    text = ''.join(line for line in text.splitlines(keepends=True) if MARKER not in line)
    count = 0
    def inject(match):
        nonlocal count
        count += 1
        return match[0] + ('    /*MMX-RENDER-PIECE*/ { extern uint8_t g_ram[0x20000]; extern void MmxRendererRecordPiece(const uint8_t *, uint16_t); '
                           'MmxRendererRecordPiece(g_ram, cpu->D); }\n')
    # Inject after the deadline check, so a yielded/resumed block is observed
    # once. Do not affect registers, flags, OAM, CPU cycles or dispatch.
    text = re.sub(r'    cpu_trace_block\(cpu, 0x00D76A\);\n(?:(?!cpu_trace_block)[\s\S])*?'
                  r'    cpu->coprocessor_master_cycles = cpu->master_cycles;\n', inject, text)
    return text, count

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    count = 0
    for path in args.gen_dir.glob('*.c'):
        original = path.read_text(encoding='utf-8')
        if 'cpu_trace_block(cpu, 0x00D76A);' not in original:
            continue
        updated, sites = apply(original)
        count += sites
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='\n')
    if count < 2:
        raise SystemExit(f'Expected both D76A entry modes, found {count} capture sites')
    print(f'MMX custom renderer: {count} pre-clipping capture sites verified')

if __name__ == '__main__':
    main()
