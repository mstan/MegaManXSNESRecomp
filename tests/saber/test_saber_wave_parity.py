#!/usr/bin/env python3
"""Compare the Saber-owned native and Python X3 wave extractors."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


EXPECTED = "65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7"


def normalized_bytes(path):
    data = Path(path).read_bytes()
    if len(data) % 32768 == 512:
        data = data[512:]
    return data


def find_rom(root):
    candidates = []
    configured = os.environ.get("MMX_SABER_X3_ROM")
    if configured:
        candidates.append(Path(configured))
    candidates.extend(root / name for name in (
        "Mega Man X3 (USA).sfc",
        "Mega Man X3 (USA).smc",
        "Mega Man X3.sfc",
        "Mega Man X3.smc",
    ))
    for candidate in candidates:
        if not candidate.is_file():
            continue
        if hashlib.sha256(normalized_bytes(candidate)).hexdigest() == EXPECTED:
            return candidate
    return None


def run(command, cwd):
    subprocess.run(command, cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    rom = find_rom(root)
    if rom is None:
        print("SKIP saber_wave_parity: normalized X3 USA ROM is absent")
        return 0

    tool = root / "tools" / "saber" / "extract_saber_wave.py"
    with tempfile.TemporaryDirectory(prefix="mmx-saber-wave-parity-") as directory:
        directory = Path(directory)
        native = directory / "native-wave.bin"
        python = directory / "python-wave.bin"
        run([str(args.native.resolve()), str(rom), str(native)], root)
        run([sys.executable, "-I", str(tool), str(rom), str(python)], root)
        native_bytes = native.read_bytes()
        python_bytes = python.read_bytes()
        if native_bytes != python_bytes:
            raise AssertionError("MMXZWAV1 native/Python byte mismatch")
        print("PASS native/Python MMXZWAV1 parity SHA-256 "
              f"{hashlib.sha256(native_bytes).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
