#include "mmx_saber_assets.h"

#include "sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MMX_SABER_CACHE_DIR
#define MMX_SABER_CACHE_DIR ""
#endif

static const uint8_t kSaberManifestSha[32] = {
  0x4e, 0x29, 0x1e, 0x5f, 0x03, 0x57, 0xaf, 0xa0,
  0x35, 0x76, 0x14, 0xe0, 0xc4, 0x97, 0xf9, 0xb3,
  0x65, 0xee, 0x99, 0xe3, 0x70, 0x64, 0x5d, 0x84,
  0x99, 0xb2, 0xe4, 0xfd, 0x07, 0xf8, 0x0f, 0xd0,
};

static const uint8_t kRideManifestSha[32] = {
  0xdf, 0x56, 0x36, 0x93, 0x59, 0x9e, 0x7d, 0x4d,
  0x37, 0xcb, 0xb7, 0x28, 0x7c, 0x4d, 0x95, 0x3e,
  0xbf, 0x23, 0xdb, 0x14, 0xa5, 0xc4, 0xbd, 0x29,
  0x91, 0x91, 0xf8, 0xf4, 0xb1, 0xff, 0x7d, 0xac,
};

static int fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

static int join_path(char *out, size_t out_size, const char *directory,
                     const char *name) {
  int written;
  if (!directory || !directory[0] || !name || !name[0]) return 0;
  written = snprintf(out, out_size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < out_size;
}

static int file_exists(const char *path) {
  FILE *file = path ? fopen(path, "rb") : NULL;
  if (!file) return 0;
  fclose(file);
  return 1;
}

static uint8_t *read_file(const char *path, size_t *size_out) {
  FILE *file;
  long length;
  size_t size;
  uint8_t *data;

  if (size_out) *size_out = 0;
  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0) {
    if (file) fclose(file);
    return NULL;
  }
  length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  size = (size_t)length;
  data = (uint8_t *)malloc(size ? size : 1);
  if (!data || (size && fread(data, 1, size, file) != size)) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  if (size_out) *size_out = size;
  return data;
}

static int check_frame(const MmxSaberAssets *assets, uint16_t animation_id,
                       uint16_t step_index, int16_t origin_x,
                       int16_t origin_y, uint16_t width, uint16_t height,
                       const char *label) {
  const MmxSaberFrame *frame = MmxSaberAssetsFrameForStep(
      assets, animation_id, step_index);
  if (!frame || frame->body.origin_x != origin_x ||
      frame->body.origin_y != origin_y || frame->body.width != width ||
      frame->body.height != height) {
    fprintf(stderr, "FAIL: %s\n", label);
    return 0;
  }
  return 1;
}

static int check_saber_assets(const MmxSaberAssets *assets) {
  static const uint16_t ids[] = {1, 2, 3, 4, 5, 6, 7};
  static const uint16_t step_counts[] = {15, 15, 18, 9, 10, 15, 9};
  static const uint16_t total_ticks[] = {30, 30, 39, 18, 20, 30, 18};
  size_t i;

  if (MmxSaberAssetsPaletteCount(assets) != 29)
    return fail("Saber palette count is not 29");
  if (!MmxSaberAssetsPalette(assets) || MmxSaberAssetsPalette(assets)[0] != 0)
    return fail("Saber palette does not reserve index zero");
  if (memcmp(MmxSaberAssetsManifestSha256(assets), kSaberManifestSha,
             sizeof(kSaberManifestSha)) != 0)
    return fail("Saber manifest digest changed");

  for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
    const MmxSaberAnimation *animation =
        MmxSaberAssetsAnimationById(assets, ids[i]);
    if (!animation || animation->step_count != step_counts[i] ||
        animation->total_ticks != total_ticks[i] || animation->facing_xor != 1)
      return fail("Saber animation table changed");
  }

  if (!check_frame(assets, 1, 0, 46, 44, 37, 44,
                   "ground slash 1 anchor changed") ||
      !check_frame(assets, 2, 0, 46, 49, 47, 39,
                   "ground slash 2 anchor changed") ||
      !check_frame(assets, 3, 17, 45, 44, 39, 44,
                   "finisher anchor changed") ||
      !check_frame(assets, 4, 4, 37, 31, 82, 55,
                   "air slash vertical reach changed") ||
      !check_frame(assets, 5, 0, 48, 38, 40, 50,
                   "wall slash hitbox anchor changed") ||
      !check_frame(assets, 6, 0, 46, 55, 51, 35,
                   "dash slash anchor changed"))
    return 1;

  return 0;
}

static int check_ride_assets(const MmxSaberAssets *assets) {
  const MmxSaberAnimation *animation =
      MmxSaberAssetsAnimationById(assets, 0x006b);
  if (MmxSaberAssetsPaletteCount(assets) != 17)
    return fail("Ride Armor palette count is not 17");
  if (!animation || animation->step_count != 23 || animation->total_ticks != 23 ||
      animation->facing_xor != 1)
    return fail("Ride Armor animation does not contain 23 poses");
  if (memcmp(MmxSaberAssetsManifestSha256(assets), kRideManifestSha,
             sizeof(kRideManifestSha)) != 0)
    return fail("Ride Armor manifest digest changed");
  if (!check_frame(assets, 0x006b, 0, 80, 50, 160, 100,
                   "Ride Armor pilot anchor changed"))
    return 1;
  return 0;
}

static int check_corrupt_header(const char *path) {
  char reason[128];
  size_t size;
  uint8_t *data = read_file(path, &size);
  uint8_t digest[32];
  MmxSaberAssets *assets;

  if (!data || size < 112) {
    free(data);
    return fail("cannot read Saber cache for corruption test");
  }
  /* Flip a reserved header byte, then repair the whole-file digest. This
   * isolates the parser's header validation from its hash validation. */
  data[100] ^= 1;
  memset(data + 68, 0, sizeof(digest));
  sha256_compute(data, size, digest);
  memcpy(data + 68, digest, sizeof(digest));
  assets = MmxSaberAssetsParse(data, size, NULL, reason, sizeof(reason));
  free(data);
  if (assets) {
    MmxSaberAssetsFree(assets);
    return fail("corrupted header was accepted");
  }
  printf("ok: corrupted header rejected (%s)\n",
         reason[0] ? reason : "parser rejected input");
  return 0;
}

int main(void) {
  const char *directory = getenv("MMX_SABER_ASSET_CACHE_DIR");
  char saber_path[4096];
  char ride_path[4096];
  char reason[128];
  MmxSaberAssets *saber;
  MmxSaberAssets *ride;

  if (!directory || !directory[0]) directory = MMX_SABER_CACHE_DIR;
  if (!join_path(saber_path, sizeof(saber_path), directory, "saber-v1.bin") ||
      !join_path(ride_path, sizeof(ride_path), directory, "ride-zero-v1.bin"))
    return fail("Saber cache directory is not configured");
  if (!file_exists(saber_path) || !file_exists(ride_path)) {
    printf("SKIPPED: private Saber caches are absent (%s)\n", directory);
    return 77;
  }

  saber = MmxSaberAssetsLoadFile(saber_path, kSaberManifestSha,
                                 reason, sizeof(reason));
  if (!saber) {
    fprintf(stderr, "FAIL: Saber cache rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    return 1;
  }
  ride = MmxSaberAssetsLoadFile(ride_path, kRideManifestSha,
                                reason, sizeof(reason));
  if (!ride) {
    fprintf(stderr, "FAIL: Ride Armor cache rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    MmxSaberAssetsFree(saber);
    return 1;
  }

  if (check_saber_assets(saber) || check_ride_assets(ride) ||
      check_corrupt_header(saber_path)) {
    MmxSaberAssetsFree(ride);
    MmxSaberAssetsFree(saber);
    return 1;
  }
  MmxSaberAssetsFree(ride);
  MmxSaberAssetsFree(saber);
  puts("MMX SABER ASSET CHECKS PASSED");
  return 0;
}
