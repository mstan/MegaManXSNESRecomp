#pragma once

#include <stdbool.h>

#include "mmx_renderer.h"
#include "../mmx_zero.h"
#include "mmx_saber_assets.h"
#include "mmx_saber_attack.h"
#include "mmx_saber_wave.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve one explicit attack snapshot. This is also the independent seam used
 * by the live resolver below and by the Saber-owned sidecar tests. */
bool MmxSaberRenderResolveSnapshot(const MmxSaberAssets *assets,
                                   MmxSaberAttackSnapshot snapshot,
                                   MmxRenderPlayerOverlay *out);

/* Resolve the current Ride Armor pilot from live native player RAM. */
bool MmxSaberRenderResolveRide(const MmxSaberAssets *assets,
                               const uint8_t *ram,
                               MmxRenderPlayerOverlay *out);

/* Resolve the current live Saber attack state against the loaded sidecar.
 * The charge flash follows `zero`, the state of the seat whose body is being
 * drawn; NULL uses the live Zero state. */
bool MmxSaberRenderResolve(const MmxSaberAssets *assets,
                           const MmxZeroState *zero,
                           MmxRenderPlayerOverlay *out);

/* Resolve one live X3 wave against its validated sidecar. */
bool MmxSaberRenderResolveWaveSnapshot(const MmxSaberWave *wave,
                                       uint8_t age, int16_t world_x,
                                       int16_t world_y, bool facing_left,
                                       MmxRenderWorldSprite *out);

/* Saber-owned world-sprite provider lifecycle. */
void MmxSaberRenderSetWave(const MmxSaberWave *wave);
unsigned MmxSaberRenderProvideWorldSprites(MmxRenderWorldSprite *out,
                                           unsigned max);

#ifdef __cplusplus
}
#endif
