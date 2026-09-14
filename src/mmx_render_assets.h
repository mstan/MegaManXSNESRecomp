#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MmxSpriteAsset {
  uint8_t tiles[8192];
  uint16_t colors[16];
  uint8_t id, tile_base, attributes;
  bool current, live_tiles;
} MmxSpriteAsset;

void MmxRenderAssetsSetRom(const uint8_t *rom, size_t size);
const MmxSpriteAsset *MmxRenderAssetsSprite(unsigned stage, unsigned section, unsigned sprite);
const MmxSpriteAsset *MmxRenderAssetsObjectSprite(const uint8_t ram[0x20000],
                                                unsigned object, unsigned animation);
/* ROM-authored palette transitions for the extra view; never touch CGRAM. */
void MmxRenderAssetsMarginPalette(const uint8_t ram[0x20000], int extra,
                                uint16_t colors[128], bool changed[128]);
