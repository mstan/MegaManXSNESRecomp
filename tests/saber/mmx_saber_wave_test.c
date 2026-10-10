#include "mmx_saber_wave.h"
#include "mmx_saber_wave_assets.h"
#include "sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MMX_SABER_WAVE_TEST_CACHE_DIR
#define MMX_SABER_WAVE_TEST_CACHE_DIR ""
#endif

#ifndef MMX_SABER_X3_ROM_DEFAULT
#define MMX_SABER_X3_ROM_DEFAULT ""
#endif

enum {
  WAVE_SIZE = 8028,
  WAVE_PALETTE_OFFSET = 88,
  WAVE_FRAME_OFFSET = 120,
  WAVE_STEP_OFFSET = 216,
  WAVE_COLLISION_OFFSET = 344,
  WAVE_PIXEL_OFFSET = 348,
  WAVE_FRAME_BYTES = 40 * 48
};

static int fail_test(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  return 0;
}

static int file_exists(const char *path) {
  FILE *file = path ? fopen(path, "rb") : NULL;
  if (!file) return 0;
  fclose(file);
  return 1;
}

static int read_file(const char *path, uint8_t **data, size_t *size) {
  FILE *file;
  long length;
  uint8_t *bytes;

  *data = NULL;
  *size = 0;
  file = fopen(path, "rb");
  if (!file) return 0;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  length = ftell(file);
  if (length < 0 || (unsigned long)length > 64u * 1024u * 1024u) {
    fclose(file);
    return 0;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  bytes = (uint8_t *)malloc(length ? (size_t)length : 1);
  if (!bytes) {
    fclose(file);
    return 0;
  }
  if (length && fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    free(bytes);
    fclose(file);
    return 0;
  }
  fclose(file);
  *data = bytes;
  *size = (size_t)length;
  return 1;
}

static uint16_t read_u16(const uint8_t *data) {
  return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void repair_wave_hash(uint8_t *data, size_t size) {
  uint8_t digest[32];
  memset(data + 16, 0, sizeof(digest));
  sha256_compute(data, size, digest);
  memcpy(data + 16, digest, sizeof(digest));
}

static int join_path(char *result, size_t result_size,
                     const char *directory, const char *name) {
  int written = snprintf(result, result_size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < result_size;
}

static size_t rom_offset(uint32_t address) {
  return (size_t)((address >> 16) & 127u) * 32768u +
      (size_t)(address & 32767u);
}

static void print_digest(const uint8_t digest[32], char text[65]) {
  unsigned i;
  for (i = 0; i < 32; ++i) snprintf(text + i * 2, 3, "%02x", digest[i]);
  text[64] = '\0';
}

static const char *find_rom(void) {
  const char *configured = getenv("MMX_SABER_X3_ROM");
  if (configured && configured[0]) {
    if (file_exists(configured)) return configured;
    printf("SKIPPED: MMX_SABER_X3_ROM is absent (%s)\n", configured);
    return NULL;
  }
  if (MMX_SABER_X3_ROM_DEFAULT[0] &&
      file_exists(MMX_SABER_X3_ROM_DEFAULT))
    return MMX_SABER_X3_ROM_DEFAULT;
  printf("SKIPPED: Mega Man X3 (USA).sfc is absent\n");
  return NULL;
}

static int build_cli(const char *rom, const char *output) {
  char error[512];
  if (!MmxSaberWaveAssetsBuild(rom, output, error, sizeof(error))) {
    fprintf(stderr, "%s\n", error[0] ? error : "wave build failed");
    return 1;
  }
  return 0;
}

static int asset_test(void) {
  const char *rom_path = find_rom();
  const char *cache_dir = getenv("MMX_SABER_WAVE_TEST_CACHE_DIR");
  char path_a[4096], path_b[4096], reason[256], digest_text[65];
  uint8_t *first = NULL, *second = NULL, *rom = NULL, *mutation = NULL;
  size_t first_size = 0, second_size = 0, rom_size = 0;
  MmxSaberWave *wave = NULL, *rejected = NULL;
  uint8_t digest[32];
  const uint16_t *palette;
  const uint8_t *collision;
  unsigned i;
  int result = 1;

  if (!rom_path) return 77;
  if (!cache_dir || !cache_dir[0]) cache_dir = MMX_SABER_WAVE_TEST_CACHE_DIR;
  if (!cache_dir[0] ||
      !join_path(path_a, sizeof(path_a), cache_dir,
                 "x3-saber-wave-v1.test.bin") ||
      !join_path(path_b, sizeof(path_b), cache_dir,
                 "x3-saber-wave-v1.test-second.bin"))
  {
    fail_test("wave test cache path is not configured");
    return 1;
  }
  remove(path_a);
  remove(path_b);

  {
    char error[512];
    if (!MmxSaberWaveAssetsBuild(rom_path, path_a, error, sizeof(error))) {
      fprintf(stderr, "FAIL: first wave build: %s\n", error);
      goto cleanup;
    }
    if (!MmxSaberWaveAssetsBuild(rom_path, path_b, error, sizeof(error))) {
      fprintf(stderr, "FAIL: second wave build: %s\n", error);
      goto cleanup;
    }
  }
  if (!read_file(path_a, &first, &first_size) ||
      !read_file(path_b, &second, &second_size)) {
    fail_test("cannot read produced wave cache");
    goto cleanup;
  }
  if (first_size != WAVE_SIZE || second_size != WAVE_SIZE) {
    fail_test("wave cache size changed");
    goto cleanup;
  }
  if (memcmp(first, second, WAVE_SIZE) != 0) {
    fail_test("building the wave cache twice was not deterministic");
    goto cleanup;
  }
  if (memcmp(first, "MMXZWAV1", 8) != 0 || read_u16(first + 8) != 1 ||
      read_u16(first + 10) != 88 || read_u32(first + 12) != WAVE_SIZE ||
      read_u32(first + 84) != 0) {
    fail_test("wave header fields are invalid");
    goto cleanup;
  }

  wave = MmxSaberWaveLoadFile(path_a, reason, sizeof(reason));
  if (!wave) {
    fprintf(stderr, "FAIL: produced wave cache rejected: %s\n", reason);
    goto cleanup;
  }
  if (MmxSaberWaveFrameCount(wave) != 4 ||
      MmxSaberWaveStepCount(wave) != 16 ||
      MmxSaberWavePaletteCount(wave) != 16 ||
      MmxSaberWaveTotalTicks(wave) != 32 ||
      MmxSaberWaveCollisionSize(wave) != 4) {
    fail_test("wave frame, pulse, palette, or collision counts changed");
    goto cleanup;
  }
  for (i = 0; i < 4; ++i) {
    static const int16_t origin_x[] = {-16, -8, -10, -13};
    static const int16_t origin_y[] = {-24, -16, -24, -24};
    const MmxSaberWaveFrame *frame = MmxSaberWaveFrameAt(wave, (uint16_t)i);
    if (!frame || !frame->pixels || frame->width != 40 || frame->height != 48 ||
        frame->origin_x != origin_x[i] || frame->origin_y != origin_y[i] ||
        frame->source_pose != 12 + i) {
      fail_test("wave frame dimensions or origins changed");
      goto cleanup;
    }
  }
  for (i = 0; i < 16; ++i) {
    const MmxSaberWaveStep *step =
        MmxSaberWaveAnimationStep(wave, (uint16_t)i);
    const MmxSaberWaveFrame *frame = MmxSaberWaveFrameForStep(wave, (uint16_t)i);
    if (!step || !frame || step->frame_index != i % 4 ||
        step->duration_ticks != 2 || frame->source_pose != 12 + i % 4) {
      fail_test("wave pulse sequence changed");
      goto cleanup;
    }
  }

  if (!read_file(rom_path, &rom, &rom_size)) {
    fail_test("cannot read source ROM for palette check");
    goto cleanup;
  }
  if (rom_size % 32768u == 512u) {
    memmove(rom, rom + 512, rom_size - 512);
    rom_size -= 512;
  }
  if (rom_offset(0x8cb100u) > rom_size ||
      32 > rom_size - rom_offset(0x8cb100u) ||
      rom_offset(0x86b85fu) > rom_size ||
      4 > rom_size - rom_offset(0x86b85fu)) {
    fail_test("source ROM palette or collision range is invalid");
    goto cleanup;
  }
  palette = MmxSaberWavePalette(wave);
  for (i = 0; i < 16; ++i) {
    if (palette[i] != read_u16(rom + rom_offset(0x8cb100u) + i * 2u)) {
      fail_test("wave palette does not match the X3 ROM");
      goto cleanup;
    }
  }
  collision = MmxSaberWaveCollisionRecord(wave);
  if (memcmp(collision, rom + rom_offset(0x86b85fu), 4) != 0) {
    fail_test("wave collision record does not match the X3 ROM");
    goto cleanup;
  }

  mutation = (uint8_t *)malloc(first_size);
  if (!mutation) {
    fail_test("cannot allocate corrupted wave header");
    goto cleanup;
  }
  memcpy(mutation, first, first_size);
  mutation[84] ^= 1;
  repair_wave_hash(mutation, first_size);
  rejected = MmxSaberWaveParse(mutation, first_size, reason, sizeof(reason));
  if (rejected) {
    MmxSaberWaveFree(rejected);
    rejected = NULL;
    fail_test("corrupted wave header was accepted");
    goto cleanup;
  }
  printf("PASS corrupted wave header rejected (%s)\n", reason);

  sha256_compute(first, first_size, digest);
  print_digest(digest, digest_text);
  printf("PASS wave asset: 4 frames, 16 pulses, 32 ticks, deterministic sha256 %s\n",
         digest_text);
  result = 0;

cleanup:
  MmxSaberWaveFree(wave);
  MmxSaberWaveFree(rejected);
  free(first);
  free(second);
  free(rom);
  free(mutation);
  remove(path_a);
  remove(path_b);
  return result;
}

int main(int argc, char **argv) {
  if (argc == 3) return build_cli(argv[1], argv[2]);
  if (argc != 1) {
    fprintf(stderr, "Usage: mmx_saber_wave_test [ROM OUTPUT]\n");
    return 2;
  }
  return asset_test();
}
