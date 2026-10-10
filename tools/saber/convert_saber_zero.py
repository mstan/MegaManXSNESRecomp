#!/usr/bin/env python3
"""Build a deterministic Saber sidecar v1 from PNGs and a JSON manifest.

The command line is:

    python tools/convert_saber_zero.py --manifest M.json --source-dir DIR
        --out OUT.bin [--preview-dir PDIR] [--report REPORT.json]

The manifest is a project-authored JSON object with this schema:

    {
      "format": "mmx-saber-manifest",
      "version": 1,
      "sources": [
        {"id": 1, "file": "Saber_1.png", "sha256": "<64 hex digits>"}
      ],
      "palette": ["#RRGGBB", "..."],       // optional, index 1 onward
      "alpha_threshold": 128,              // optional, 1..255
      "frames": [
        {
          "id": 10,
          "source": 1,
          "body": {"rect": [x, y, w, h], "origin": [ox, oy]},
          "blade": {
            "rect": [x, y, w, h],
            "origin": [ox, oy],
            "layer": "front"                // or "behind"
          },
          "source_frame": 0
        }
      ],
      "animations": [
        {
          "id": 42,
          "facing_xor": 0,
          "steps": [{"frame": 10, "ticks": 3}]
        }
      ]
    }

Frame order is manifest order; frame IDs and animation step frame IDs are
resolved to those indices. Animation total_ticks is the sum of its step
durations. Source files are hashed as raw bytes before Pillow decodes them.
The manifest digest stored in the sidecar is SHA-256 of:

    json.dumps(obj, sort_keys=True, separators=(",", ":"),
               ensure_ascii=True).encode()

with obj being the parsed JSON object. The whole-file digest is SHA-256 of
the complete sidecar after zeroing header bytes 68 through 99, exactly as the
committed C parser does.

Alpha 0 is transparent and alpha 255 is opaque. Other alpha values fail
unless alpha_threshold is present; then alpha >= threshold is opaque and the
rest is transparent. Transparent RGB values do not enter the palette. An
explicit palette requires every opaque RGB triple to match an entry. Without
one, unique opaque RGB triples are sorted by (R, G, B). RGB candidates are
converted to SNES BGR555, and collisions are deduplicated after conversion.
The retained candidate is the first candidate in the input order, and the
remaining BGR555 values are sorted numerically. Thus an explicit palette uses
manifest order as its first-occurrence order, while an inferred palette uses
RGB-sorted order.

Only the standard library and Pillow are required. All validation, decoding,
preview rendering, and report construction happen before outputs are
published. Each output is staged beside its destination and replaced
atomically after successful construction.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
from typing import Any, Optional

try:
    from PIL import Image
except ImportError:  # pragma: no cover - exercised only on an incomplete host
    Image = None  # type: ignore[assignment]


MAX_FILE_BYTES = 64 * 1024 * 1024
HEADER_BYTES = 112
WHOLE_SHA_OFFSET = 68
WHOLE_SHA_BYTES = 32
HEX_SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")
RGB_HEX = re.compile(r"^#[0-9a-fA-F]{6}$")


class ConversionError(ValueError):
    """A user-facing manifest/source/output error."""


@dataclass(frozen=True)
class SourceSpec:
    source_id: int
    file_name: str
    sha256: str


@dataclass(frozen=True)
class PlaneSpec:
    rect: tuple[int, int, int, int]
    origin: tuple[int, int]
    layer: int
    layer_name: Optional[str]


@dataclass(frozen=True)
class FrameSpec:
    frame_id: int
    source_id: int
    body: PlaneSpec
    blade: Optional[PlaneSpec]
    source_frame: int


@dataclass(frozen=True)
class StepSpec:
    frame_id: int
    ticks: int


@dataclass(frozen=True)
class AnimationSpec:
    animation_id: int
    facing_xor: int
    steps: tuple[StepSpec, ...]


@dataclass(frozen=True)
class ManifestSpec:
    sources: tuple[SourceSpec, ...]
    palette_rgb: Optional[tuple[tuple[int, int, int], ...]]
    alpha_threshold: Optional[int]
    frames: tuple[FrameSpec, ...]
    animations: tuple[AnimationSpec, ...]


@dataclass
class SourceAsset:
    spec: SourceSpec
    image: Any


@dataclass(frozen=True)
class PreparedPlane:
    spec: PlaneSpec
    rgb_pixels: tuple[Optional[tuple[int, int, int]], ...]


@dataclass(frozen=True)
class PreparedFrame:
    spec: FrameSpec
    body: PreparedPlane
    blade: Optional[PreparedPlane]


@dataclass(frozen=True)
class EncodedPlane:
    spec: PlaneSpec
    indices: bytes
    pixel_offset: int


@dataclass(frozen=True)
class EncodedFrame:
    spec: FrameSpec
    body: EncodedPlane
    blade: Optional[EncodedPlane]


def _error(message: str) -> None:
    raise ConversionError(message)


def _require(condition: bool, message: str) -> None:
    if not condition:
        _error(message)


def _ensure_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        _error(f"{context} must be an object")
    return value


def _ensure_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        _error(f"{context} must be an array")
    return value


def _ensure_keys(
    value: dict[str, Any],
    required: set[str],
    optional: set[str],
    context: str,
) -> None:
    keys = set(value)
    missing = sorted(required - keys)
    unknown = sorted(keys - required - optional)
    if missing:
        _error(f"{context} missing {missing[0]}")
    if unknown:
        _error(f"{context} has unknown field {unknown[0]}")


def _integer(value: Any, context: str) -> int:
    if type(value) is not int:
        _error(f"{context} must be an integer")
    return value


def _u16(value: Any, context: str, nonzero: bool = False) -> int:
    value = _integer(value, context)
    if value < (1 if nonzero else 0) or value > 0xFFFF:
        _error(f"{context} is outside u16 range")
    return value


def _i16(value: Any, context: str) -> int:
    value = _integer(value, context)
    if value < -0x8000 or value > 0x7FFF:
        _error(f"{context} is outside i16 range")
    return value


def _parse_rect(value: Any, context: str) -> tuple[int, int, int, int]:
    values = _ensure_list(value, f"{context}.rect")
    if len(values) != 4:
        _error(f"{context}.rect must have four integers")
    x, y, width, height = (
        _integer(item, f"{context}.rect[{index}]")
        for index, item in enumerate(values)
    )
    if x < 0 or y < 0:
        _error(f"{context}.rect origin must be nonnegative")
    if width < 1 or width > 256 or height < 1 or height > 256:
        _error(f"{context}.rect dimensions must be 1..256")
    return x, y, width, height


def _parse_origin(value: Any, context: str) -> tuple[int, int]:
    values = _ensure_list(value, f"{context}.origin")
    if len(values) != 2:
        _error(f"{context}.origin must have two integers")
    return (
        _i16(values[0], f"{context}.origin[0]"),
        _i16(values[1], f"{context}.origin[1]"),
    )


def _parse_plane(value: Any, context: str, blade: bool) -> PlaneSpec:
    plane = _ensure_object(value, context)
    required = {"rect", "origin"}
    optional = {"layer"} if blade else set()
    _ensure_keys(plane, required, optional, context)
    rect = _parse_rect(plane["rect"], context)
    origin = _parse_origin(plane["origin"], context)
    if not blade:
        return PlaneSpec(rect, origin, 0, None)
    layer_name = plane["layer"]
    if layer_name not in ("front", "behind"):
        _error(f"{context}.layer must be front or behind")
    return PlaneSpec(
        rect,
        origin,
        2 if layer_name == "front" else 1,
        layer_name,
    )


def _parse_rgb(value: Any, context: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not RGB_HEX.fullmatch(value):
        _error(f"{context} must be #RRGGBB")
    return int(value[1:3], 16), int(value[3:5], 16), int(value[5:7], 16)


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"invalid JSON constant {value}")


def _object_pairs_without_duplicates(
    pairs: list[tuple[str, Any]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key}")
        result[key] = value
    return result


def _canonical_manifest_bytes(obj: dict[str, Any]) -> bytes:
    return json.dumps(
        obj,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode()


def _load_manifest(path: Path) -> tuple[ManifestSpec, bytes]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        _error(f"cannot read manifest: {exc}")
    try:
        obj = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_pairs_without_duplicates,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        _error(f"invalid manifest JSON: {exc}")
    obj = _ensure_object(obj, "manifest")
    _ensure_keys(
        obj,
        {"format", "version", "sources", "frames", "animations"},
        {"palette", "alpha_threshold"},
        "manifest",
    )
    if obj["format"] != "mmx-saber-manifest":
        _error("manifest format mismatch")
    if type(obj["version"]) is not int or obj["version"] != 1:
        _error("manifest version mismatch")

    source_values = _ensure_list(obj["sources"], "manifest.sources")
    if not 1 <= len(source_values) <= 32:
        _error("manifest source count must be 1..32")
    sources: list[SourceSpec] = []
    source_ids: set[int] = set()
    for index, raw_source in enumerate(source_values):
        context = f"source {index}"
        source = _ensure_object(raw_source, context)
        _ensure_keys(source, {"id", "file", "sha256"}, set(), context)
        source_id = _u16(source["id"], f"{context}.id", nonzero=True)
        if source_id in source_ids:
            _error(f"duplicate source id {source_id}")
        source_ids.add(source_id)
        file_name = source["file"]
        if not isinstance(file_name, str) or not file_name:
            _error(f"{context}.file must be a nonempty string")
        sha256 = source["sha256"]
        if not isinstance(sha256, str) or not HEX_SHA256.fullmatch(sha256):
            _error(f"{context}.sha256 must be 64 hex digits")
        sources.append(SourceSpec(source_id, file_name, sha256.lower()))

    palette_rgb: Optional[tuple[tuple[int, int, int], ...]] = None
    if "palette" in obj:
        palette_values = _ensure_list(obj["palette"], "manifest.palette")
        if not 1 <= len(palette_values) <= 255:
            _error("manifest.palette must contain 1..255 colors")
        palette_rgb = tuple(
            _parse_rgb(value, f"manifest.palette[{index}]")
            for index, value in enumerate(palette_values)
        )

    alpha_threshold: Optional[int] = None
    if "alpha_threshold" in obj:
        alpha_threshold = _integer(
            obj["alpha_threshold"], "manifest.alpha_threshold"
        )
        if not 1 <= alpha_threshold <= 255:
            _error("manifest.alpha_threshold must be 1..255")

    frame_values = _ensure_list(obj["frames"], "manifest.frames")
    if not 1 <= len(frame_values) <= 1024:
        _error("manifest frame count must be 1..1024")
    frames: list[FrameSpec] = []
    frame_ids: set[int] = set()
    for index, raw_frame in enumerate(frame_values):
        context = f"frame {index}"
        frame = _ensure_object(raw_frame, context)
        _ensure_keys(
            frame,
            {"id", "source", "body", "source_frame"},
            {"blade"},
            context,
        )
        frame_id = _u16(frame["id"], f"{context}.id", nonzero=True)
        if frame_id in frame_ids:
            _error(f"duplicate frame id {frame_id}")
        frame_ids.add(frame_id)
        source_id = _u16(frame["source"], f"{context}.source", nonzero=True)
        if source_id not in source_ids:
            _error(f"{context}.source references missing source")
        body = _parse_plane(frame["body"], f"{context}.body", blade=False)
        blade = (
            _parse_plane(frame["blade"], f"{context}.blade", blade=True)
            if "blade" in frame
            else None
        )
        source_frame = _u16(
            frame["source_frame"], f"{context}.source_frame", nonzero=False
        )
        frames.append(FrameSpec(frame_id, source_id, body, blade, source_frame))

    animation_values = _ensure_list(obj["animations"], "manifest.animations")
    if not 1 <= len(animation_values) <= 64:
        _error("manifest animation count must be 1..64")
    animations: list[AnimationSpec] = []
    animation_ids: set[int] = set()
    total_steps = 0
    for index, raw_animation in enumerate(animation_values):
        context = f"animation {index}"
        animation = _ensure_object(raw_animation, context)
        _ensure_keys(animation, {"id", "facing_xor", "steps"}, set(), context)
        animation_id = _u16(
            animation["id"], f"{context}.id", nonzero=True
        )
        if animation_id in animation_ids:
            _error(f"duplicate animation id {animation_id}")
        animation_ids.add(animation_id)
        facing_xor = _integer(
            animation["facing_xor"], f"{context}.facing_xor"
        )
        if facing_xor not in (0, 1):
            _error(f"{context}.facing_xor must be 0 or 1")
        step_values = _ensure_list(animation["steps"], f"{context}.steps")
        if not step_values:
            _error(f"{context}.steps must not be empty")
        steps: list[StepSpec] = []
        total_ticks = 0
        for step_index, raw_step in enumerate(step_values):
            step_context = f"{context}.steps[{step_index}]"
            step = _ensure_object(raw_step, step_context)
            _ensure_keys(step, {"frame", "ticks"}, set(), step_context)
            frame_id = _u16(
                step["frame"], f"{step_context}.frame", nonzero=True
            )
            if frame_id not in frame_ids:
                _error(f"{step_context}.frame references missing frame")
            ticks = _integer(step["ticks"], f"{step_context}.ticks")
            if not 1 <= ticks <= 0xFFFF:
                _error(f"{step_context}.ticks must be 1..65535")
            total_ticks += ticks
            if total_ticks > 0xFFFF:
                _error(f"{context} total ticks overflow")
            steps.append(StepSpec(frame_id, ticks))
        total_steps += len(steps)
        if total_steps > 4096:
            _error("manifest step count must be 1..4096")
        animations.append(AnimationSpec(animation_id, facing_xor, tuple(steps)))

    return (
        ManifestSpec(
            tuple(sources),
            palette_rgb,
            alpha_threshold,
            tuple(frames),
            tuple(animations),
        ),
        _canonical_manifest_bytes(obj),
    )


def _source_path(source_dir: Path, file_name: str) -> Path:
    path = Path(file_name)
    if path.is_absolute():
        _error("source file must be relative to --source-dir")
    try:
        base = source_dir.resolve()
        resolved = (source_dir / path).resolve()
        resolved.relative_to(base)
    except (OSError, ValueError) as exc:
        _error(f"source file escapes --source-dir: {file_name}: {exc}")
    return resolved


def _load_sources(
    source_dir: Path, specs: tuple[SourceSpec, ...]
) -> dict[int, SourceAsset]:
    if Image is None:
        _error("Pillow is required")
    assets: dict[int, SourceAsset] = {}
    for spec in specs:
        path = _source_path(source_dir, spec.file_name)
        try:
            raw = path.read_bytes()
        except OSError as exc:
            _error(f"cannot read source {spec.file_name}: {exc}")
        actual_sha = hashlib.sha256(raw).hexdigest()
        if actual_sha != spec.sha256:
            _error(f"source sha256 mismatch for {spec.file_name}")
        try:
            with Image.open(io.BytesIO(raw)) as decoded:
                if decoded.format != "PNG":
                    _error(f"source is not PNG: {spec.file_name}")
                decoded.load()
                image = decoded.convert("RGBA")
        except ConversionError:
            raise
        except Exception as exc:
            _error(f"cannot decode PNG {spec.file_name}: {exc}")
        assets[spec.source_id] = SourceAsset(spec, image)
    return assets


def _extract_plane(
    source: SourceAsset,
    spec: PlaneSpec,
    alpha_threshold: Optional[int],
    explicit_palette: Optional[set[tuple[int, int, int]]],
    context: str,
) -> PreparedPlane:
    x, y, width, height = spec.rect
    image_width, image_height = source.image.size
    if x > image_width - width or y > image_height - height:
        _error(f"{context} rect outside PNG bounds")
    pixels = source.image.load()
    result: list[Optional[tuple[int, int, int]]] = []
    for row in range(y, y + height):
        for column in range(x, x + width):
            red, green, blue, alpha = pixels[column, row]
            if alpha_threshold is None:
                if alpha == 0:
                    result.append(None)
                elif alpha == 255:
                    rgb = (red, green, blue)
                    if explicit_palette is not None and rgb not in explicit_palette:
                        _error(f"{context} has RGB outside explicit palette")
                    result.append(rgb)
                else:
                    _error(f"{context} has partial alpha without threshold")
            elif alpha >= alpha_threshold:
                rgb = (red, green, blue)
                if explicit_palette is not None and rgb not in explicit_palette:
                    _error(f"{context} has RGB outside explicit palette")
                result.append(rgb)
            else:
                result.append(None)
    return PreparedPlane(spec, tuple(result))


def _prepare_frames(
    manifest: ManifestSpec, sources: dict[int, SourceAsset]
) -> tuple[PreparedFrame, ...]:
    explicit_palette = (
        set(manifest.palette_rgb) if manifest.palette_rgb is not None else None
    )
    prepared: list[PreparedFrame] = []
    for index, frame in enumerate(manifest.frames):
        source = sources[frame.source_id]
        body = _extract_plane(
            source,
            frame.body,
            manifest.alpha_threshold,
            explicit_palette,
            f"frame {index} body",
        )
        blade = (
            _extract_plane(
                source,
                frame.blade,
                manifest.alpha_threshold,
                explicit_palette,
                f"frame {index} blade",
            )
            if frame.blade is not None
            else None
        )
        prepared.append(PreparedFrame(frame, body, blade))
    return tuple(prepared)


def _bgr555(rgb: tuple[int, int, int]) -> int:
    red, green, blue = rgb
    return (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10)


def _make_palette(
    manifest: ManifestSpec, frames: tuple[PreparedFrame, ...]
) -> tuple[tuple[int, ...], dict[tuple[int, int, int], int]]:
    if manifest.palette_rgb is not None:
        candidates = list(manifest.palette_rgb)
    else:
        colors: set[tuple[int, int, int]] = set()
        for frame in frames:
            for plane in (frame.body, frame.blade):
                if plane is not None:
                    colors.update(
                        pixel for pixel in plane.rgb_pixels if pixel is not None
                    )
        if len(colors) > 255:
            _error("more than 255 opaque RGB colors")
        candidates = sorted(colors)
    if not candidates:
        _error("palette has no opaque color")
    if len(candidates) > 255:
        _error("palette has more than 255 colors")

    first_by_bgr: dict[int, tuple[int, tuple[int, int, int]]] = {}
    for order, rgb in enumerate(candidates):
        value = _bgr555(rgb)
        if value not in first_by_bgr:
            first_by_bgr[value] = (order, rgb)
    ordered_bgr = sorted(
        first_by_bgr,
        key=lambda value: (value, first_by_bgr[value][0]),
    )
    if len(ordered_bgr) > 255:
        _error("palette has more than 255 BGR555 colors")
    color_to_index: dict[tuple[int, int, int], int] = {}
    for index, value in enumerate(ordered_bgr, start=1):
        for _, rgb in (first_by_bgr[value],):
            color_to_index[rgb] = index
    # Colliding RGB candidates map to the same BGR555 index.
    for rgb in candidates:
        color_to_index[rgb] = 1 + ordered_bgr.index(_bgr555(rgb))
    return (0, *ordered_bgr), color_to_index


def _encode_plane(
    plane: PreparedPlane,
    color_to_index: dict[tuple[int, int, int], int],
    pixel_offset: int,
) -> EncodedPlane:
    indices = bytearray()
    for pixel in plane.rgb_pixels:
        if pixel is None:
            indices.append(0)
        else:
            try:
                indices.append(color_to_index[pixel])
            except KeyError:
                _error("opaque pixel missing from palette")
    return EncodedPlane(plane.spec, bytes(indices), pixel_offset)


def _pack_output(
    manifest: ManifestSpec,
    manifest_bytes: bytes,
    prepared_frames: tuple[PreparedFrame, ...],
    palette: tuple[int, ...],
    color_to_index: dict[tuple[int, int, int], int],
) -> tuple[bytes, tuple[EncodedFrame, ...], tuple[tuple[int, int], ...]]:
    frame_indices = {
        frame.frame_id: index
        for index, frame in enumerate(manifest.frames)
    }
    encoded_frames: list[EncodedFrame] = []
    pixel_blob = bytearray()
    for frame in prepared_frames:
        body = _encode_plane(frame.body, color_to_index, len(pixel_blob))
        pixel_blob.extend(body.indices)
        blade: Optional[EncodedPlane] = None
        if frame.blade is not None:
            blade = _encode_plane(frame.blade, color_to_index, len(pixel_blob))
            pixel_blob.extend(blade.indices)
        encoded_frames.append(EncodedFrame(frame.spec, body, blade))

    step_records: list[tuple[int, int]] = []
    animation_records: list[tuple[AnimationSpec, int, int, int]] = []
    for animation in manifest.animations:
        first_step = len(step_records)
        total_ticks = 0
        for step in animation.steps:
            step_records.append((frame_indices[step.frame_id], step.ticks))
            total_ticks += step.ticks
        animation_records.append(
            (
                animation,
                first_step,
                len(animation.steps),
                total_ticks,
            )
        )

    source_section = b"".join(
        struct.pack("<HH32s", source.source_id, 0, bytes.fromhex(source.sha256))
        for source in manifest.sources
    )
    palette_section = b"".join(struct.pack("<H", value) for value in palette)
    palette_section += b"\0" * ((-len(palette_section)) % 4)
    animation_section = b"".join(
        struct.pack(
            "<HHHHBBHI",
            animation.animation_id,
            first_step,
            step_count,
            total_ticks,
            animation.facing_xor,
            0,
            0,
            0,
        )
        for animation, first_step, step_count, total_ticks in animation_records
    )
    step_section = b"".join(
        struct.pack("<HHI", frame_index, ticks, 0)
        for frame_index, ticks in step_records
    )
    frame_section = b"".join(
        struct.pack(
            "<IIHHhhIIHHhhHHBBH",
            frame.body.pixel_offset,
            len(frame.body.indices),
            frame.body.spec.rect[2],
            frame.body.spec.rect[3],
            frame.body.spec.origin[0],
            frame.body.spec.origin[1],
            frame.blade.pixel_offset if frame.blade is not None else 0,
            len(frame.blade.indices) if frame.blade is not None else 0,
            frame.blade.spec.rect[2] if frame.blade is not None else 0,
            frame.blade.spec.rect[3] if frame.blade is not None else 0,
            frame.blade.spec.origin[0] if frame.blade is not None else 0,
            frame.blade.spec.origin[1] if frame.blade is not None else 0,
            frame.spec.source_id,
            frame.spec.source_frame,
            frame.blade.spec.layer if frame.blade is not None else 0,
            0,
            0,
        )
        for frame in encoded_frames
    )

    body = bytearray(HEADER_BYTES)
    file_size = (
        HEADER_BYTES
        + len(source_section)
        + len(palette_section)
        + len(animation_section)
        + len(step_section)
        + len(frame_section)
        + len(pixel_blob)
    )
    if file_size > MAX_FILE_BYTES:
        _error("sidecar exceeds 64 MiB")
    struct.pack_into(
        "<8sHHIIHHHHHHI",
        body,
        0,
        b"MMXSABR\0",
        1,
        HEADER_BYTES,
        file_size,
        0,
        len(manifest.sources),
        len(palette),
        len(manifest.animations),
        len(step_records),
        len(encoded_frames),
        0,
        len(pixel_blob),
    )
    body[36:68] = hashlib.sha256(manifest_bytes).digest()
    data = body + source_section + palette_section + animation_section
    data += step_section + frame_section + pixel_blob
    hash_input = bytearray(data)
    hash_input[WHOLE_SHA_OFFSET : WHOLE_SHA_OFFSET + WHOLE_SHA_BYTES] = b"\0" * 32
    data[WHOLE_SHA_OFFSET : WHOLE_SHA_OFFSET + WHOLE_SHA_BYTES] = hashlib.sha256(
        hash_input
    ).digest()
    return bytes(data), tuple(encoded_frames), tuple(step_records)


def _palette_rgba(palette: tuple[int, ...]) -> list[tuple[int, int, int, int]]:
    colors: list[tuple[int, int, int, int]] = [(0, 0, 0, 0)]
    for value in palette[1:]:
        colors.append(
            (
                (value & 31) * 255 // 31,
                ((value >> 5) & 31) * 255 // 31,
                ((value >> 10) & 31) * 255 // 31,
                255,
            )
        )
    return colors


def _compose_preview_frame(
    frame: EncodedFrame, palette: list[tuple[int, int, int, int]]
) -> Any:
    if Image is None:
        _error("Pillow is required")
    planes = [frame.body]
    if frame.blade is not None and frame.blade.spec.layer == 1:
        planes.insert(0, frame.blade)
    if frame.blade is not None and frame.blade.spec.layer == 2:
        planes.append(frame.blade)
    left = 0
    top = 0
    right = 0
    bottom = 0
    for plane in planes:
        origin_x, origin_y = plane.spec.origin
        width, height = plane.spec.rect[2], plane.spec.rect[3]
        left = min(left, origin_x)
        top = min(top, origin_y)
        right = max(right, origin_x + width)
        bottom = max(bottom, origin_y + height)
    left -= 1
    top -= 1
    right += 1
    bottom += 1
    image = Image.new("RGBA", (right - left, bottom - top), (0, 0, 0, 0))
    for plane in planes:
        origin_x, origin_y = plane.spec.origin
        width, height = plane.spec.rect[2], plane.spec.rect[3]
        for row in range(height):
            for column in range(width):
                index = plane.indices[row * width + column]
                if index:
                    image.putpixel(
                        (
                            origin_x + column - left,
                            origin_y + row - top,
                        ),
                        palette[index],
                    )
    cross_x = -left
    cross_y = -top
    for dx, dy in ((0, 0), (-1, 0), (1, 0), (0, -1), (0, 1)):
        x, y = cross_x + dx, cross_y + dy
        if 0 <= x < image.width and 0 <= y < image.height:
            image.putpixel((x, y), (255, 0, 255, 255))
    return image


def _preview_png(
    animation: AnimationSpec,
    encoded_frames: tuple[EncodedFrame, ...],
    frame_indices: dict[int, int],
    palette: tuple[int, ...],
) -> bytes:
    if Image is None:
        _error("Pillow is required")
    colors = _palette_rgba(palette)
    native_frames = [
        _compose_preview_frame(
            encoded_frames[frame_indices[step.frame_id]],
            colors,
        )
        for step in animation.steps
    ]
    gap = 1
    width = sum(frame.width for frame in native_frames) + gap * (
        len(native_frames) - 1
    )
    height = max(frame.height for frame in native_frames)
    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    x = 0
    for frame in native_frames:
        sheet.alpha_composite(frame, (x, 0))
        x += frame.width + gap
    scaled = sheet.resize(
        (sheet.width * 2, sheet.height * 2),
        resample=Image.Resampling.NEAREST,
    )
    output = io.BytesIO()
    scaled.save(
        output,
        format="PNG",
        pnginfo=None,
        optimize=False,
        compress_level=9,
    )
    return output.getvalue()


def _report_bytes(
    manifest: ManifestSpec,
    frames: tuple[EncodedFrame, ...],
    output: bytes,
) -> bytes:
    report_frames: list[dict[str, Any]] = []
    for frame in frames:
        entry: dict[str, Any] = {
            "body": {
                "origin": list(frame.spec.body.origin),
                "rect": list(frame.spec.body.rect),
            },
            "id": frame.spec.frame_id,
            "source": frame.spec.source_id,
            "source_frame": frame.spec.source_frame,
        }
        if frame.spec.blade is None:
            entry["blade"] = None
        else:
            entry["blade"] = {
                "layer": frame.spec.blade.layer_name,
                "origin": list(frame.spec.blade.origin),
                "rect": list(frame.spec.blade.rect),
            }
        report_frames.append(entry)
    report = {
        "frame_count": len(frames),
        "frames": report_frames,
        "output_sha256": hashlib.sha256(output).hexdigest(),
        "palette_size": len(_palette_values_from_output(output)),
        "sources": [
            {"id": source.source_id, "sha256": source.sha256}
            for source in manifest.sources
        ],
    }
    return json.dumps(
        report,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode()


def _palette_values_from_output(output: bytes) -> tuple[int, ...]:
    palette_count = struct.unpack_from("<H", output, 22)[0]
    palette_offset = HEADER_BYTES + struct.unpack_from("<H", output, 20)[0] * 36
    return tuple(
        struct.unpack_from("<H", output, palette_offset + index * 2)[0]
        for index in range(palette_count)
    )


def _destination_key(path: Path) -> str:
    return os.path.normcase(os.path.realpath(os.path.abspath(str(path))))


def _stage_file(destination: Path, content: bytes) -> Path:
    if destination.exists() and destination.is_dir():
        _error(f"output path is a directory: {destination}")
    temporary_path: Optional[Path] = None
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.",
            suffix=".tmp",
            dir=str(destination.parent),
        )
        temporary_path = Path(temporary_name)
        try:
            with os.fdopen(fd, "wb") as stream:
                fd = -1
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
        except Exception:
            if fd >= 0:
                os.close(fd)
            raise
        return temporary_path
    except OSError as exc:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass
        _error(f"cannot stage output {destination}: {exc}")
    except Exception:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass
        raise
    raise AssertionError("unreachable")


def _publish(outputs: list[tuple[Path, bytes]]) -> None:
    staged: list[tuple[Path, Path]] = []
    try:
        for destination, content in outputs:
            staged.append((_stage_file(destination, content), destination))
        for temporary, destination in staged:
            os.replace(str(temporary), str(destination))
    except OSError as exc:
        _error(f"cannot publish outputs: {exc}")
    finally:
        for temporary, _ in staged:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                pass


def convert(
    manifest_path: Path,
    source_dir: Path,
    output_path: Path,
    preview_dir: Optional[Path] = None,
    report_path: Optional[Path] = None,
) -> None:
    manifest, manifest_bytes = _load_manifest(manifest_path)
    sources = _load_sources(source_dir, manifest.sources)
    prepared_frames = _prepare_frames(manifest, sources)
    palette, color_to_index = _make_palette(manifest, prepared_frames)
    output, encoded_frames, _ = _pack_output(
        manifest,
        manifest_bytes,
        prepared_frames,
        palette,
        color_to_index,
    )

    frame_indices = {
        frame.frame_id: index
        for index, frame in enumerate(manifest.frames)
    }
    outputs: list[tuple[Path, bytes]] = [(output_path, output)]
    if preview_dir is not None:
        for animation in manifest.animations:
            preview_name = f"animation_{animation.animation_id}.png"
            outputs.append(
                (
                    preview_dir / preview_name,
                    _preview_png(
                        animation,
                        encoded_frames,
                        frame_indices,
                        palette,
                    ),
                )
            )
    if report_path is not None:
        outputs.append(
            (
                report_path,
                _report_bytes(manifest, encoded_frames, output),
            )
        )
    destination_keys = [_destination_key(destination) for destination, _ in outputs]
    if len(destination_keys) != len(set(destination_keys)):
        _error("output paths must be distinct")
    source_keys = {
        _destination_key(_source_path(source_dir, source.file_name))
        for source in manifest.sources
    }
    if any(destination in source_keys for destination in destination_keys):
        _error("output path overlaps a read-only source")
    _publish(outputs)


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--preview-dir", type=Path)
    parser.add_argument("--report", type=Path)
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = _argument_parser().parse_args(argv)
    try:
        convert(
            args.manifest,
            args.source_dir,
            args.out,
            args.preview_dir,
            args.report,
        )
    except ConversionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
