#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MmxSpriteAsset {
  uint8_t tiles[8192];
  uint16_t colors[16];
  uint8_t id, tile_base, attributes;
  bool current, live_tiles, live_colors;
} MmxSpriteAsset;

void MmxRenderAssetsSetRom(const uint8_t *rom, size_t size);
const MmxSpriteAsset *MmxRenderAssetsSprite(unsigned stage, unsigned section, unsigned sprite);
/* Private residency for cross-stage boss/child actors in the rush arena. */
const MmxSpriteAsset *MmxRenderAssetsRushSprite(unsigned stage,unsigned sprite);
const MmxSpriteAsset *MmxRenderAssetsCaptiveZero(void);
/* Original X1 body/beam CHR and blue palette, decoded without touching VRAM. */
const MmxSpriteAsset *MmxRenderAssetsTeleportX(unsigned pose);
/* Native X1 weapon-selection DMA and palettes, isolated from the other seat. */
const MmxSpriteAsset *MmxRenderAssetsWeaponX(unsigned weapon, bool body);
/* Full native buster pose, independent of another co-op shot's CHR upload. */
const MmxSpriteAsset *MmxRenderAssetsChargedBuster(unsigned group, unsigned pose);
/* Charged Sting's original eight-step body palette sequence. */
bool MmxRenderAssetsStingPalette(unsigned phase, uint16_t colors[16]);
const MmxSpriteAsset *MmxRenderAssetsObjectSprite(const uint8_t ram[0x20000],
                                                unsigned object, unsigned animation);
bool MmxRenderAssetsRideArmorPalettePending(const uint8_t ram[0x20000], const uint16_t colors[16]);
unsigned MmxRenderAssetsDeathPaletteFade(const uint8_t ram[0x20000], const uint16_t colors[256]);
uint16_t MmxRenderAssetsFadeColor(uint16_t color, unsigned amount);
typedef struct MmxBackgroundPalette {
  uint16_t colors[128];
  bool valid[128];
} MmxBackgroundPalette;
/* Resolve the resource belonging to a world column, independently of which
 * side of the viewport it occupies. NULL retains the current live resource. */
const uint8_t *MmxRenderAssetsBackgroundTile(const uint8_t ram[0x20000],
                                            int world_x, unsigned vram_word);
const MmxBackgroundPalette *MmxRenderAssetsBackgroundPalette(const uint8_t ram[0x20000],
                                                             int world_x);
const MmxBackgroundPalette *MmxRenderAssetsBackgroundPalettePhase(const uint8_t ram[0x20000], unsigned phase);
