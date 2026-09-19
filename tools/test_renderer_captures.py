#!/usr/bin/env python3
"""Capture isolated MMX save fixtures and replay each at five aspect settings.

Copies caller-owned saves; never runs in the source checkout or writes back to
the originals. The pixel oracle compares unanchored native pixels, then saves
anchored wide images for visual review. It does not certify a full playthrough.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile

def sprite_matches(data):
    """Independent native OAM check of pre-clipping geometry and art identity."""
    ram_offset = 12 + 224 * 66656
    pieces_offset = ram_offset + 0x20000 + 256 * 224 * 4
    version = struct.unpack_from('<I', data, 4)[0]
    stride = 12 if version == 2 else 8
    count = struct.unpack_from('<I', data, pieces_offset + 2048 * stride)[0]
    # OAM is stable in these fixture samples. Raster 100 avoids setup lines.
    raster = 12 + 100 * 66656
    oam = struct.unpack_from('<256H', data, raster + 64 + 512)
    high = data[raster + 64 + 512 + 512 + 65536:][:32]
    expected = set()
    for slot in range(128):
        pos, attr = oam[slot * 2:slot * 2 + 2]
        flags = high[slot // 4] >> (slot % 4 * 2)
        x = (pos & 255) | ((flags & 1) << 8)
        if x >= 256: x -= 512
        expected.add((x, pos >> 8, attr, 16 if flags & 2 else 8))
    matched = missing = 0
    for i in range(count):
        x, y, attr, size = struct.unpack_from('<hhHB', data, pieces_offset + i * stride)
        if -15 <= x < 255 and 0 <= y < 224:
            if (x, y & 255, attr, size) in expected: matched += 1
            else: missing += 1
    result = dict(native_piece_matches=matched, native_piece_unmatched=missing)
    if version == 2:
        expanded = pieces_offset + 2048 * stride + 10
        expanded_count = struct.unpack_from('<I', data, expanded + 2048 * stride + 2)[0]
        enabled = bool(data[expanded + 2048 * stride + 6])
        result.update(expanded_sprites=enabled, submitted_pieces=expanded_count)
        if enabled:
            result['submission_prefix_matches'] = (
                expanded_count >= count and data[pieces_offset:pieces_offset + count * stride] == data[expanded:expanded + count * stride])
    return result

def bmp_pixels(data):
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    rows = [data[offset + y * width * 4:offset + (y + 1) * width * 4] for y in range(abs(height))]
    return b''.join(rows if height < 0 else reversed(rows))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--saves', type=Path, required=True)
    p.add_argument('--slots', type=int, nargs='+', default=[0])
    p.add_argument('--artifacts', type=Path, required=True)
    p.add_argument('--frames', type=int, default=120)
    p.add_argument('--script', type=Path)
    p.add_argument('--aspect', choices=['adaptive', '16:9', '21:9', '32:9', 'max'], default='32:9')
    p.add_argument('--renderer', choices=['custom', 'legacy', 'off'], default='custom')
    p.add_argument('--expanded-sprites', action='store_true')
    args = p.parse_args()
    if args.script:
        for line in args.script.read_text().splitlines():
            parts = line.split('#', 1)[0].split()
            if parts and parts[0] not in ('wait', 'press', 'loadstate'):
                p.error(f'Unsupported input command: {parts[0]}')
    root = Path(__file__).resolve().parents[1]
    exe, rom = args.exe.resolve(strict=True), args.rom.resolve(strict=True)
    replay = exe.with_name('mmx_render_capture.exe' if os.name == 'nt' else 'mmx_render_capture')
    args.artifacts.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='mmx-render-', dir=args.artifacts.resolve()))
    print(run, flush=True)
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               MMX_RENDER_CAPTURE='final.capture', MMX_RENDER_CAPTURE_FRAME=str(args.frames),
               SNESRECOMP_FRAME_BMP='final.bmp', SNESRECOMP_FRAME_BMP_FRAME=str(args.frames))
    report = []
    for slot in args.slots:
        folder = run / f'slot{slot}'
        folder.mkdir(); (folder / 'saves').mkdir()
        shutil.copytree(root / 'mods/preloaded', folder / 'mods')
        shutil.copyfile(args.saves / f'save{slot}.sav', folder / 'saves/save0.sav')
        (folder / 'mods/state.toml').write_text('''format_version = 1
[[package]]
id = "megaman-x.enhancement.widescreen"
version = "1.0.0"
[[feature]]
package_id = "megaman-x.enhancement.widescreen"
id = "widescreen"
enabled = true
[feature.values]
renderer = "custom"
aspect = "32:9"
hud = "edges"
expanded_sprites = "off"
'''.replace('aspect = "32:9"', f'aspect = "{"adaptive" if args.aspect == "max" else args.aspect}"')
   .replace('expanded_sprites = "off"', f'expanded_sprites = "{"on" if args.expanded_sprites else "off"}"')
   .replace('renderer = "custom"', f'renderer = "{args.renderer if args.renderer != "off" else "custom"}"')
   .replace('enabled = true', f'enabled = {"false" if args.renderer == "off" else "true"}'))
        (folder / 'config.ini').write_text('''[General]
Autosave = 0
SkipLauncher = 1
DisableFrameDelay = 1
[Graphics]
WindowScale = 1
NewRenderer = 1
NoSpriteLimits = 1
OutputMethod = SDL-Software
DisplayAspect = 4:3
[Sound]
EnableAudio = 0
[GamepadMap]
EnableGamepad1 = false
EnableGamepad2 = false
'''.replace('WindowScale = 1', 'WindowScale = 1\nWindowSize = 2048x300' if args.aspect == 'max' else 'WindowScale = 1'))
        (folder / 'input.txt').write_text(args.script.read_text() if args.script else 'wait 30\nloadstate 0\n')
        with (folder / 'stdout.log').open('wb') as out, (folder / 'stderr.log').open('wb') as err:
            result = subprocess.run([str(exe), '--config', 'config.ini', '--script', 'input.txt',
                                     '--benchmark', str(args.frames), str(rom)], cwd=folder, env=env,
                                    stdout=out, stderr=err, timeout=180)
        if result.returncode or (args.renderer == 'custom' and not (folder / 'final.capture').is_file()):
            raise RuntimeError(f'slot {slot}: capture failed ({result.returncode}); see {folder}')
        log = (folder / 'stderr.log').read_text(errors='replace')
        if '[interp_cap]' in log or 'watchdog' in log.lower() or 'Save read error' in (folder / 'stdout.log').read_text(errors='replace'):
            raise RuntimeError(f'slot {slot}: invalid guest execution; see {folder}')
        bitmap = (folder / 'final.bmp').read_bytes()
        width, height = struct.unpack_from('<ii', bitmap, 18)
        expected_width = 256 if args.renderer == 'off' else 342 if args.renderer == 'legacy' else {
            'adaptive': 342, '16:9': 342, '21:9': 448, '32:9': 682, 'max': 1024}[args.aspect]
        if bitmap[:2] != b'BM' or width != expected_width or abs(height) != 224:
            raise RuntimeError(f'Incorrect presented dimensions: {width}x{height}, expected {expected_width}x224')
        if args.renderer != 'custom':
            if (folder / 'final.capture').exists(): raise RuntimeError('Custom renderer remained enabled')
            entry = dict(slot=slot, renderer=args.renderer, presented_width=width)
            report.append(entry); print(entry, flush=True)
            (run / 'report.json').write_text(json.dumps(report, indent=2))
            continue
        data = (folder / 'final.capture').read_bytes()
        ram = memoryview(data)[12 + 224 * 66656:][:0x20000]
        def word(address): return struct.unpack_from('<H', ram, address)[0]
        metadata = dict(stage=ram[0x1f7a], scene=ram[0xd3], camera_x=word(0x1e4d), camera_y=word(0x1e50),
                        stage_scene=ram[0xd1] == 2 and ram[0xd2] == 4
                        and (ram[0xd3] in (0, 2, 4, 6, 8) or (ram[0xd3] == 10 and ram[0xd4] in (0, 2)))
                        and not (ram[0x1f10] in (6, 8) and ram[0xc3] & 0x80), **sprite_matches(data))
        if metadata.get('expanded_sprites', False) != args.expanded_sprites:
            raise RuntimeError('Expanded sprite mod option did not match requested value')
        for aspect in ['4:3', '16:9', '21:9', '32:9', 'max']:
            name = aspect.replace(':', 'x')
            result = subprocess.run([str(replay), str(folder / 'final.capture'), str(rom), aspect,
                                     '1', str(folder / f'{name}.bmp')], env=env, capture_output=True, text=True)
            if result.returncode not in (0, 1):
                raise RuntimeError(result.stderr)
            entry = dict(slot=slot, aspect=aspect, **metadata, **json.loads(result.stdout))
            if aspect == ('16:9' if args.aspect == 'adaptive' else args.aspect):
                replay_bitmap = (folder / f'{name}.bmp').read_bytes()
                entry['live_replay_equal'] = bmp_pixels(bitmap) == bmp_pixels(replay_bitmap)
                if not entry['live_replay_equal']:
                    raise RuntimeError(f'Live presentation differs from replay: {folder}')
            report.append(entry)
            print(entry, flush=True)
        (run / 'report.json').write_text(json.dumps(report, indent=2))
    return int(any(e.get('native_differences', 0) or
                   ('custom_lines' in e and e['custom_lines'] != (224 if e['stage_scene'] else 0)) for e in report))

if __name__ == '__main__':
    raise SystemExit(main())
