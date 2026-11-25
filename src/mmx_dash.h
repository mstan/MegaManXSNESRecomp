#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mmx_render_assets.h"

/* One X controller exists even in X/Zero co-op. Its phase belongs to current
 * snapshots/rollback; native inventory and password data remain untouched. */
typedef struct MmxDashState {
  uint8_t valid,stage,used,ticks,facing,dash_jump,recovery,reserved;
} MmxDashState;
enum {MMX_DASH_EARLY=1,MMX_DASH_AIR=2};
void MmxDashEnable(unsigned options);
void MmxDashReset(void);
void MmxDashResetState(void);
MmxDashState MmxDashGetState(void);
bool MmxDashValidState(const MmxDashState *state);
void MmxDashSetState(const MmxDashState *state);
void MmxDashPrePlayer(uint8_t *ram,bool is_x,bool full_armor);
void MmxDashPlayerMotion(uint8_t *ram,unsigned object,bool is_x,bool full_armor);
void MmxDashPlayerEnd(uint8_t *ram,bool is_x,bool full_armor);
unsigned MmxDashUpgradeBits(unsigned original,bool is_x);
bool MmxDashEnabled(void);

/* Verified X2 data stays in a local cache. Native X1 arrangements decide
 * which pieces are absent; this provider never replaces acquired armor. */
bool MmxDashArtLoad(const char *path);
const MmxSpriteAsset *MmxDashArt(unsigned pose,const uint8_t **arrangement,const uint16_t *colors);
bool MmxDashPieceMissing(const uint8_t *native_layout,const uint8_t donor[4]);
bool MmxDashArtVisible(const uint8_t *ram,bool is_x,bool full_armor);
