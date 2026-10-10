#!/usr/bin/env python3
"""Synthetic, project-authored tests for convert_saber_zero.py."""

from __future__ import annotations

import argparse
import copy
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from statistics import median
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
CONVERTER = ROOT / "tools" / "saber" / "convert_saber_zero.py"
C_CHECK: Path | None = None


def canonical_manifest(obj: dict) -> bytes:
    return json.dumps(
        obj,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode()


class SaberConverterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="saber-converter-")
        self.root = Path(self.temp_dir.name)
        self.source_dir = self.root / "sources"
        self.source_dir.mkdir()
        self.source_path = self.source_dir / "synthetic_saber.png"
        self.manifest_path = self.root / "manifest.json"
        self._write_base_image()
        self.manifest = {
            "format": "mmx-saber-manifest",
            "version": 1,
            "sources": [
                {
                    "id": 7,
                    "file": "synthetic_saber.png",
                    "sha256": self._source_sha(),
                }
            ],
            "frames": [
                {
                    "id": 101,
                    "source": 7,
                    "body": {"rect": [0, 0, 4, 3], "origin": [-2, 3]},
                    "blade": {
                        "rect": [4, 0, 2, 2],
                        "origin": [2, 1],
                        "layer": "front",
                    },
                    "source_frame": 4,
                },
                {
                    "id": 102,
                    "source": 7,
                    "body": {"rect": [2, 1, 3, 2], "origin": [-1, 2]},
                    "source_frame": 5,
                },
            ],
            "animations": [
                {
                    "id": 42,
                    "facing_xor": 1,
                    "steps": [
                        {"frame": 101, "ticks": 2},
                        {"frame": 102, "ticks": 3},
                    ],
                }
            ],
        }
        self._write_manifest(self.manifest)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _write_base_image(self) -> None:
        image = Image.new("RGBA", (8, 4), (0, 0, 0, 0))
        image.putpixel((0, 0), (255, 0, 0, 255))
        image.putpixel((1, 0), (0, 255, 0, 255))
        image.putpixel((4, 0), (0, 0, 255, 255))
        image.putpixel((5, 0), (255, 0, 0, 255))
        image.save(
            self.source_path,
            format="PNG",
            pnginfo=None,
            optimize=False,
            compress_level=9,
        )

    def _source_sha(self) -> str:
        return hashlib.sha256(self.source_path.read_bytes()).hexdigest()

    def _write_manifest(self, manifest: dict) -> None:
        self.manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def _run_converter(
        self,
        manifest: dict,
        output: Path,
        preview_dir: Path | None = None,
        report: Path | None = None,
        check_c: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        self._write_manifest(manifest)
        command = [
            sys.executable,
            "-I",
            str(CONVERTER),
            "--manifest",
            str(self.manifest_path),
            "--source-dir",
            str(self.source_dir),
            "--out",
            str(output),
        ]
        if preview_dir is not None:
            command.extend(["--preview-dir", str(preview_dir)])
        if report is not None:
            command.extend(["--report", str(report)])
        result = subprocess.run(
            command,
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0 and check_c and C_CHECK is not None:
            expected_manifest_sha = hashlib.sha256(
                canonical_manifest(manifest)
            ).hexdigest()
            checked = subprocess.run(
                [
                    str(C_CHECK),
                    str(output),
                    expected_manifest_sha,
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                checked.returncode,
                0,
                msg=checked.stdout + checked.stderr,
            )
        return result

    def _assert_failure(
        self,
        manifest: dict,
        name: str,
        expected_message: str | None = None,
    ) -> None:
        output = self.root / f"{name}.bin"
        result = self._run_converter(
            manifest,
            output,
            check_c=False,
        )
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        if expected_message is not None:
            self.assertIn(expected_message, result.stderr)
        self.assertFalse(output.exists(), result.stderr)

    def _rewrite_image(self, image: Image.Image) -> None:
        image.save(
            self.source_path,
            format="PNG",
            pnginfo=None,
            optimize=False,
            compress_level=9,
        )
        self.manifest["sources"][0]["sha256"] = self._source_sha()

    def test_conversion_is_deterministic_and_hashes_match(self) -> None:
        output_one = self.root / "one.bin"
        output_two = self.root / "two.bin"
        preview_one = self.root / "preview-one"
        preview_two = self.root / "preview-two"
        report_one = self.root / "one.json"
        report_two = self.root / "two.json"

        first = self._run_converter(
            self.manifest,
            output_one,
            preview_one,
            report_one,
        )
        self.assertEqual(first.returncode, 0, msg=first.stderr)
        second = self._run_converter(
            self.manifest,
            output_two,
            preview_two,
            report_two,
        )
        self.assertEqual(second.returncode, 0, msg=second.stderr)

        self.assertEqual(output_one.read_bytes(), output_two.read_bytes())
        self.assertEqual(report_one.read_bytes(), report_two.read_bytes())
        preview_names = sorted(path.name for path in preview_one.iterdir())
        self.assertEqual(preview_names, ["animation_42.png"])
        self.assertEqual(
            (preview_one / "animation_42.png").read_bytes(),
            (preview_two / "animation_42.png").read_bytes(),
        )

        data = output_one.read_bytes()
        self.assertEqual(data[:8], b"MMXSABR\0")
        self.assertEqual(struct.unpack_from("<H", data, 8)[0], 1)
        self.assertEqual(struct.unpack_from("<H", data, 10)[0], 112)
        self.assertEqual(struct.unpack_from("<I", data, 12)[0], len(data))
        self.assertEqual(
            struct.unpack_from("<5H", data, 20),
            (1, 4, 1, 2, 2),
        )
        manifest_sha = hashlib.sha256(canonical_manifest(self.manifest)).digest()
        self.assertEqual(data[36:68], manifest_sha)
        hash_input = bytearray(data)
        hash_input[68:100] = b"\0" * 32
        self.assertEqual(data[68:100], hashlib.sha256(hash_input).digest())
        report = json.loads(report_one.read_text(encoding="utf-8"))
        self.assertEqual(report["frame_count"], 2)
        self.assertEqual(report["palette_size"], 4)
        self.assertEqual(
            report["output_sha256"],
            hashlib.sha256(data).hexdigest(),
        )
        self.assertEqual(
            [source["sha256"] for source in report["sources"]],
            [self.manifest["sources"][0]["sha256"]],
        )

    def test_synthetic_ride_atlas_preserves_origins_and_pose_animation(self) -> None:
        atlas_path = self.source_dir / "synthetic_ride.png"
        image = Image.new("RGBA", (28, 18), (17, 19, 23, 0))
        for pose in range(23):
            column, row = pose % 7, pose // 7
            image.putpixel(
                (column * 4 + 1, row * 3 + 1),
                ((pose * 31) % 256, (pose * 47) % 256, (pose * 61) % 256, 255),
            )
        # This transparent RGB value must not become a palette entry.
        image.putpixel((27, 17), (251, 3, 197, 0))
        image.save(atlas_path, format="PNG", pnginfo=None, optimize=False, compress_level=9)
        manifest = {
            "format": "mmx-saber-manifest",
            "version": 1,
            "sources": [{
                "id": 8,
                "file": "synthetic_ride.png",
                "sha256": hashlib.sha256(atlas_path.read_bytes()).hexdigest(),
            }],
            "frames": [
                {
                    "id": 1101 + pose,
                    "source": 8,
                    "body": {
                        "rect": [(pose % 7) * 4, (pose // 7) * 3, 4, 3],
                        "origin": [2, 1] if pose != 7 else [-3, 4],
                    },
                    "source_frame": pose,
                }
                for pose in range(23)
            ],
            "animations": [{
                "id": 0x6B,
                "facing_xor": 0,
                "steps": [
                    {"frame": 1101 + pose, "ticks": 1}
                    for pose in range(23)
                ],
            }],
        }
        first = self.root / "ride-one.bin"
        second = self.root / "ride-two.bin"
        result = self._run_converter(manifest, first)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        result = self._run_converter(manifest, second)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        data = first.read_bytes()
        self.assertEqual(struct.unpack_from("<5H", data, 20), (1, 24, 1, 23, 23))
        self.assertEqual(struct.unpack_from("<H", data, 10)[0], 112)
        section = 112 + 36
        section = (section + 24 * 2 + 3) & ~3
        animation = struct.unpack_from("<HHHH", data, section)
        self.assertEqual(animation[:4], (0x6B, 0, 23, 23))
        step_section = section + 16
        self.assertEqual(
            [struct.unpack_from("<HH", data, step_section + pose * 8)[1] for pose in range(23)],
            [1] * 23,
        )
        frame_section = step_section + 23 * 8
        self.assertEqual(struct.unpack_from("<hh", data, frame_section + 12), (2, 1))
        self.assertEqual(struct.unpack_from("<hh", data, frame_section + 7 * 40 + 12), (-3, 4))

    def test_production_sv2_origins_wv1_wall_order_and_dv1_dash_alignment_are_pinned(self) -> None:
        manifest_path = ROOT / "tools" / "saber" / "saber_zero_manifest.json"
        source_dir = ROOT / "assets" / "saber-zero" / "sprites"
        self.assertTrue(source_dir.is_dir(), source_dir)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        saber2_origins = [
            (frame["source_frame"], tuple(frame["body"]["origin"]))
            for frame in manifest["frames"]
            if frame["source"] == 2
        ]
        self.assertEqual(
            saber2_origins,
            [
                (0, (46, 49)),
                (1, (46, 47)),
                (2, (46, 45)),
                (3, (35, 45)),
                (4, (26, 45)),
                (5, (25, 45)),
                (6, (28, 45)),
                (7, (32, 45)),
                (8, (39, 45)),
                (9, (43, 45)),
                (10, (45, 45)),
                (11, (42, 45)),
                (12, (42, 44)),
                (13, (46, 45)),
                (14, (44, 44)),
            ],
        )
        dash_origins = [
            (frame["source_frame"], tuple(frame["body"]["origin"]))
            for frame in manifest["frames"]
            if frame["source"] == 6
        ]
        self.assertEqual(
            dash_origins,
            [
                (0, (46, 55)),
                (1, (46, 56)),
                (2, (46, 55)),
                (3, (46, 53)),
                (4, (46, 52)),
                (5, (45, 47)),
                (6, (33, 47)),
                (7, (37, 44)),
                (8, (37, 44)),
                (9, (39, 44)),
                (10, (44, 44)),
                (11, (42, 45)),
                (12, (42, 44)),
                (13, (46, 45)),
                (14, (44, 44)),
            ],
        )
        dash = next(
            animation for animation in manifest["animations"] if animation["id"] == 6
        )
        self.assertEqual(
            [step["frame"] for step in dash["steps"]], list(range(1068, 1083))
        )
        self.assertEqual([step["ticks"] for step in dash["steps"]], [2] * 15)

        # DV1 uses the renderer's effective right-facing convention: donor
        # facing_xor=1 is unmirrored, while legacy pose 0 is mirrored around
        # canvas column 63. The nine most frequent donor colors are Zero's
        # body; sparse blade/arc/hair colors are excluded from the columns.
        frames_by_key = {
            (frame["source"], frame["source_frame"]): frame
            for frame in manifest["frames"]
        }

        def source_file(source: int) -> Path:
            return source_dir / next(
                item["file"] for item in manifest["sources"] if item["id"] == source
            )

        def opaque_colors(frame: dict) -> set[tuple[int, int, int]]:
            x, y, width, height = frame["body"]["rect"]
            with Image.open(source_file(frame["source"])) as opened:
                image = opened.convert("RGBA")
                colors = Counter(
                    image.getpixel((x + column, y + row))[:3]
                    for row in range(height)
                    for column in range(width)
                    if image.getpixel((x + column, y + row))[3]
                )
            return {color for color, _ in colors.most_common(9)}

        def body_rows(frame: dict, colors: set[tuple[int, int, int]]) -> dict[int, list[int]]:
            x, y, width, height = frame["body"]["rect"]
            origin_x, origin_y = frame["body"]["origin"]
            rows: dict[int, list[int]] = {}
            with Image.open(source_file(frame["source"])) as opened:
                image = opened.convert("RGBA")
                for row in range(height):
                    columns = [
                        origin_x + column
                        for column in range(width)
                        if image.getpixel((x + column, y + row))[3]
                        and image.getpixel((x + column, y + row))[:3] in colors
                    ]
                    if columns:
                        rows[origin_y + row] = columns
            return rows

        idle_colors = opaque_colors(frames_by_key[(1, 0)])
        dash_final = body_rows(frames_by_key[(6, 14)], idle_colors)
        legacy_cache = (
            ROOT / "build-mingw" / "cache" / "mmx-source" / "x3-zero-v7.bin"
        ).read_bytes()
        idle_pose = legacy_cache[1812 : 1812 + 128 * 128]
        legacy_colors = {
            color for color, _ in Counter(pixel for pixel in idle_pose if pixel).most_common(9)
        }
        legacy_rows: dict[int, list[int]] = {}
        for row in range(128):
            columns = [
                127 - column
                for column in range(128)
                if idle_pose[row * 128 + column] in legacy_colors
            ]
            if columns:
                legacy_rows[row] = columns

        upper_limit = min(dash_final) + int(
            frames_by_key[(6, 14)]["body"]["rect"][3] * 0.65
        )
        midpoint_deltas = [
            sum(dash_final[row]) / len(dash_final[row])
            - sum(legacy_rows[row]) / len(legacy_rows[row])
            for row in dash_final
            if row <= upper_limit and row in legacy_rows
        ]
        self.assertTrue(midpoint_deltas)
        self.assertLessEqual(abs(median(midpoint_deltas)), 1.0)
        self.assertEqual(max(dash_final), max(legacy_rows))

        wall = next(
            animation for animation in manifest["animations"] if animation["id"] == 5
        )
        expected_wall_ids = list(range(1067, 1057, -1))
        self.assertEqual(
            [step["frame"] for step in wall["steps"]], expected_wall_ids
        )
        self.assertEqual([step["ticks"] for step in wall["steps"]], [2] * 10)

        output = self.root / "production.bin"
        result = subprocess.run(
            [
                sys.executable,
                "-I",
                str(CONVERTER),
                "--manifest",
                str(manifest_path),
                "--source-dir",
                str(source_dir),
                "--out",
                str(output),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        data = output.read_bytes()
        if C_CHECK is not None:
            checked = subprocess.run(
                [
                    str(C_CHECK),
                    str(output),
                    hashlib.sha256(canonical_manifest(manifest)).hexdigest(),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                checked.returncode,
                0,
                msg=checked.stdout + checked.stderr,
            )

        source_count, palette_count, animation_count, step_count, frame_count = (
            struct.unpack_from("<5H", data, 20)
        )
        section = 112 + source_count * 36
        section += palette_count * 2
        section = (section + 3) & ~3
        animation_section = section
        step_section = animation_section + animation_count * 16
        frame_section = step_section + step_count * 8
        animation5 = None
        for index in range(animation_count):
            record = struct.unpack_from(
                "<HHHH", data, animation_section + index * 16
            )
            if record[0] == 5:
                animation5 = record
                break
        self.assertIsNotNone(animation5)
        first_step, wall_step_count, wall_ticks = (
            animation5[1],
            animation5[2],
            animation5[3],
        )
        self.assertEqual((wall_step_count, wall_ticks), (10, 20))
        frame_indices = []
        durations = []
        for index in range(wall_step_count):
            frame_index, ticks = struct.unpack_from(
                "<HH", data, step_section + (first_step + index) * 8
            )
            frame_indices.append(frame_index)
            durations.append(ticks)
        self.assertEqual(
            [manifest["frames"][index]["id"] for index in frame_indices],
            expected_wall_ids,
        )
        self.assertEqual(durations, [2] * 10)

        self.assertEqual(frame_count, len(manifest["frames"]))
        for frame_index, expected in enumerate(saber2_origins):
            source_frame, origin = expected
            frame = manifest["frames"][
                next(
                    index
                    for index, candidate in enumerate(manifest["frames"])
                    if candidate["source"] == 2
                    and candidate["source_frame"] == source_frame
                )
            ]
            self.assertEqual(frame["body"]["origin"], list(origin))
            descriptor_offset = frame_section + manifest["frames"].index(frame) * 40
            self.assertEqual(
                struct.unpack_from("<hh", data, descriptor_offset + 12), origin
            )

    def test_wrong_source_sha256_leaves_existing_output_unchanged(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["sources"][0]["sha256"] = "0" * 64
        output = self.root / "existing.bin"
        sentinel = b"do not replace"
        output.write_bytes(sentinel)
        result = self._run_converter(bad, output, check_c=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), sentinel)

    def test_rect_outside_png_leaves_no_output(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["frames"][0]["body"]["rect"] = [7, 0, 2, 1]
        self._assert_failure(bad, "outside", "outside PNG bounds")

    def test_rect_width_257_leaves_no_output(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["frames"][0]["body"]["rect"] = [0, 0, 257, 1]
        self._assert_failure(bad, "wide", "dimensions must be 1..256")

    def test_duplicate_frame_id_leaves_no_output(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["frames"][1]["id"] = bad["frames"][0]["id"]
        self._assert_failure(bad, "duplicate-frame")

    def test_step_missing_frame_leaves_no_output(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["animations"][0]["steps"][0]["frame"] = 999
        self._assert_failure(bad, "missing-step-frame")

    def test_partial_alpha_without_threshold_leaves_no_output(self) -> None:
        with Image.open(self.source_path) as source:
            image = source.convert("RGBA")
        image.putpixel((0, 0), (255, 0, 0, 128))
        self._rewrite_image(image)
        self._assert_failure(self.manifest, "partial-alpha")

    def test_alpha_threshold_accepts_partial_alpha(self) -> None:
        with Image.open(self.source_path) as source:
            image = source.convert("RGBA")
        image.putpixel((0, 0), (255, 0, 0, 128))
        self._rewrite_image(image)
        manifest = copy.deepcopy(self.manifest)
        manifest["alpha_threshold"] = 128
        output = self.root / "threshold.bin"
        result = self._run_converter(manifest, output)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue(output.exists())

    def test_more_than_255_colors_leaves_no_output(self) -> None:
        image = Image.new("RGBA", (16, 16), (0, 0, 0, 255))
        for index in range(256):
            image.putpixel(
                (index % 16, index // 16),
                (index, (index * 37) % 256, (index * 91) % 256, 255),
            )
        self._rewrite_image(image)
        bad = copy.deepcopy(self.manifest)
        bad["frames"][0]["body"]["rect"] = [0, 0, 16, 16]
        bad["frames"][1]["body"]["rect"] = [0, 0, 1, 1]
        self._assert_failure(bad, "too-many-colors")

    def test_explicit_palette_mismatch_leaves_no_output(self) -> None:
        bad = copy.deepcopy(self.manifest)
        bad["palette"] = ["#ff0000"]
        self._assert_failure(bad, "palette-mismatch")


def main() -> int:
    global C_CHECK
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--c-check", type=Path)
    arguments, unittest_arguments = parser.parse_known_args()
    C_CHECK = arguments.c_check
    result = unittest.main(
        argv=[sys.argv[0], *unittest_arguments],
        exit=False,
    )
    return 0 if result.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
