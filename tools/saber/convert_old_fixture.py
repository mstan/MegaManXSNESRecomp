#!/usr/bin/env python3
"""Convert old Saber v16 fixture saves to the current MMXT v13 layout."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import sys
import tempfile


OLD_SIZE = 318_903
CURRENT_SIZE = 318_683
SAVE0_SIZE = 317_575

RTLS_MAGIC = 0x52544C53
RTLS_VERSION = 10
MMXT_MAGIC = 0x4D4D5854
OLD_MMXT_VERSION = 16
CURRENT_MMXT_VERSION = 13
SAVE0_MMXT_VERSION = 3
EXECUTION_SIZE = 18_536
RBRS_MAGIC = 0x53524252
RBRS_VERSION = 6

MMXT_OFFSET = 297_411
MMXT_VERSION_OFFSET = 297_415
EXECUTION_SIZE_OFFSET = 299_035
RBRS_OFFSET = 299_039


class ConversionError(ValueError):
    """A user-facing input validation error."""


def _u32(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def _require_u32(data: bytes, offset: int, expected: int, label: str) -> None:
    actual = _u32(data, offset)
    if actual != expected:
        shown = "missing" if actual is None else str(actual)
        raise ConversionError(
            f"{label} mismatch at {offset}: expected {expected}, got {shown}"
        )


def _validate_common(data: bytes, mmxt_version: int) -> None:
    _require_u32(data, 0, RTLS_MAGIC, "RTLS magic")
    _require_u32(data, 4, RTLS_VERSION, "RTLS version")
    _require_u32(data, MMXT_OFFSET, MMXT_MAGIC, "MMXT magic")
    _require_u32(data, MMXT_VERSION_OFFSET, mmxt_version, "MMXT version")
    _require_u32(data, EXECUTION_SIZE_OFFSET, EXECUTION_SIZE, "execution size")
    _require_u32(data, RBRS_OFFSET, RBRS_MAGIC, "RBRS magic")
    _require_u32(data, RBRS_OFFSET + 4, RBRS_VERSION, "RBRS version")


def _converted_bytes(data: bytes) -> bytes:
    if len(data) != OLD_SIZE:
        raise ConversionError(
            f"input size mismatch: expected {OLD_SIZE}, got {len(data)}"
        )
    _validate_common(data, OLD_MMXT_VERSION)
    result = bytearray(data[:CURRENT_SIZE])
    struct.pack_into("<I", result, MMXT_VERSION_OFFSET, CURRENT_MMXT_VERSION)
    if len(result) != CURRENT_SIZE:
        raise ConversionError(
            f"internal output size mismatch: expected {CURRENT_SIZE}, got {len(result)}"
        )
    return bytes(result)


def _classify_unchanged(data: bytes) -> bool:
    version = _u32(data, MMXT_VERSION_OFFSET)
    if version == CURRENT_MMXT_VERSION and len(data) != CURRENT_SIZE:
        raise ConversionError(
            f"input size mismatch for MMXT v{CURRENT_MMXT_VERSION}: "
            f"expected {CURRENT_SIZE}, got {len(data)}"
        )
    if version == SAVE0_MMXT_VERSION and len(data) != SAVE0_SIZE:
        raise ConversionError(
            f"input size mismatch for MMXT v{SAVE0_MMXT_VERSION}: "
            f"expected {SAVE0_SIZE}, got {len(data)}"
        )
    if len(data) == CURRENT_SIZE and version == CURRENT_MMXT_VERSION:
        _validate_common(data, CURRENT_MMXT_VERSION)
        return True
    if len(data) == SAVE0_SIZE and version == SAVE0_MMXT_VERSION:
        _validate_common(data, SAVE0_MMXT_VERSION)
        return True
    return False


def _publish(output: Path, data: bytes) -> None:
    parent = output.parent
    if not parent.exists():
        raise ConversionError(f"output directory does not exist: {parent}")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=parent,
            prefix=f".{output.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    except OSError as exc:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass
        raise ConversionError(f"cannot write {output}: {exc}") from exc


def convert(input_path: Path, output_path: Path) -> str:
    try:
        data = input_path.read_bytes()
    except OSError as exc:
        raise ConversionError(f"cannot read {input_path}: {exc}") from exc

    if _classify_unchanged(data):
        _publish(output_path, data)
        return "unchanged"

    converted = _converted_bytes(data)
    _publish(output_path, converted)
    return "converted"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert an old Saber MMXT v16 fixture save to MMXT v13."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        mode = convert(args.input, args.output)
    except ConversionError as exc:
        print(f"convert_old_fixture.py: {exc}", file=sys.stderr)
        return 1
    print(f"{mode}: {args.input} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
