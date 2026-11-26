#!/usr/bin/env python3
"""Read-only pre-clipping sprite observation for the custom renderer.

Run after apply_overrides.py. Idempotent, and fail closed if D76A coverage
is missing: an executable without this hook silently loses margin sprites.
"""
import argparse
import json
from pathlib import Path
import re

MARKER = '/*MMX-RENDER-PIECE*/'

def observed_modes(text):
    modes, current = set(), None
    for line in text.splitlines():
        label = re.match(r'\s*L_D76A_M([01])X([01]):', line)
        if label:
            current = (int(label[1]), int(label[2]))
        elif 'cpu_trace_block(' in line and 'D76A' not in line:
            current = None
        if current is not None and MARKER in line and 'MmxRendererRecordPiece(' in line:
            modes.add(current)
    return modes

def validate_modes(expected, observed, count):
    if not expected or expected - observed or count < len(expected):
        raise ValueError(f'D76A native capture coverage missing: expected {sorted(expected)}, observed {sorted(observed)}, sites {count}')

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
    def observe(match):
        return match[0] + ('    /*MMX-RENDER-PIECE*/ { extern uint8_t g_ram[0x20000]; extern void MmxRendererObserveObject(const uint8_t *, uint16_t); '
                           'MmxRendererObserveObject(g_ram, (uint16_t)(cpu->D + cpu->X)); }\n')
    text, objects = re.subn(r'    cpu_trace_block\(cpu, 0x00D6A7\);\n(?:(?!cpu_trace_block)[\s\S])*?'
                  r'    cpu->coprocessor_master_cycles = cpu->master_cycles;\n', observe, text)
    if count and objects != count:
        raise ValueError(f'D6A7/D76A mode coverage differs: {objects}/{count}')
    return text, count

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    count = 0
    observed = set()
    for path in args.gen_dir.glob('*.c'):
        original = path.read_text(encoding='utf-8')
        if 'cpu_trace_block(cpu, 0x00D76A);' not in original:
            continue
        updated, sites = apply(original)
        observed.update(observed_modes(updated))
        count += sites
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='\n')
    manifest = json.loads((args.gen_dir / 'program_manifest.json').read_text())
    expected = {(int(key[-3]), int(key[-1])) for key, node in manifest['nodes'].items()
                if (int(key.split(':')[0], 16) & 0x7fffff) == 0x00d76a
                and node['disposition'] == 'aot_eligible'}
    validate_modes(expected, observed, count)
    print(f'MMX custom renderer: {count} pre-clipping capture sites verified')

if __name__ == '__main__':
    main()
