#!/usr/bin/env python3
"""Synthetic tests for the old Saber fixture-save converter."""

from __future__ import annotations

import struct
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONVERTER = ROOT / "tools" / "saber" / "convert_old_fixture.py"

OLD_SIZE = 318_903
CURRENT_SIZE = 318_683
SAVE0_SIZE = 317_575
MMXT_OFFSET = 297_411
MMXT_VERSION_OFFSET = 297_415
EXECUTION_SIZE_OFFSET = 299_035
RBRS_OFFSET = 299_039


def put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def synthetic_old() -> bytes:
    data = bytearray((index * 29 + 7) & 0xFF for index in range(OLD_SIZE))
    put_u32(data, 0, 0x52544C53)
    put_u32(data, 4, 10)
    put_u32(data, MMXT_OFFSET, 0x4D4D5854)
    put_u32(data, MMXT_VERSION_OFFSET, 16)
    put_u32(data, EXECUTION_SIZE_OFFSET, 18_536)
    put_u32(data, RBRS_OFFSET, 0x53524252)
    put_u32(data, RBRS_OFFSET + 4, 6)
    return bytes(data)


class ConvertOldFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="old-fixture-")
        self.root = Path(self.temp_dir.name)
        self.source = self.root / "source.sav"
        self.source.write_bytes(synthetic_old())

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def run_converter(self, source: Path, output: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-I", str(CONVERTER), str(source), str(output)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )

    def test_old_v16_conversion_is_exact_and_deterministic(self) -> None:
        expected = bytearray(self.source.read_bytes()[:CURRENT_SIZE])
        put_u32(expected, MMXT_VERSION_OFFSET, 13)

        first = self.root / "first.sav"
        second = self.root / "second.sav"
        result = self.run_converter(self.source, first)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        result = self.run_converter(self.source, second)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(first.read_bytes(), bytes(expected))
        self.assertEqual(second.read_bytes(), bytes(expected))
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(first.stat().st_size, CURRENT_SIZE)

    def test_wrong_size_magic_and_versions_are_rejected_without_output(self) -> None:
        valid = bytearray(self.source.read_bytes())
        mutations = {
            "wrong-size": bytes(valid[:-1]),
            "wrong-rtls-magic": self.mutate(valid, 0, 0),
            "wrong-rtls-version": self.mutate(valid, 4, 9),
            "wrong-mmxt-magic": self.mutate(valid, MMXT_OFFSET, 0),
            "wrong-mmxt-version": self.mutate(valid, MMXT_VERSION_OFFSET, 15),
            "wrong-execution-size": self.mutate(valid, EXECUTION_SIZE_OFFSET, 1),
            "wrong-rbrs-magic": self.mutate(valid, RBRS_OFFSET, 0),
            "wrong-rbrs-version": self.mutate(valid, RBRS_OFFSET + 4, 5),
        }
        for name, data in mutations.items():
            with self.subTest(name=name):
                source = self.root / f"{name}.sav"
                output = self.root / f"{name}.out.sav"
                source.write_bytes(data)
                result = self.run_converter(source, output)
                self.assertNotEqual(result.returncode, 0, msg=result.stdout)
                self.assertIn("convert_old_fixture.py:", result.stderr)
                self.assertFalse(output.exists(), result.stderr)

    def test_already_converted_and_save0_style_inputs_are_unchanged(self) -> None:
        converted = bytearray(self.source.read_bytes()[:CURRENT_SIZE])
        put_u32(converted, MMXT_VERSION_OFFSET, 13)
        converted_source = self.root / "converted.sav"
        converted_output = self.root / "converted-copy.sav"
        converted_source.write_bytes(converted)
        result = self.run_converter(converted_source, converted_output)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(converted_output.read_bytes(), bytes(converted))

        trailing_source = self.root / "converted-with-trailing.sav"
        trailing_output = self.root / "converted-with-trailing-copy.sav"
        trailing_source.write_bytes(bytes(converted) + b"T" * 220)
        result = self.run_converter(trailing_source, trailing_output)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertIn("input size mismatch", result.stderr)
        self.assertFalse(trailing_output.exists(), result.stderr)

        save0 = bytearray(self.source.read_bytes()[:SAVE0_SIZE])
        put_u32(save0, MMXT_VERSION_OFFSET, 3)
        save0_source = self.root / "save0.sav"
        save0_output = self.root / "save0-copy.sav"
        save0_source.write_bytes(save0)
        result = self.run_converter(save0_source, save0_output)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(save0_output.read_bytes(), bytes(save0))

    @staticmethod
    def mutate(data: bytearray, offset: int, value: int) -> bytes:
        result = bytearray(data)
        put_u32(result, offset, value)
        return bytes(result)


if __name__ == "__main__":
    unittest.main()
