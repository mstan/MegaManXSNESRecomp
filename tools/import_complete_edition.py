#!/usr/bin/env python3
"""Split CF's Complete Edition 1.0 data into independent stock-ROM mods.

The supplied IPS files are reference inputs, never a new compiled ROM variant.
Only dialogue and table data are imported. The debugger is implemented in C.
"""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROM_SHA256 = 'b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32'
PATCHES = {
    'Mega Man X Complete Edition Including Gameplay Changes.ips':
        '88ffeaa1e76dd344683d76654883d61c9301dfdc8629a2a18dd5e847de2d1328',
    'Mega Man X Complete Edition With Text Changes Only.ips':
        '1df5ab6a1064fce0fead1351d3d5146494dbcfc7da0a3bca20f8258e4da6d5e5',
}


def records(data):
    if data[:5] != b'PATCH':
        raise ValueError('Not an IPS file')
    cursor, result = 5, []
    while data[cursor:cursor + 3] != b'EOF':
        address = int.from_bytes(data[cursor:cursor + 3], 'big')
        size = int.from_bytes(data[cursor + 3:cursor + 5], 'big')
        cursor += 5
        if size:
            payload = data[cursor:cursor + size]
            cursor += size
        else:
            size = int.from_bytes(data[cursor:cursor + 2], 'big')
            payload = data[cursor + 2:cursor + 3] * size
            cursor += 3
        if len(payload) != size or not size:
            raise ValueError('Truncated IPS record')
        result.append((address, payload))
    if cursor + 3 != len(data):
        raise ValueError('Unexpected IPS trailer')
    return result


def fingerprint(data):
    result = 2166136261
    for byte in data:
        result = ((result ^ byte) * 16777619) & 0xffffffff
    return result


def split(full, text):
    result = []
    for address, payload in text:
        if 0xc0 <= payload[0] <= 0xc7:
            result.append(('COLOURS', address, payload[:1]))
            address, payload = address + 1, payload[1:]
        if payload:
            result.append(('TYPOS' if address == 0x24b71 else 'SCRIPT', address, payload))
    for address, payload in full:
        if address == 0x31c5b:
            result.append(('TYPOS', address, payload))
        elif address == 0x36fcb:
            result.append(('HOGANMER', address, payload))
        elif address in (0x370ab, 0x3717d, 0x3719d):
            result.append(('DAMAGE', address, payload))
        elif (address, payload) not in text and address != 0x6584 and address < 0x17e990:
            raise ValueError(f'Unclassified gameplay record {address:06x}')
    return result


def generate(archive, rom):
    if len(rom) % 0x8000 == 512:
        rom = rom[512:]
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError('Expected an unmodified USA Rev 1 ROM')
    inputs = []
    with zipfile.ZipFile(archive) as source:
        for name, expected in PATCHES.items():
            data = source.read(name)
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f'Unexpected Complete Edition patch: {name}')
            inputs.append(records(data))
    rows = []
    for feature, address, data in split(*inputs):
        if data == rom[address:address + len(data)]:
            continue
        encoded = ''.join(f'\\x{b:02x}' for b in data)
        rows.append(f'  {{MMX_COMPLETE_{feature}, 0x{address:06x}, {len(data)}, '
                    f'0x{fingerprint(rom[address:address + len(data)]):08x}u, "{encoded}"}},')
    return ('/* Imported data from CF / Windii Gitlord, Complete Edition 1.0.\n'
            ' * See docs/complete-edition.md for provenance and attribution.\n'
            ' * Regenerate with tools/import_complete_edition.py. No guest code. */\n'
            'static const CompletePatch patches[] = {\n' + '\n'.join(rows) + '\n};\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'src/mmx_complete_data.inc')
    args = parser.parse_args()
    args.out.write_text(generate(args.archive, args.rom.read_bytes()), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
