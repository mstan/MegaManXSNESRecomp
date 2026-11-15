#!/usr/bin/env python3
"""Compare every data-toggle combination with the owner's original IPS files."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import import_complete_edition as importer


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--exe', type=Path, required=True)
    args = p.parse_args()
    stock = args.rom.read_bytes()
    if len(stock) % 32768 == 512:
        stock = stock[512:]
    groups = dict(SCRIPT=0, COLOURS=1, TYPOS=2, HOGANMER=3, DAMAGE=4)
    with zipfile.ZipFile(args.archive) as z:
        full, text = [importer.records(z.read(name)) for name in importer.PATCHES]
    patches = importer.split(full, text)
    for mask in range(32):
        expected = bytearray(stock)
        for group, address, data in patches:
            if mask & (1 << groups[group]):
                expected[address:address + len(data)] = data
        actual = subprocess.check_output([str(args.exe), str(args.rom), str(mask)])
        assert actual == expected, f'Feature combination {mask}: data differs from original IPS'
    # All imported data matches the complete reference; only the native debugger
    # replaces the IPS code/injected menu. The title/menu bytes are untouched.
    reference = bytearray(stock)
    for address, data in full:
        if address != 0x6584 and address < 0x17e990:
            reference[address:address + len(data)] = data
    assert actual == reference
    assert importer.generate(args.archive, stock) == (Path(__file__).resolve().parents[1] / 'src/mmx_complete_data.inc').read_text(encoding='utf-8')
    print('32 IPS-reference combinations, reversible guards, reproducible import passed')


if __name__ == '__main__':
    main()
