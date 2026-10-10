#include "mmx_saber_sfx.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_HEADER = 24, TEST_RECORD = 20, TEST_PCM = 12 };

static int check(int condition, const char *what) {
  if (!condition) fprintf(stderr, "FAIL: %s\n", what);
  return condition;
}

static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static size_t valid_sidecar(uint8_t **out) {
  const size_t table = TEST_HEADER + MMX_SABER_SFX_CLIP_COUNT * TEST_RECORD;
  const size_t size = table + MMX_SABER_SFX_CLIP_COUNT * TEST_PCM;
  uint8_t *data = (uint8_t *)calloc(size, 1);
  if (!data) return 0;
  memcpy(data, "MMXSFX2", 7);
  put16(data + 8, MMX_SABER_SFX_VERSION);
  put16(data + 10, TEST_HEADER);
  put16(data + 12, MMX_SABER_SFX_CLIP_COUNT);
  put16(data + 14, TEST_RECORD);
  put32(data + 16, (uint32_t)size);
  put32(data + 20, MMX_SABER_SFX_CLIP_COUNT * TEST_PCM);
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    uint8_t *record = data + TEST_HEADER + i * TEST_RECORD;
    put32(record + 0, 1);
    put32(record + 4, MMX_SABER_SFX_NATIVE_SAMPLE_RATE);
    put32(record + 8, 6);
    put32(record + 12, TEST_PCM);
    for (unsigned j = 0; j < TEST_PCM / 2; ++j) {
      int16_t sample = (int16_t)(100 * (int)(i + 1) + (int)j);
      data[table + i * TEST_PCM + j * 2] = (uint8_t)sample;
      data[table + i * TEST_PCM + j * 2 + 1] = (uint8_t)(sample >> 8);
    }
  }
  *out = data;
  return size;
}

static int expect_reject(const uint8_t *data, size_t size,
                         const char *category) {
  MmxSaberSfx *sfx = NULL;
  char reason[64] = {0};
  if (MmxSaberSfxParse(data, size, &sfx, reason, sizeof(reason))) {
    MmxSaberSfxFree(sfx);
    return check(0, "malformed sidecar is rejected");
  }
  return check(!sfx && strstr(reason, category) != NULL, category);
}

static int file_exists(const char *path) {
  FILE *file = path ? fopen(path, "rb") : NULL;
  if (!file) return 0;
  fclose(file);
  return 1;
}

static int actual_cache_test(void) {
#ifndef MMX_SABER_SFX_CACHE
  puts("SKIP Saber SFX cache path is not configured");
  return 77;
#else
  const char *path = MMX_SABER_SFX_CACHE;
  MmxSaberSfx *sfx;
  char reason[128] = {0};
  if (!file_exists(path)) {
    puts("SKIP Saber SFX private cache is absent");
    return 77;
  }
  sfx = MmxSaberSfxLoadFile(path, reason, sizeof(reason));
  if (!check(sfx != NULL, "private Saber SFX v2 cache loads")) return 0;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    const MmxSaberSfxClip *clip = MmxSaberSfxClipAt(sfx, i);
    if (!check(clip != NULL, "private cache clip exists") ||
        !check(clip->channels == MMX_SABER_SFX_CHANNELS,
               "private cache is mono") ||
        !check(clip->sample_rate == MMX_SABER_SFX_NATIVE_SAMPLE_RATE,
               "private cache is 32 kHz") ||
        !check(clip->frame_count > 0 && clip->samples != NULL,
               "private cache clip has PCM") ||
        !check(clip->byte_length == clip->frame_count * sizeof(int16_t),
               "private cache PCM length matches frames")) {
      MmxSaberSfxFree(sfx);
      return 0;
    }
  }
  MmxSaberSfxFree(sfx);
  puts("PASS Saber SFX v2 private cache loads as mono 32 kHz PCM");
  return 1;
#endif
}

int main(void) {
  uint8_t *data = NULL;
  uint8_t *bad = NULL;
  size_t size;
  MmxSaberSfx *sfx = NULL;
  char reason[64] = {0};
  int cache_result;

  cache_result = actual_cache_test();
  if (cache_result == 77) return 77;
  if (!cache_result) return 1;

  size = valid_sidecar(&data);
  if (!check(data != NULL && size != 0, "valid test sidecar allocates"))
    return 1;
  if (!check(MmxSaberSfxParse(data, size, &sfx, reason, sizeof(reason)),
             "valid sidecar parses") ||
      !check(sfx != NULL && reason[0] == '\0', "valid sidecar publishes")) {
    free(data);
    MmxSaberSfxFree(sfx);
    return 1;
  }
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    const MmxSaberSfxClip *clip = MmxSaberSfxClipAt(sfx, i);
    if (!check(clip && clip->frame_count == 6u, "test clip frame count") ||
        !check(clip && clip->channels == MMX_SABER_SFX_CHANNELS,
               "test clip channel count") ||
        !check(clip && clip->sample_rate == MMX_SABER_SFX_NATIVE_SAMPLE_RATE,
               "test clip sample rate") ||
        !check(clip && clip->byte_length == TEST_PCM,
               "test clip byte length") ||
        !check(clip && clip->samples[0] == (int16_t)(100 * (i + 1)),
               "test clip PCM copy")) {
      MmxSaberSfxFree(sfx);
      free(data);
      return 1;
    }
  }
  MmxSaberSfxFree(sfx);

  {
    bad = (uint8_t *)malloc(size);
    if (!check(bad != NULL, "malformed test sidecar allocates")) {
      free(data);
      return 1;
    }
    memcpy(bad, data, size);
    bad[0] = 'X';
    if (!expect_reject(bad, size, "magic")) goto bad_fail;
    memcpy(bad, data, size);
    bad[7] = '1';
    if (!expect_reject(bad, size, "magic")) goto bad_fail;
    memcpy(bad, data, size);
    put16(bad + 8, 1);
    if (!expect_reject(bad, size, "unsupported version")) goto bad_fail;
    memcpy(bad, data, size);
    if (!expect_reject(bad, size - 1, "file size")) goto bad_fail;
    memcpy(bad, data, size);
    put32(bad + TEST_HEADER + 12, UINT32_MAX);
    if (!expect_reject(bad, size, "PCM length overflow")) goto bad_fail;
    memcpy(bad, data, size);
    put32(bad + TEST_HEADER + 0, 3);
    if (!expect_reject(bad, size, "channels")) goto bad_fail;
    memcpy(bad, data, size);
    put32(bad + TEST_HEADER + 4, 7999);
    if (!expect_reject(bad, size, "sample rate")) goto bad_fail;
    memcpy(bad, data, size);
    put32(bad + 20, MMX_SABER_SFX_MAX_PCM_BYTES + 1u);
    if (!expect_reject(bad, size, "aggregate PCM cap")) goto bad_fail;
    memcpy(bad, data, size);
    put32(bad + 20, 1);
    if (!expect_reject(bad, size, "aggregate PCM length")) goto bad_fail;
    free(bad);
  }
  free(data);
  puts("PASS Saber SFX v2 loader rejects bad magic/version/truncation/overflow/layout/aggregate");
  return 0;

bad_fail:
  free(bad);
  free(data);
  return 1;
}
