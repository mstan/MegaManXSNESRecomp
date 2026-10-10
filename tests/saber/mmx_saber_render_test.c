#include "mmx_saber_render.h"
#include "mmx_zero.h"
#include "sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MMX_SABER_RENDER_CACHE_DIR
#define MMX_SABER_RENDER_CACHE_DIR ""
#endif

/* The isolated resolver target does not link the full Zero controller. These
 * doubles expose the same native body-palette query used by the provider. */
static MmxZeroState test_zero_state;
static uint16_t test_zero_colors[128];
static uint16_t test_charge_colors[3][16];

bool MmxZeroActive(void) { return true; }
MmxZeroState MmxZeroGetState(void) { return test_zero_state; }
const uint16_t *MmxZeroColors(void) { return test_zero_colors; }
int MmxZeroChargeFlashPaletteIndex(const MmxZeroState *s) {
  if (!s || s->active_x || s->swap_phase || s->slash ||
      (s->charge < 25 && !(s->combo && s->saber_ready)) || (s->charge_phase & 2))
    return -1;
  return s->saber_ready || s->charge >= 201 ? 2 : s->charge >= 141 ? 1 : 0;
}
const uint16_t *MmxZeroBodyColors(const MmxZeroState *s) {
  int index = MmxZeroChargeFlashPaletteIndex(s);
  return index < 0 ? test_zero_colors + 16 : test_charge_colors[index];
}

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

static const uint16_t kRideSourceFrames[23] = {
  4, 35, 36, 7, 8, 9, 19, 14, 15, 16, 5, 6,
  26, 21, 22, 23, 1, 2, 3, 28, 30, 31, 32,
};

typedef struct OldRenderTuple {
  uint8_t tick;
  uint8_t step;
  uint8_t source_frame;
  int16_t body_x;
  int16_t body_y;
  uint16_t body_width;
  uint16_t body_height;
} OldRenderTuple;

/* Independent oracle copied from oldsaber/saber-zero-variant:src/mmx_saber.c:
 * 1351-1365. The tuples are the sidecar frame/step, origin, and dimensions
 * selected by that old walk; they are deliberately not obtained from the
 * resolver under test. The old compositor's plane contract is at
 * oldsaber/saber-zero-variant:src/mmx_renderer.c:1199-1215. */
static const OldRenderTuple kOldTuples[7][4] = {
  {
    {0, 0, 0, 46, 44, 37, 44},
    {29, 14, 14, 44, 44, 39, 44},
    {30, 14, 14, 44, 44, 39, 44},
    {35, 14, 14, 44, 44, 39, 44},
  },
  {
    {0, 0, 0, 46, 49, 47, 39},
    {29, 14, 14, 44, 44, 39, 44},
    {30, 14, 14, 44, 44, 39, 44},
    {35, 14, 14, 44, 44, 39, 44},
  },
  {
    {0, 0, 0, 44, 44, 39, 44},
    {30, 15, 15, 43, 44, 41, 44},
    {38, 17, 17, 45, 44, 39, 44},
    {44, 17, 17, 45, 44, 39, 44},
  },
  {
    {0, 0, 0, 37, 37, 40, 51},
    {17, 8, 8, 50, 23, 29, 64},
    {18, 8, 8, 50, 23, 29, 64},
    {23, 8, 8, 50, 23, 29, 64},
  },
  {
    {0, 0, 9, 48, 38, 40, 50},
    {19, 9, 0, 48, 38, 35, 50},
    {20, 9, 0, 48, 38, 35, 50},
    {25, 9, 0, 48, 38, 35, 50},
  },
  {
    {0, 0, 0, 46, 55, 51, 35},
    {29, 14, 14, 44, 44, 39, 44},
    {30, 14, 14, 44, 44, 39, 44},
    {35, 14, 14, 44, 44, 39, 44},
  },
  {
    {0, 0, 0, 36, 43, 41, 45},
    {17, 8, 8, 43, 44, 37, 44},
    {18, 8, 8, 43, 44, 37, 44},
    {23, 8, 8, 43, 44, 37, 44},
  },
};

static const uint8_t kTotals[7] = {30, 30, 39, 18, 20, 30, 18};
static const MmxSaberPadKind kKinds[7] = {
  SABER_KIND_GROUND1, SABER_KIND_GROUND2, SABER_KIND_GROUND3,
  SABER_KIND_AIR, SABER_KIND_WALL, SABER_KIND_DASH,
  SABER_KIND_SABER_LAND,
};
static const uint8_t kIndices[7] = {0, 1, 2, 0, 0, 0, 0};

static void check(int ok, const char *message) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
  printf("ok: %s\n", message);
}

static int join_path(char *out, size_t out_size, const char *directory,
                     const char *name) {
  int written = snprintf(out, out_size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < out_size;
}

static void put_u16(uint8_t *bytes, unsigned offset, unsigned value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, unsigned offset, unsigned value) {
  put_u16(bytes, offset, value);
  put_u16(bytes, offset + 2, value >> 16);
}

static MmxSaberAssets *make_flash_assets(void) {
  enum { SIZE = 226, FRAME_OFFSET = 180, PIXEL_OFFSET = 220 };
  uint8_t data[SIZE] = {0};
  uint8_t digest[32];
  char reason[128] = {0};

  memcpy(data, "MMXSABR\0", 8);
  put_u16(data, 8, 1);
  put_u16(data, 10, 112);
  put_u32(data, 12, SIZE);
  put_u16(data, 20, 1); /* source count */
  put_u16(data, 22, 4); /* palette count */
  put_u16(data, 24, 1); /* animation count */
  put_u16(data, 26, 1); /* step count */
  put_u16(data, 28, 1); /* frame count */
  put_u32(data, 32, 6); /* body 4 bytes + blade 2 bytes */
  put_u16(data, 112, 1); /* source id */
  put_u16(data, 148, 0);
  put_u16(data, 150, 1);
  put_u16(data, 152, 2);
  put_u16(data, 154, 3);
  put_u16(data, 156, 1); /* animation id */
  put_u16(data, 158, 0); /* first step */
  put_u16(data, 160, 1); /* step count */
  put_u16(data, 162, 1); /* total ticks */
  put_u16(data, 172, 0); /* frame index */
  put_u16(data, 174, 1); /* duration */
  put_u32(data, FRAME_OFFSET, 0); /* body offset */
  put_u32(data, FRAME_OFFSET + 4, 4); /* body length */
  put_u16(data, FRAME_OFFSET + 8, 2);
  put_u16(data, FRAME_OFFSET + 10, 2);
  put_u32(data, FRAME_OFFSET + 16, 4); /* blade offset */
  put_u32(data, FRAME_OFFSET + 20, 2); /* blade length */
  put_u16(data, FRAME_OFFSET + 24, 2);
  put_u16(data, FRAME_OFFSET + 26, 1);
  put_u16(data, FRAME_OFFSET + 32, 1); /* source id */
  put_u16(data, FRAME_OFFSET + 34, 0); /* source frame */
  data[FRAME_OFFSET + 36] = 1; /* blade behind body */
  data[PIXEL_OFFSET + 0] = 1;
  data[PIXEL_OFFSET + 1] = 2;
  data[PIXEL_OFFSET + 2] = 0;
  data[PIXEL_OFFSET + 3] = 3;
  data[PIXEL_OFFSET + 4] = 2;
  data[PIXEL_OFFSET + 5] = 3;
  memset(data + 68, 0, sizeof(digest));
  sha256_compute(data, SIZE, digest);
  memcpy(data + 68, digest, sizeof(digest));
  return MmxSaberAssetsParse(data, SIZE, NULL, reason, sizeof(reason));
}

static int check_plane(const MmxRenderPlayerOverlayPlane *actual,
                       const MmxSaberPlane *expected) {
  return actual->pixels == expected->pixels &&
      actual->width == expected->width && actual->height == expected->height &&
      actual->origin_x == expected->origin_x &&
      actual->origin_y == expected->origin_y;
}

static unsigned color_luminance(uint16_t color) {
  unsigned red = color & 31, green = (color >> 5) & 31, blue = (color >> 10) & 31;
  return red * 299 + green * 587 + blue * 114;
}

/* Independent oracle for the old donor-palette mapping at
 * oldsaber/saber-zero-variant:src/mmx_renderer.c:1138-1191. */
static void expected_flash_palette(uint16_t *mapped, const uint16_t *palette,
                                   uint16_t palette_count,
                                   const uint16_t *flash) {
  uint16_t donor_order[256], flash_order[16];
  unsigned donor_count = 0, groups = 0;
  memcpy(mapped, palette, (size_t)palette_count * sizeof(*mapped));
  mapped[0] = 0;
  for (unsigned i = 1; i < palette_count; ++i) {
    unsigned j = donor_count;
    while (j && (color_luminance(palette[donor_order[j - 1]]) >
                     color_luminance(palette[i]) ||
                 (color_luminance(palette[donor_order[j - 1]]) ==
                      color_luminance(palette[i]) && donor_order[j - 1] > i))) {
      donor_order[j] = donor_order[j - 1];
      --j;
    }
    donor_order[j] = (uint16_t)i;
    ++donor_count;
  }
  for (unsigned i = 0; i < 16; ++i) {
    unsigned j = i;
    while (j && (color_luminance(flash[flash_order[j - 1]]) >
                     color_luminance(flash[i]) ||
                 (color_luminance(flash[flash_order[j - 1]]) ==
                      color_luminance(flash[i]) && flash_order[j - 1] > i))) {
      flash_order[j] = flash_order[j - 1];
      --j;
    }
    flash_order[j] = (uint16_t)i;
  }
  for (unsigned rank = 0; rank < donor_count;) {
    unsigned end = rank + 1;
    unsigned luminance = color_luminance(palette[donor_order[rank]]);
    while (end < donor_count &&
           color_luminance(palette[donor_order[end]]) == luminance) ++end;
    ++groups;
    rank = end;
  }
  for (unsigned group = 0, rank = 0; rank < donor_count; ++group) {
    unsigned end = rank + 1;
    unsigned luminance = color_luminance(palette[donor_order[rank]]);
    while (end < donor_count &&
           color_luminance(palette[donor_order[end]]) == luminance) ++end;
    unsigned flash_rank = groups > 1 ? group * 15 / (groups - 1) : 0;
    for (unsigned i = rank; i < end; ++i)
      mapped[donor_order[i]] = flash[flash_order[flash_rank]];
    rank = end;
  }
}

static bool find_blade_snapshot(const MmxSaberAssets *assets,
                               MmxSaberAttackSnapshot *snapshot,
                               const MmxSaberFrame **expected) {
  for (size_t i = 0; i < MmxSaberAttackTableCount(); ++i) {
    const MmxSaberAttack *attack = MmxSaberAttackTableAt(i);
    const MmxSaberAnimation *animation;
    uint16_t tick = 0;
    if (!attack) continue;
    animation = MmxSaberAssetsAnimationById(assets, attack->visual_animation);
    if (!animation) continue;
    for (uint16_t step = 0; step < animation->step_count; ++step) {
      const MmxSaberFrame *frame = MmxSaberAssetsFrameForStep(
          assets, attack->visual_animation, step);
      if (frame && frame->blade.pixels && frame->blade.width && frame->blade.height) {
        *snapshot = (MmxSaberAttackSnapshot){
          attack->kind, attack->index, SABER_PHASE_ACTIVE, (uint8_t)tick,
          (uint8_t)attack->visual_animation, (uint8_t)step, 0};
        *expected = frame;
        return true;
      }
      tick = (uint16_t)(tick + animation->steps[step].duration_ticks);
    }
  }
  return false;
}

static unsigned first_pixel(const MmxSaberPlane *plane) {
  size_t size = (size_t)plane->width * plane->height;
  for (size_t i = 0; i < size; ++i) if (plane->pixels[i]) return plane->pixels[i];
  return 0;
}

static void charge_flash_checks(const MmxSaberAssets *assets) {
  const uint16_t *base = MmxSaberAssetsPalette(assets);
  uint16_t palette_count = MmxSaberAssetsPaletteCount(assets);
  MmxSaberAttackSnapshot snapshot;
  const MmxSaberFrame *expected;
  bool native_flash[8], overlay_flash[8];
  uint16_t expected_palette[256];

  check(find_blade_snapshot(assets, &snapshot, &expected),
        "sidecar exposes a donor frame with body and blade planes");
  for (unsigned i = 0; i < 128; ++i) test_zero_colors[i] = (uint16_t)(i & 0x7fff);
  for (unsigned tier = 0; tier < 3; ++tier)
    for (unsigned i = 0; i < 16; ++i)
      test_charge_colors[tier][i] = (uint16_t)((((i + tier + 1) * 3) & 31) |
          ((((i + tier + 2) * 5) & 31) << 5) |
          ((((i + tier + 3) * 7) & 31) << 10));
  test_zero_state = (MmxZeroState){0};
  test_zero_state.charge = 30;
  for (unsigned frame = 0; frame < 8; ++frame) {
    MmxRenderPlayerOverlay actual;
    test_zero_state.charge_phase = (uint8_t)frame;
    native_flash[frame] = MmxZeroBodyColors(&test_zero_state) != test_zero_colors + 16;
    check(MmxSaberRenderResolveSnapshot(assets, snapshot, &actual),
          "charged donor frame resolves");
    overlay_flash[frame] = actual.palette != base;
    check(actual.body.pixels == expected->body.pixels &&
              actual.blade.pixels == expected->blade.pixels,
          "charge palette selection preserves both donor planes");
    if (native_flash[frame]) {
      expected_flash_palette(expected_palette, base, palette_count,
                             MmxZeroBodyColors(&test_zero_state));
      check(!memcmp(actual.palette, expected_palette,
                    (size_t)palette_count * sizeof(*base)),
            "flashing donor palette matches the old role mapping");
    } else {
      check(actual.palette == base,
            "non-flashing charge frame keeps the donor palette");
    }
  }
  check(!memcmp(native_flash, overlay_flash, sizeof(native_flash)) &&
            native_flash[0] && native_flash[1] && !native_flash[2] &&
            !native_flash[3] && native_flash[4] && native_flash[5],
        "donor palette flashes on exactly native Zero's body frames");

  test_zero_state.charge_phase = 0;
  MmxRenderPlayerOverlay active;
  unsigned body_pixel = first_pixel(&expected->body);
  unsigned blade_pixel = first_pixel(&expected->blade);
  check(MmxSaberRenderResolveSnapshot(assets, snapshot, &active),
        "active flash frame resolves for body and blade color assertion");
  expected_flash_palette(expected_palette, base, palette_count,
                         MmxZeroBodyColors(&test_zero_state));
  check(active.palette[body_pixel] == expected_palette[body_pixel] &&
            active.palette[blade_pixel] == expected_palette[blade_pixel],
        "body and blade both use the flashing donor palette");

  test_zero_state.charge = 0;
  bool no_charge_flash = false;
  for (unsigned frame = 0; frame < 8; ++frame) {
    MmxRenderPlayerOverlay actual;
    test_zero_state.charge_phase = (uint8_t)frame;
    check(MmxSaberRenderResolveSnapshot(assets, snapshot, &actual),
          "uncharged donor frame resolves");
    no_charge_flash |= actual.palette != base;
  }
  check(!no_charge_flash, "no charge never flashes the donor palette");
  test_zero_state = (MmxZeroState){0};
}

static void check_tuple(const MmxSaberAssets *assets, unsigned animation_index,
                        const OldRenderTuple *expected) {
  const uint16_t animation_id = (uint16_t)(animation_index + 1);
  const MmxSaberFrame *frame = MmxSaberAssetsFrameForStep(
      assets, animation_id, expected->step);
  MmxSaberAttackSnapshot snapshot = {
    kKinds[animation_index], kIndices[animation_index], SABER_PHASE_ACTIVE,
    expected->tick, (uint8_t)animation_id, 0, 0x40};
  MmxRenderPlayerOverlay actual;

  check(frame != NULL, "old tuple resolves to a sidecar frame");
  check(frame->source_id == animation_id &&
            frame->source_frame_id == expected->source_frame &&
            frame->body.origin_x == expected->body_x &&
            frame->body.origin_y == expected->body_y &&
            frame->body.width == expected->body_width &&
            frame->body.height == expected->body_height &&
            frame->blade.pixels == NULL && frame->blade.width == 0 &&
            frame->blade.height == 0 && frame->blade.origin_x == 0 &&
            frame->blade.origin_y == 0 && frame->blade_layer == 0,
        "hard-coded old sidecar tuple is unchanged");
  check(MmxSaberRenderResolveSnapshot(assets, snapshot, &actual),
        "Saber resolver accepts the live-shaped attack snapshot");
  check(actual.active && check_plane(&actual.body, &frame->body) &&
            check_plane(&actual.blade, &frame->blade) &&
            actual.blade_layer == frame->blade_layer &&
            actual.palette == MmxSaberAssetsPalette(assets) &&
            actual.palette_count == MmxSaberAssetsPaletteCount(assets) &&
            !actual.facing_left,
        "resolver returns the exact donor planes, palette, layer, and facing");
}

static void ride_matrix_checks(const MmxSaberAssets *assets) {
  uint8_t ram[0x20000] = {0};
  bool matrix_ok = true;
  const MmxSaberAnimation *animation =
      MmxSaberAssetsAnimationById(assets, 0x006b);

  ram[0x0baa] = 0x2c;
  ram[0x0e18] = 1;
  ram[0x0e22] = 0x40;
  for (unsigned group = 0; group < 2; ++group) {
    ram[0x0bbe] = (uint8_t)(group ? 0x6b : 0x6a);
    for (unsigned facing = 0; facing < 2; ++facing) {
      ram[0x0bb9] = (uint8_t)(facing ? 0x40 : 0);
      for (unsigned pose = 0; pose < 23; ++pose) {
        MmxRenderPlayerOverlay actual;
        const MmxSaberFrame *expected = MmxSaberAssetsFrameForStep(
            assets, 0x006b, (uint16_t)pose);
        bool resolved = MmxSaberRenderResolveRide(assets, ram, &actual);
        matrix_ok &= resolved && expected && actual.active &&
            actual.body.pixels == expected->body.pixels &&
            actual.body.width == expected->body.width &&
            actual.body.height == expected->body.height &&
            actual.body.origin_x == expected->body.origin_x &&
            actual.body.origin_y == expected->body.origin_y &&
            expected->source_frame_id == kRideSourceFrames[pose] &&
            actual.palette == MmxSaberAssetsPalette(assets) &&
            actual.palette_count == MmxSaberAssetsPaletteCount(assets) &&
            actual.blade.pixels == NULL && actual.blade.width == 0 &&
            actual.blade.height == 0 && actual.blade_layer == 0 &&
            animation && actual.facing_left == ((facing != 0) ^
                                                (animation->facing_xor != 0));
        ram[0x0bbf] = (uint8_t)(pose + 1);
      }
      ram[0x0bbf] = 0;
    }
  }
  check(matrix_ok,
        "Ride Armor resolver maps both pilot groups, all 23 poses, palette, and facing");

  ram[0x0baa] = 0x10;
  ram[0x0bbe] = 0x6b;
  MmxRenderPlayerOverlay inactive;
  check(!MmxSaberRenderResolveRide(assets, ram, &inactive) &&
            !inactive.active,
        "a non-riding action never resolves a Ride Armor overlay");

  ram[0x0baa] = 0x2c;
  ram[0x0bbf] = 23;
  const MmxSaberFrame *pose_zero = MmxSaberAssetsFrameForStep(assets, 0x006b, 0);
  MmxRenderPlayerOverlay fallback;
  check(pose_zero && MmxSaberRenderResolveRide(assets, ram, &fallback) &&
            fallback.body.pixels == pose_zero->body.pixels,
        "an out-of-range native pose falls back to ride pose zero");
}

static void wave_sequence_checks(const MmxSaberWave *wave) {
  static const uint8_t expected_frames[34] = {
    0, 0, 1, 1, 2, 2, 3, 3,
    0, 0, 1, 1, 2, 2, 3, 3,
    0, 0, 1, 1, 2, 2, 3, 3,
    0, 0, 1, 1, 2, 2, 3, 3,
    0, 0,
  };
  for (unsigned age = 0; age < sizeof(expected_frames); ++age) {
    MmxRenderWorldSprite actual;
    const MmxSaberWaveFrame *expected = MmxSaberWaveFrameAt(
        wave, expected_frames[age]);
    check(expected != NULL &&
              MmxSaberRenderResolveWaveSnapshot(wave, (uint8_t)age,
                  320, 224, false, &actual),
          "wave resolver accepts every old-cycle age");
    check(actual.pixels == expected->pixels && actual.width == expected->width &&
              actual.height == expected->height &&
              actual.origin_x == expected->origin_x &&
              actual.origin_y == expected->origin_y && actual.world_x == 320 &&
              actual.world_y == 224 && !actual.facing_left &&
              actual.palette == MmxSaberWavePalette(wave) &&
              actual.palette_count == MmxSaberWavePaletteCount(wave) &&
              actual.z == 0xa680,
          "wave resolver selects the hard-coded old two-tick frame sequence");
  }
  MmxRenderWorldSprite right, left;
  check(MmxSaberRenderResolveWaveSnapshot(wave, 2, 320, 224, false, &right) &&
            MmxSaberRenderResolveWaveSnapshot(wave, 2, 320, 224, true, &left) &&
            !right.facing_left && left.facing_left,
        "wave resolver carries the slot facing into the mirror flag");
}

int main(void) {
  char path[4096];
  char ride_path[4096];
  char wave_path[4096];
  char reason[128] = {0};
  MmxSaberAssets *assets;
  MmxSaberAssets *ride;
  MmxSaberWave *wave;

  if (!MMX_SABER_RENDER_CACHE_DIR[0] ||
      !join_path(path, sizeof(path), MMX_SABER_RENDER_CACHE_DIR,
                 "saber-v1.bin")) {
    fprintf(stderr, "FAIL: Saber render cache directory is not configured\n");
    return 1;
  }
  FILE *file = fopen(path, "rb");
  if (!file) {
    printf("SKIPPED: private Saber sidecar is absent (%s)\n", path);
    return 77;
  }
  fclose(file);

  assets = MmxSaberAssetsLoadFile(path, kSaberManifestSha,
                                  reason, sizeof(reason));
  if (!assets) {
    fprintf(stderr, "FAIL: Saber sidecar rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    return 1;
  }
  if (!join_path(ride_path, sizeof(ride_path), MMX_SABER_RENDER_CACHE_DIR,
                 "ride-zero-v1.bin")) {
    MmxSaberAssetsFree(assets);
    fprintf(stderr, "FAIL: Ride Armor cache path is not configured\n");
    return 1;
  }
  file = fopen(ride_path, "rb");
  if (!file) {
    printf("SKIPPED: private Ride Armor sidecar is absent (%s)\n", ride_path);
    MmxSaberAssetsFree(assets);
    return 77;
  }
  fclose(file);
  ride = MmxSaberAssetsLoadFile(ride_path, kRideManifestSha,
                                reason, sizeof(reason));
  if (!ride) {
    fprintf(stderr, "FAIL: Ride Armor sidecar rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    MmxSaberAssetsFree(assets);
    return 1;
  }
  if (!join_path(wave_path, sizeof(wave_path), MMX_SABER_RENDER_CACHE_DIR,
                 "x3-saber-wave-v1.bin")) {
    MmxSaberAssetsFree(ride);
    MmxSaberAssetsFree(assets);
    fprintf(stderr, "FAIL: Saber wave cache path is not configured\n");
    return 1;
  }
  file = fopen(wave_path, "rb");
  if (!file) {
    printf("SKIPPED: private Saber wave sidecar is absent (%s)\n", wave_path);
    MmxSaberAssetsFree(ride);
    MmxSaberAssetsFree(assets);
    return 77;
  }
  fclose(file);
  wave = MmxSaberWaveLoadFile(wave_path, reason, sizeof(reason));
  if (!wave) {
    fprintf(stderr, "FAIL: Saber wave sidecar rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    MmxSaberAssetsFree(ride);
    MmxSaberAssetsFree(assets);
    return 1;
  }

  for (unsigned animation = 0; animation < 7; ++animation)
    for (unsigned tuple = 0; tuple < 4; ++tuple)
      check_tuple(assets, animation, &kOldTuples[animation][tuple]);
  ride_matrix_checks(ride);

  MmxSaberAttackReset();
  MmxSaberAttackStep(true, true, true, 0, 0);
  MmxRenderPlayerOverlay live;
  check(MmxSaberRenderResolve(assets, NULL, &live) && live.active &&
            live.body.origin_x == 46 && live.body.origin_y == 44,
        "live attack state resolves ground animation 1");
  MmxSaberAttackReset();
  check(!MmxSaberRenderResolve(assets, NULL, &live) && !live.active,
        "idle attack state resolves inactive");
  check(!MmxSaberRenderResolve(NULL, NULL, &live) && !live.active,
        "missing sidecar resolves inactive");
  wave_sequence_checks(wave);

  MmxSaberAssets *flash_assets = make_flash_assets();
  check(flash_assets != NULL, "in-memory body-and-blade flash sidecar parses");
  charge_flash_checks(flash_assets);
  MmxSaberAssetsFree(flash_assets);

  MmxSaberWaveFree(wave);
  MmxSaberAssetsFree(ride);
  MmxSaberAssetsFree(assets);
  puts("MMX SABER RENDER CHECKS PASSED");
  return 0;
}
