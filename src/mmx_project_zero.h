#pragma once

#include "mmx_zero.h"
#include "mmx_source_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MMX_PROJECT_ZERO_BODY_POSES = 0x55, MMX_PROJECT_ZERO_CAPTURE_POSES = 5 };

typedef struct MmxProjectZeroAssets {
  uint8_t body[MMX_PROJECT_ZERO_BODY_POSES][MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT];
  uint8_t capture[MMX_PROJECT_ZERO_CAPTURE_POSES][MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT];
  uint16_t colors[128];
  uint8_t life_head[16 * 16];
  int menu_y;
  uint16_t jump_speed;
  uint8_t muzzle_y[2];
} MmxProjectZeroAssets;

/* Return owned assets sampled from the supported IPS and stock cartridge.
 * The temporary patched image is private to the importer. */
MmxProjectZeroAssets *MmxProjectZeroRead(const char *path,
    const uint8_t *rom, size_t rom_size, char *error, size_t error_size);
void MmxZeroAdoptProjectAssets(MmxProjectZeroAssets *assets, const uint8_t hud[MMX_ZERO_HUD_BYTES]);
bool MmxZeroUsesProjectAssets(void);
int MmxZeroMenuY(void);
int MmxZeroProjectLifeColor(unsigned x, unsigned y);

#ifdef __cplusplus
}
#endif
