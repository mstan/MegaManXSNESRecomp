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
  int menu_y;
  uint16_t jump_speed;
  uint8_t muzzle_y[2];
} MmxProjectZeroAssets;

#define MMX_PROJECT_ZERO_PACK_MAGIC "MMXPZ001"
enum {
  MMX_PROJECT_ZERO_PACK_BYTES =
      8 + (MMX_PROJECT_ZERO_BODY_POSES + MMX_PROJECT_ZERO_CAPTURE_POSES) *
          MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT + 256 + 6 + MMX_ZERO_HUD_BYTES
};

bool MmxProjectZeroBundled(void);
MmxProjectZeroAssets *MmxProjectZeroLoad(uint8_t hud[MMX_ZERO_HUD_BYTES]);
void MmxZeroAdoptProjectAssets(MmxProjectZeroAssets *assets, const uint8_t hud[MMX_ZERO_HUD_BYTES]);
bool MmxZeroUsesProjectAssets(void);
int MmxZeroMenuY(void);

#ifdef __cplusplus
}
#endif
