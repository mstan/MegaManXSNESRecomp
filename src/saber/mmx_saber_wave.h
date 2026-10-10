#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  MMX_SABER_WAVE_VERSION = 1,
  MMX_SABER_WAVE_HEADER_BYTES = 88,
  MMX_SABER_WAVE_FRAME_COUNT = 4,
  MMX_SABER_WAVE_STEP_COUNT = 16,
  MMX_SABER_WAVE_PALETTE_COUNT = 16,
  MMX_SABER_WAVE_CANVAS_WIDTH = 40,
  MMX_SABER_WAVE_CANVAS_HEIGHT = 48,
  MMX_SABER_WAVE_COLLISION_BYTES = 4
};

typedef struct MmxSaberWave MmxSaberWave;

typedef struct MmxSaberWaveFrame {
  const uint8_t *pixels;
  uint16_t width;
  uint16_t height;
  int16_t origin_x;
  int16_t origin_y;
  uint16_t source_pose;
} MmxSaberWaveFrame;

typedef struct MmxSaberWaveStep {
  uint16_t frame_index;
  uint16_t duration_ticks;
} MmxSaberWaveStep;

MmxSaberWave *MmxSaberWaveParse(const uint8_t *data, size_t size,
                                char *reason, size_t reason_size);
MmxSaberWave *MmxSaberWaveLoadFile(const char *path,
                                   char *reason, size_t reason_size);
void MmxSaberWaveFree(MmxSaberWave *wave);

uint16_t MmxSaberWaveFrameCount(const MmxSaberWave *wave);
const MmxSaberWaveFrame *MmxSaberWaveFrameAt(const MmxSaberWave *wave,
                                             uint16_t frame_index);
const MmxSaberWaveFrame *MmxSaberWaveFrameForStep(const MmxSaberWave *wave,
                                                  uint16_t step_index);
uint16_t MmxSaberWaveStepCount(const MmxSaberWave *wave);
const MmxSaberWaveStep *MmxSaberWaveAnimationStep(const MmxSaberWave *wave,
                                                  uint16_t step_index);
uint16_t MmxSaberWaveTotalTicks(const MmxSaberWave *wave);
const uint16_t *MmxSaberWavePalette(const MmxSaberWave *wave);
uint16_t MmxSaberWavePaletteCount(const MmxSaberWave *wave);
const uint8_t *MmxSaberWaveCollisionRecord(const MmxSaberWave *wave);
uint16_t MmxSaberWaveCollisionSize(const MmxSaberWave *wave);

#ifdef __cplusplus
}
#endif
