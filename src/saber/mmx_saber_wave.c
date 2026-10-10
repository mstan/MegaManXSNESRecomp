#include "mmx_saber_wave.h"

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MMX_SABER_WAVE_MAX_FILE_BYTES (1u * 1024u * 1024u)
#define MMX_SABER_WAVE_WHOLE_SHA_OFFSET 16u
#define MMX_SABER_WAVE_SHA_BYTES 32u
#define MMX_SABER_WAVE_PALETTE_OFFSET MMX_SABER_WAVE_HEADER_BYTES
#define MMX_SABER_WAVE_FRAME_TABLE_OFFSET \
  (MMX_SABER_WAVE_PALETTE_OFFSET + MMX_SABER_WAVE_PALETTE_COUNT * 2u)
#define MMX_SABER_WAVE_FRAME_RECORD_BYTES 24u
#define MMX_SABER_WAVE_STEP_TABLE_OFFSET \
  (MMX_SABER_WAVE_FRAME_TABLE_OFFSET + \
   MMX_SABER_WAVE_FRAME_COUNT * MMX_SABER_WAVE_FRAME_RECORD_BYTES)
#define MMX_SABER_WAVE_STEP_RECORD_BYTES 8u
#define MMX_SABER_WAVE_COLLISION_OFFSET \
  (MMX_SABER_WAVE_STEP_TABLE_OFFSET + \
   MMX_SABER_WAVE_STEP_COUNT * MMX_SABER_WAVE_STEP_RECORD_BYTES)
#define MMX_SABER_WAVE_PIXEL_OFFSET \
  (MMX_SABER_WAVE_COLLISION_OFFSET + MMX_SABER_WAVE_COLLISION_BYTES)
#define MMX_SABER_WAVE_FRAME_PIXEL_BYTES \
  (MMX_SABER_WAVE_CANVAS_WIDTH * MMX_SABER_WAVE_CANVAS_HEIGHT)
#define MMX_SABER_WAVE_PIXEL_BYTES \
  (MMX_SABER_WAVE_FRAME_COUNT * MMX_SABER_WAVE_FRAME_PIXEL_BYTES)
#define MMX_SABER_WAVE_FILE_BYTES \
  (MMX_SABER_WAVE_PIXEL_OFFSET + MMX_SABER_WAVE_PIXEL_BYTES)

static const int16_t k_wave_origin_x[MMX_SABER_WAVE_FRAME_COUNT] =
    {-16, -8, -10, -13};
static const int16_t k_wave_origin_y[MMX_SABER_WAVE_FRAME_COUNT] =
    {-24, -16, -24, -24};

struct MmxSaberWave {
  uint16_t total_ticks;
  uint16_t palette[MMX_SABER_WAVE_PALETTE_COUNT];
  uint8_t collision[MMX_SABER_WAVE_COLLISION_BYTES];
  MmxSaberWaveFrame frames[MMX_SABER_WAVE_FRAME_COUNT];
  MmxSaberWaveStep steps[MMX_SABER_WAVE_STEP_COUNT];
  uint8_t *pixels;
};

static void set_reason(char *reason, size_t reason_size, const char *message) {
  size_t length;
  if (!reason || !reason_size) return;
  if (!message) message = "";
  length = strlen(message);
  if (length >= reason_size) length = reason_size - 1;
  memcpy(reason, message, length);
  reason[length] = '\0';
}

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int16_t read_i16(const uint8_t *p) {
  return (int16_t)read_u16(p);
}

static bool range_in_file(uint32_t offset, uint32_t length, size_t size) {
  return offset <= size && (size_t)length <= size - offset;
}

static MmxSaberWave *fail(MmxSaberWave *wave, char *reason,
                          size_t reason_size, const char *message) {
  MmxSaberWaveFree(wave);
  set_reason(reason, reason_size, message);
  return NULL;
}

MmxSaberWave *MmxSaberWaveParse(const uint8_t *data, size_t size,
                                char *reason, size_t reason_size) {
  MmxSaberWave *wave = NULL;
  uint8_t digest[32];
  uint8_t *hash_input = NULL;
  size_t i;
  uint32_t file_size, palette_offset, frame_offset, step_offset;
  uint32_t collision_offset, pixel_offset, pixel_bytes;
  uint32_t expected_size = MMX_SABER_WAVE_FILE_BYTES;
  uint32_t total_ticks = 0;

  set_reason(reason, reason_size, "");
  if (!data) return fail(NULL, reason, reason_size, "null input");
  if (size > MMX_SABER_WAVE_MAX_FILE_BYTES)
    return fail(NULL, reason, reason_size, "file too large");
  if (size < MMX_SABER_WAVE_HEADER_BYTES)
    return fail(NULL, reason, reason_size, "header truncated");
  if (memcmp(data, "MMXZWAV1", 8) != 0)
    return fail(NULL, reason, reason_size, "magic mismatch");
  if (read_u16(data + 8) != MMX_SABER_WAVE_VERSION)
    return fail(NULL, reason, reason_size, "version mismatch");
  if (read_u16(data + 10) != MMX_SABER_WAVE_HEADER_BYTES)
    return fail(NULL, reason, reason_size, "header size mismatch");
  file_size = read_u32(data + 12);
  if (file_size != size)
    return fail(NULL, reason, reason_size, "file size mismatch");
  if (read_u16(data + 48) != MMX_SABER_WAVE_FRAME_COUNT ||
      read_u16(data + 50) != MMX_SABER_WAVE_STEP_COUNT ||
      read_u16(data + 52) != MMX_SABER_WAVE_PALETTE_COUNT ||
      read_u16(data + 54) != MMX_SABER_WAVE_CANVAS_WIDTH ||
      read_u16(data + 56) != MMX_SABER_WAVE_CANVAS_HEIGHT ||
      read_u16(data + 58) != MMX_SABER_WAVE_COLLISION_BYTES)
    return fail(NULL, reason, reason_size, "unsupported wave counts or canvas");
  if (read_u32(data + 84) != 0)
    return fail(NULL, reason, reason_size, "reserved header nonzero");

  palette_offset = read_u32(data + 60);
  frame_offset = read_u32(data + 64);
  step_offset = read_u32(data + 68);
  collision_offset = read_u32(data + 72);
  pixel_offset = read_u32(data + 76);
  pixel_bytes = read_u32(data + 80);
  if (palette_offset != MMX_SABER_WAVE_PALETTE_OFFSET ||
      frame_offset != MMX_SABER_WAVE_FRAME_TABLE_OFFSET ||
      step_offset != MMX_SABER_WAVE_STEP_TABLE_OFFSET ||
      collision_offset != MMX_SABER_WAVE_COLLISION_OFFSET ||
      pixel_offset != MMX_SABER_WAVE_PIXEL_OFFSET ||
      pixel_bytes != MMX_SABER_WAVE_PIXEL_BYTES ||
      file_size != expected_size)
    return fail(NULL, reason, reason_size, "wave section range mismatch");
  if (!range_in_file(palette_offset, MMX_SABER_WAVE_PALETTE_COUNT * 2u, size) ||
      !range_in_file(frame_offset,
                     MMX_SABER_WAVE_FRAME_COUNT * MMX_SABER_WAVE_FRAME_RECORD_BYTES,
                     size) ||
      !range_in_file(step_offset,
                     MMX_SABER_WAVE_STEP_COUNT * MMX_SABER_WAVE_STEP_RECORD_BYTES,
                     size) ||
      !range_in_file(collision_offset, MMX_SABER_WAVE_COLLISION_BYTES, size) ||
      !range_in_file(pixel_offset, pixel_bytes, size))
    return fail(NULL, reason, reason_size, "wave section outside file");

  hash_input = (uint8_t *)malloc(size);
  if (!hash_input)
    return fail(NULL, reason, reason_size, "allocation failed");
  memcpy(hash_input, data, size);
  memset(hash_input + MMX_SABER_WAVE_WHOLE_SHA_OFFSET, 0,
         MMX_SABER_WAVE_SHA_BYTES);
  sha256_compute(hash_input, size, digest);
  free(hash_input);
  if (memcmp(data + MMX_SABER_WAVE_WHOLE_SHA_OFFSET, digest,
             MMX_SABER_WAVE_SHA_BYTES) != 0)
    return fail(NULL, reason, reason_size, "whole-file hash mismatch");

  wave = (MmxSaberWave *)calloc(1, sizeof(*wave));
  if (!wave) return fail(NULL, reason, reason_size, "allocation failed");
  for (i = 0; i < MMX_SABER_WAVE_PALETTE_COUNT; ++i) {
    uint16_t color = read_u16(data + palette_offset + i * 2u);
    if (color & 0x8000u)
      return fail(wave, reason, reason_size, "palette entry has bit 15");
    wave->palette[i] = color;
  }
  memcpy(wave->collision, data + collision_offset,
         MMX_SABER_WAVE_COLLISION_BYTES);

  wave->pixels = (uint8_t *)malloc(pixel_bytes);
  if (!wave->pixels) return fail(wave, reason, reason_size, "allocation failed");
  memcpy(wave->pixels, data + pixel_offset, pixel_bytes);
  for (i = 0; i < pixel_bytes; ++i)
    if (wave->pixels[i] >= MMX_SABER_WAVE_PALETTE_COUNT)
      return fail(wave, reason, reason_size, "pixel index out of range");

  for (i = 0; i < MMX_SABER_WAVE_FRAME_COUNT; ++i) {
    const uint8_t *entry = data + frame_offset + i * MMX_SABER_WAVE_FRAME_RECORD_BYTES;
    uint32_t offset = read_u32(entry);
    uint32_t length = read_u32(entry + 4);
    uint16_t width = read_u16(entry + 8);
    uint16_t height = read_u16(entry + 10);
    uint16_t source_pose = read_u16(entry + 16);
    if (offset != MMX_SABER_WAVE_PIXEL_OFFSET + i * MMX_SABER_WAVE_FRAME_PIXEL_BYTES ||
        length != MMX_SABER_WAVE_FRAME_PIXEL_BYTES ||
        width != MMX_SABER_WAVE_CANVAS_WIDTH ||
        height != MMX_SABER_WAVE_CANVAS_HEIGHT ||
        source_pose != 12 + i ||
        read_i16(entry + 12) != k_wave_origin_x[i] ||
        read_i16(entry + 14) != k_wave_origin_y[i] ||
        entry[18] || entry[19] || read_u32(entry + 20) != 0)
      return fail(wave, reason, reason_size, "frame range or descriptor invalid");
    wave->frames[i].pixels = wave->pixels + (offset - pixel_offset);
    wave->frames[i].width = width;
    wave->frames[i].height = height;
    wave->frames[i].origin_x = read_i16(entry + 12);
    wave->frames[i].origin_y = read_i16(entry + 14);
    wave->frames[i].source_pose = source_pose;
  }

  for (i = 0; i < MMX_SABER_WAVE_STEP_COUNT; ++i) {
    const uint8_t *entry = data + step_offset + i * MMX_SABER_WAVE_STEP_RECORD_BYTES;
    uint16_t frame_index = read_u16(entry);
    uint16_t duration = read_u16(entry + 2);
    if (frame_index != i % 4 || duration != 2 ||
        read_u32(entry + 4) != 0)
      return fail(wave, reason, reason_size, "animation step range invalid");
    wave->steps[i].frame_index = frame_index;
    wave->steps[i].duration_ticks = duration;
    total_ticks += duration;
  }
  if (total_ticks != 32)
    return fail(wave, reason, reason_size, "animation duration invalid");
  wave->total_ticks = (uint16_t)total_ticks;
  return wave;
}

MmxSaberWave *MmxSaberWaveLoadFile(const char *path,
                                   char *reason, size_t reason_size) {
  FILE *file;
  long length;
  size_t size;
  uint8_t *data;
  MmxSaberWave *wave;

  set_reason(reason, reason_size, "");
  if (!path) return fail(NULL, reason, reason_size, "null file path");
  file = fopen(path, "rb");
  if (!file) return fail(NULL, reason, reason_size, "cannot open file");
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot seek file");
  }
  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot size file");
  }
  if ((unsigned long)length > MMX_SABER_WAVE_MAX_FILE_BYTES) {
    fclose(file);
    return fail(NULL, reason, reason_size, "file too large");
  }
  size = (size_t)length;
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot seek file");
  }
  data = (uint8_t *)malloc(size ? size : 1);
  if (!data) {
    fclose(file);
    return fail(NULL, reason, reason_size, "allocation failed");
  }
  if (size && fread(data, 1, size, file) != size) {
    free(data);
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot read file");
  }
  fclose(file);
  wave = MmxSaberWaveParse(data, size, reason, reason_size);
  free(data);
  return wave;
}

void MmxSaberWaveFree(MmxSaberWave *wave) {
  if (!wave) return;
  free(wave->pixels);
  free(wave);
}

uint16_t MmxSaberWaveFrameCount(const MmxSaberWave *wave) {
  return wave ? MMX_SABER_WAVE_FRAME_COUNT : 0;
}

const MmxSaberWaveFrame *MmxSaberWaveFrameAt(const MmxSaberWave *wave,
                                             uint16_t frame_index) {
  return wave && frame_index < MMX_SABER_WAVE_FRAME_COUNT ?
      &wave->frames[frame_index] : NULL;
}

const MmxSaberWaveFrame *MmxSaberWaveFrameForStep(const MmxSaberWave *wave,
                                                  uint16_t step_index) {
  const MmxSaberWaveStep *step = MmxSaberWaveAnimationStep(wave, step_index);
  return step ? MmxSaberWaveFrameAt(wave, step->frame_index) : NULL;
}

uint16_t MmxSaberWaveStepCount(const MmxSaberWave *wave) {
  return wave ? MMX_SABER_WAVE_STEP_COUNT : 0;
}

const MmxSaberWaveStep *MmxSaberWaveAnimationStep(const MmxSaberWave *wave,
                                                  uint16_t step_index) {
  return wave && step_index < MMX_SABER_WAVE_STEP_COUNT ?
      &wave->steps[step_index] : NULL;
}

uint16_t MmxSaberWaveTotalTicks(const MmxSaberWave *wave) {
  return wave ? wave->total_ticks : 0;
}

const uint16_t *MmxSaberWavePalette(const MmxSaberWave *wave) {
  return wave ? wave->palette : NULL;
}

uint16_t MmxSaberWavePaletteCount(const MmxSaberWave *wave) {
  return wave ? MMX_SABER_WAVE_PALETTE_COUNT : 0;
}

const uint8_t *MmxSaberWaveCollisionRecord(const MmxSaberWave *wave) {
  return wave ? wave->collision : NULL;
}

uint16_t MmxSaberWaveCollisionSize(const MmxSaberWave *wave) {
  return wave ? MMX_SABER_WAVE_COLLISION_BYTES : 0;
}
