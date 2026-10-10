#!/usr/bin/env python3
"""Extract the vanilla X3 Z-Saber wave from a user-supplied USA ROM."""
import argparse
import hashlib
from pathlib import Path
import struct


SHA256 = '65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7'
WAVE_TRANSFERS = (0x85e672, 0x85e67f, 0x85e68b, 0x85e697)
WAVE_LAYOUT_GROUP = 0x4f
WAVE_POSES = (12, 13, 14, 15)
WAVE_WIDTH, WAVE_HEIGHT = 40, 48
WAVE_HEADER_BYTES = 88
WAVE_FRAME_RECORD_BYTES = 24
WAVE_STEP_RECORD_BYTES = 8


class Rom:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if len(self.data) % 32768 == 512:
            self.data = self.data[512:]
        if hashlib.sha256(self.data).hexdigest() != SHA256:
            raise ValueError('Expected the original Mega Man X3 USA ROM')

    def read(self, address, length):
        if address & 0xffff < 0x8000:
            raise ValueError(f'Not a ROM address: {address:06x}')
        offset = ((address >> 16) & 127) * 32768 + (address & 32767)
        result = self.data[offset:offset + length]
        if len(result) != length:
            raise ValueError(f'ROM read out of bounds: {address:06x}')
        return result

    def integer(self, address, length=2):
        return int.from_bytes(self.read(address, length), 'little')


def direct_transfer(rom, address, tiles, known=None):
    """Apply one of X3's direct two-record CHR transfer lists."""
    for _ in range(64):
        count = rom.read(address, 1)[0]
        if not count:
            return
        source = rom.integer(address + 1, 3)
        target = rom.integer(address + 4)
        offset = ((target & 0x7fff) - 0x6000) * 2
        length = count * 16
        if not 0 <= offset <= len(tiles) - length:
            raise ValueError('Invalid saber wave tile transfer')
        tiles[offset:offset + length] = rom.read(source, length)
        if known is not None:
            known[offset:offset + length] = b'\x01' * length
        address += 6
        if target & 0x8000:
            return
    raise ValueError('Unterminated saber wave tile transfer list')


def wave_frame(rom, pose_number, transfer):
    table = rom.integer(0x8d8000 + WAVE_LAYOUT_GROUP * 3, 3)
    address = rom.integer(table + pose_number * 3, 3)
    count = rom.read(address, 1)[0]
    if count > 64:
        raise ValueError('Invalid saber wave sprite piece count')
    pieces = rom.read(address + 1, count * 4)
    bounds = []
    for piece in range(count):
        flags, x, y, _tile = struct.unpack_from('<BbbB', pieces, piece * 4)
        size = 16 if flags & 32 else 8
        bounds.append((x, y, size))
        if flags & 14:
            raise ValueError('Unexpected saber wave sprite palette')
    left = min(x for x, _y, _size in bounds)
    top = min(y for _x, y, _size in bounds)
    right = max(x + size for x, _y, size in bounds)
    bottom = max(y + size for _x, y, size in bounds)
    if right - left > WAVE_WIDTH or bottom - top > WAVE_HEIGHT:
        raise ValueError('Saber wave layout exceeds extraction canvas')

    tiles = bytearray(8192)
    known = bytearray(8192)
    pixels = bytearray(WAVE_WIDTH * WAVE_HEIGHT)
    direct_transfer(rom, transfer, tiles, known)
    # The direct lists fill the exact CHR ranges used by these layouts. Keep
    # the known-byte check aligned with the native pose compositor.
    for piece in reversed(range(count)):
        flags, x, y, tile = struct.unpack_from('<BbbB', pieces, piece * 4)
        size = 16 if flags & 32 else 8
        for dy in range(size):
            for dx in range(size):
                tx = size - 1 - dx if flags & 64 else dx
                ty = size - 1 - dy if flags & 128 else dy
                number = (((tile >> 4) + ty // 8) & 15) * 16 + ((tile + tx // 8) & 15)
                bits = number * 32 + (ty & 7) * 2
                color = 0
                for plane in range(4):
                    index = bits + (plane // 2) * 16 + plane % 2
                    if index >= len(tiles):
                        raise ValueError('Saber wave tile index outside CHR')
                    if not known[index]:
                        raise ValueError('Unresolved saber wave source graphics')
                    color |= ((tiles[index] >> (7 - (tx & 7))) & 1) << plane
                if color:
                    px, py = x + dx - left, y + dy - top
                    if not (0 <= px < WAVE_WIDTH and 0 <= py < WAVE_HEIGHT):
                        raise ValueError('Saber wave pixel exceeds canvas')
                    pixels[py * WAVE_WIDTH + px] = color
    return pixels, left, top


def extract_wave(path):
    """Extract the ROM-owned X3 Z-Saber wave sidecar bytes."""
    rom = Rom(path)
    frame_count, step_count, palette_count = 4, 16, 16
    palette_offset = WAVE_HEADER_BYTES
    frame_offset = palette_offset + palette_count * 2
    step_offset = frame_offset + frame_count * WAVE_FRAME_RECORD_BYTES
    collision_offset = step_offset + step_count * WAVE_STEP_RECORD_BYTES
    pixel_offset = collision_offset + 4
    frame_bytes = WAVE_WIDTH * WAVE_HEIGHT
    pixel_bytes = frame_count * frame_bytes
    size = pixel_offset + pixel_bytes
    data = bytearray(size)
    data[:8] = b'MMXZWAV1'
    struct.pack_into('<HHI', data, 8, 1, WAVE_HEADER_BYTES, size)
    struct.pack_into('<6H', data, 48, frame_count, step_count, palette_count,
                     WAVE_WIDTH, WAVE_HEIGHT, 4)
    struct.pack_into('<6I', data, 60, palette_offset, frame_offset, step_offset,
                     collision_offset, pixel_offset, pixel_bytes)
    data[palette_offset:palette_offset + 32] = rom.read(0x8cb100, 32)
    data[collision_offset:collision_offset + 4] = rom.read(0x86b85f, 4)
    for index, pose_number in enumerate(WAVE_POSES):
        pixels, left, top = wave_frame(rom, pose_number, WAVE_TRANSFERS[index])
        descriptor = frame_offset + index * WAVE_FRAME_RECORD_BYTES
        pixels_at = pixel_offset + index * frame_bytes
        struct.pack_into('<IIHHhhH', data, descriptor, pixels_at, frame_bytes,
                         WAVE_WIDTH, WAVE_HEIGHT, left, top, pose_number)
        data[pixels_at:pixels_at + frame_bytes] = pixels
    for index in range(step_count):
        struct.pack_into('<HH', data, step_offset + index * WAVE_STEP_RECORD_BYTES,
                         index % frame_count, 2)
    digest = hashlib.sha256(data).digest()
    data[16:48] = digest
    return bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('rom', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = extract_wave(args.rom)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Extracted X3 Z-Saber wave to {args.output} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
