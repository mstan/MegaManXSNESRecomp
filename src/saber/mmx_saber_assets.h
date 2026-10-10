#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MmxSaberAssets MmxSaberAssets;

typedef struct MmxSaberPlane {
  const uint8_t *pixels;
  uint16_t width;
  uint16_t height;
  int16_t origin_x;
  int16_t origin_y;
} MmxSaberPlane;

typedef struct MmxSaberFrame {
  MmxSaberPlane body;
  MmxSaberPlane blade;
  uint16_t source_id;
  uint16_t source_frame_id;
  uint8_t blade_layer;
} MmxSaberFrame;

typedef struct MmxSaberStep {
  uint16_t frame_index;
  uint16_t duration_ticks;
} MmxSaberStep;

typedef struct MmxSaberAnimation {
  uint16_t id;
  uint16_t first_step;
  uint16_t step_count;
  uint16_t total_ticks;
  uint8_t facing_xor;
  const MmxSaberStep *steps;
} MmxSaberAnimation;

/* Parse an in-memory Saber v1 sidecar. The returned object owns copies of all
 * descriptors and data needed by its accessors, so `data` may be released
 * after this function returns. `expected_manifest_sha` may be NULL to accept
 * the manifest digest carried by the file without comparing it. */
MmxSaberAssets *MmxSaberAssetsParse(const uint8_t *data, size_t size,
                                    const uint8_t expected_manifest_sha[32],
                                    char *reason, size_t reason_size);

/* Read and parse one complete Saber v1 sidecar from `path`. */
MmxSaberAssets *MmxSaberAssetsLoadFile(const char *path,
                                       const uint8_t expected_manifest_sha[32],
                                       char *reason, size_t reason_size);

void MmxSaberAssetsFree(MmxSaberAssets *assets);

const MmxSaberAnimation *MmxSaberAssetsAnimationById(const MmxSaberAssets *assets,
                                                     uint16_t animation_id);
const MmxSaberStep *MmxSaberAssetsAnimationStep(const MmxSaberAssets *assets,
                                                uint16_t animation_id,
                                                uint16_t step_index);
const MmxSaberFrame *MmxSaberAssetsFrameForStep(const MmxSaberAssets *assets,
                                                uint16_t animation_id,
                                                uint16_t step_index);

const uint16_t *MmxSaberAssetsPalette(const MmxSaberAssets *assets);
uint16_t MmxSaberAssetsPaletteCount(const MmxSaberAssets *assets);
const uint8_t *MmxSaberAssetsManifestSha256(const MmxSaberAssets *assets);

#ifdef __cplusplus
}
#endif
