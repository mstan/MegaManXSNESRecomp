#include "mmx_renderer.h"
#include "mmx_weapon_combat.h"
#include "mmx_weapons.h"
#include "mmx_zero.h"
#include "saber/mmx_saber_assets.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool MmxKncBugfixActive(unsigned seat) { (void)seat;return false; }
unsigned MmxKncBugfixPhase(void) { return 0; }

#ifndef MMX_SABER_RENDER_CACHE_DIR
#define MMX_SABER_RENDER_CACHE_DIR ""
#endif
#ifndef MMX_SABER_DRAW_ZERO_PATH
#define MMX_SABER_DRAW_ZERO_PATH "saber-render-draw-zero.bin"
#endif
#ifndef MMX_SABER_DRAW_WEAPON_PATH
#define MMX_SABER_DRAW_WEAPON_PATH "saber-render-draw-weapon.bin"
#endif

static const uint8_t kSaberManifestSha[32] = {
  0x4e, 0x29, 0x1e, 0x5f, 0x03, 0x57, 0xaf, 0xa0,
  0x35, 0x76, 0x14, 0xe0, 0xc4, 0x97, 0xf9, 0xb3,
  0x65, 0xee, 0x99, 0xe3, 0x70, 0x64, 0x5d, 0x84,
  0x99, 0xb2, 0xe4, 0xfd, 0x07, 0xf8, 0x0f, 0xd0,
};

enum { ANCHOR_X = 40, ANCHOR_Y = 40, REGION_X = 32, REGION_Y = 34,
       REGION_WIDTH = 16, REGION_HEIGHT = 10 };

static uint8_t ram[0x20000], rom[0x100000];
static Ppu ppu;
static uint32_t stock[256 * 224], output[MMX_RENDER_MAX_WIDTH * 224];
static uint32_t right_output[256 * 224];
static MmxRenderPlayerOverlay provider_overlay;
static MmxRenderWorldSprite provider_world[8];
static unsigned provider_world_count;
static MmxRenderDebugRect provider_debug[64];
static unsigned provider_debug_count;
static uint8_t body_pixels[] = {
  1, 2, 0, 0, 0,
  0, 3, 2, 0, 0,
  0, 0, 0, 3, 1,
};
static uint8_t blade_pixels[] = {1, 3, 1};
static const uint8_t world_pixels[] = {
  1, 2, 0,
  3, 1, 2,
};
static const uint16_t overlay_palette[] = {0, 31, 31 << 5, 31 << 10};

static void check(bool okay, const char *message) {
  if (!okay) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
  printf("ok: %s\n", message);
}

static void put_word(uint8_t *bytes, unsigned address, unsigned value) {
  bytes[address] = (uint8_t)value;
  bytes[address + 1] = (uint8_t)(value >> 8);
}

static uint32_t expand_color(uint16_t color) {
  unsigned red = color & 31, green = (color >> 5) & 31, blue = color >> 10;
  red = (red << 3) | (red >> 2);
  green = (green << 3) | (green >> 2);
  blue = (blue << 3) | (blue >> 2);
  return red << 16 | green << 8 | blue;
}

static uint32_t checksum_region(const uint32_t *pixels) {
  uint32_t checksum = 2166136261u;
  for (int y = REGION_Y; y < REGION_Y + REGION_HEIGHT; ++y)
    for (int x = REGION_X; x < REGION_X + REGION_WIDTH; ++x)
      checksum = (checksum ^ pixels[y * 256 + x]) * 16777619u;
  return checksum;
}

static bool provide_overlay(const uint8_t *ram, const MmxZeroState *zero,
                            MmxRenderPlayerOverlay *out) {
  (void)ram;
  (void)zero;
  *out = provider_overlay;
  return true;
}

static unsigned provide_world(MmxRenderWorldSprite *out, unsigned max) {
  unsigned count = provider_world_count < max ? provider_world_count : max;
  if (count) memcpy(out, provider_world, count * sizeof(*out));
  return count;
}

static unsigned provide_debug(MmxRenderDebugRect *out, unsigned max) {
  unsigned count = provider_debug_count < max ? provider_debug_count : max;
  if (count) memcpy(out, provider_debug, count * sizeof(*out));
  return count;
}

static void write_zero_asset(void) {
  FILE *file = fopen(MMX_SABER_DRAW_ZERO_PATH, "wb");
  const uint8_t header[] = {
    'M', 'M', 'X', 'Z', 'E', 'R', 'O', '6',
    128, 0, 128, 0, 64, 0, 64, 0, 117, 0, 35, 0,
  };
  uint8_t palette[256] = {0};
  uint8_t bounds[40] = {0};
  uint8_t hud[160] = {0};
  uint8_t animation[MMX_ZERO_ANIMATION_BYTES] = {0};
  uint8_t emission[MMX_ZERO_MUZZLE_BYTES] = {0};
  uint8_t page[MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT] = {0};

  check(file != NULL, "temporary native Zero cache opens");
  palette[2] = 31;
  for (unsigned i = 0; i < sizeof(bounds); i += 4) {
    bounds[i + 2] = 1;
    bounds[i + 3] = 1;
  }
  for (unsigned i = 0; i < 136; ++i) {
    animation[i * 2] = 0x10;
    animation[i * 2 + 1] = 1;
  }
  animation[0x110] = 1;
  page[64 * MMX_ZERO_WIDTH + 64] = 1;
  check(fwrite(header, sizeof(header), 1, file) == 1 &&
            fwrite(palette, sizeof(palette), 1, file) == 1 &&
            fwrite(bounds, sizeof(bounds), 1, file) == 1 &&
            fwrite(hud, sizeof(hud), 1, file) == 1 &&
            fwrite(animation, sizeof(animation), 1, file) == 1 &&
            fwrite(emission, sizeof(emission), 1, file) == 1,
        "temporary native Zero cache header writes");
  bool poses_written = true;
  for (unsigned pose = 0; pose < MMX_ZERO_POSES; ++pose) {
    poses_written &= fwrite(page, sizeof(page), 1, file) == 1;
    memset(page, 0, sizeof(page));
  }
  check(poses_written && fclose(file) == 0 &&
            MmxZeroLoad(MMX_SABER_DRAW_ZERO_PATH),
        "temporary native Zero cache loads");
  remove(MMX_SABER_DRAW_ZERO_PATH);
}

static void scene(void) {
  memset(&ppu, 0, sizeof(ppu));
  memset(ram, 0, sizeof(ram));
  memset(rom, 0, sizeof(rom));
  memset(stock, 0, sizeof(stock));
  MmxRendererSetPlayerOverlayProvider(NULL);
  MmxRendererSetWorldSpriteProvider(NULL);
  MmxRendererSetDebugRectProvider(NULL);
  provider_world_count = 0;
  provider_debug_count = 0;
  MmxRendererReset();
  MmxRendererSetRom(rom, sizeof(rom));
  g_mmx_custom_renderer = true;
  g_mmx_expanded_sprites = false;
  g_mmx_render_asset_repairs = false;

  ram[0xd1] = 2;
  ram[0xd2] = 4;
  ram[0xd3] = 4;
  ram[0xba9] = 2;
  ram[0xbcf] = 16;
  ram[0xbbf] = 0;
  put_word(ram, 0xbad, ANCHOR_X);
  put_word(ram, 0xbb0, ANCHOR_Y + 8);
  put_word(ram, 0x1e4d, 0);
  put_word(ram, 0x1e50, 0);

  ppu.inidisp = 15;
  ppu.bgmode = 1;
  ppu.screenEnabled[0] = 16;
  for (unsigned i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xe000;
  ppu.oam[32] = 0x2828;
  ppu.oam[33] = 0x2000;

  put_word(ram, 0, ANCHOR_X);
  put_word(ram, 2, ANCHOR_Y);
  put_word(ram, 0x18, 0x8000);
  ram[0x1a] = 0x80;
  ram[0xf] = 0x20;
  MmxRendererObserveObject(ram, 0xba8);
  MmxRendererRecordPiece(ram, 0);
  MmxRendererLatchSprites();
}

static void render_view(MmxRenderView view);

static void render(void) {
  render_view((MmxRenderView){256, 0, 4.0 / 3.0});
}

static void render_view(MmxRenderView view) {
  memset(output, 0, sizeof(output));
  MmxRendererBeginFrame(ram);
  for (unsigned y = 1; y <= 224; ++y) MmxRendererCaptureLine(&ppu, y);
  check(MmxRendererEndFrame(stock), "offscreen renderer frame captures");
  check(MmxRendererDraw(output, view, false),
        "offscreen renderer frame draws");
}

static void capture_and_draw(MmxRenderView view) {
  for (unsigned y = 1; y <= 224; ++y) MmxRendererCaptureLine(&ppu, y);
  check(MmxRendererEndFrame(stock), "offscreen renderer snapshot captures");
  check(MmxRendererDraw(output, view, false),
        "offscreen renderer snapshot draws");
}

static void write_weapon_asset(void) {
  enum { RECORD_BYTES = 612 + 38 + 5 + 8 + 1, SIZE = 12 + 8 * RECORD_BYTES };
  uint8_t *data = (uint8_t *)calloc(SIZE, 1);
  FILE *file;
  check(data != NULL, "temporary weapon-effect cache allocates");
  memcpy(data, "MMXWEAP5", 8);
  put_word(data, 8, 8);
  put_word(data, 10, 0);
  for (unsigned weapon = 0; weapon < 8; ++weapon) {
    unsigned pos = 12 + weapon * RECORD_BYTES;
    data[pos] = 2;
    data[pos + 1] = (uint8_t)(weapon + 1);
    data[pos + 2] = 1;
    put_word(data, pos + 612, weapon == 3 ? 70 : weapon + 1);
    put_word(data, pos + 614, 1);
    put_word(data, pos + 616, 5);
    /* The effect uses group-color index 1; make it blue so the world sprite's
     * later red pixel proves that the lower-z weapon effect was present. */
    put_word(data, pos + 620, 31 << 10);
    data[pos + 650] = 1;
    put_word(data, pos + 659, 1);
    put_word(data, pos + 661, 1);
    data[pos + 663] = 1;
  }
  file = fopen(MMX_SABER_DRAW_WEAPON_PATH, "wb");
  check(file != NULL && fwrite(data, SIZE, 1, file) == 1 &&
            fclose(file) == 0,
        "temporary weapon-effect cache writes");
  free(data);
}

static MmxRenderPlayerOverlay synthetic_overlay(bool left, uint8_t layer) {
  MmxRenderPlayerOverlay overlay = {0};
  overlay.active = true;
  overlay.body = (MmxRenderPlayerOverlayPlane){
    body_pixels, 5, 3, 61, 62};
  overlay.blade = (MmxRenderPlayerOverlayPlane){
    blade_pixels, 3, 1, 62, 63};
  overlay.blade_layer = layer;
  overlay.palette = overlay_palette;
  overlay.palette_count = sizeof(overlay_palette) / sizeof(overlay_palette[0]);
  overlay.facing_left = left;
  return overlay;
}

static MmxRenderWorldSprite synthetic_world(bool left, int32_t x, int32_t y,
                                            uint16_t z) {
  return (MmxRenderWorldSprite){
    world_pixels, 3, 2, 0, 0, x, y, left, overlay_palette,
    sizeof(overlay_palette) / sizeof(overlay_palette[0]), z};
}

static void world_sprite_geometry_and_snapshot(void) {
  MmxRenderWorldSprite snapshot[8];
  const MmxRenderView narrow = {256, 0, 4.0 / 3.0};
  const MmxRenderView wide = {342, 43, 16.0 / 9.0};

  scene();
  render_view(narrow);
  memcpy(right_output, output, sizeof(right_output));

  scene();
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(narrow);
  check(!memcmp(right_output, output, sizeof(right_output)),
        "an empty world snapshot preserves the native renderer output exactly");

  scene();
  provider_world[0] = synthetic_world(false, 40, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(narrow);
  check(MmxRendererWorldSpriteSnapshot(snapshot, 8) == 1 &&
            snapshot[0].world_x == 40 && snapshot[0].world_y == 40,
        "BeginFrame captures one independent world sprite snapshot");
  check(output[40 * 256 + 40] == 0xff0000 &&
            output[40 * 256 + 41] == 0x00ff00 &&
            output[41 * 256 + 40] == 0x0000ff &&
            output[41 * 256 + 41] == 0xff0000 &&
            output[41 * 256 + 42] == 0x00ff00,
        "world coordinates and signed origin land on the expected pixels");

  scene();
  provider_world[0] = synthetic_world(true, 40, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(narrow);
  check(output[40 * 256 + 39] == 0xff0000 &&
            output[40 * 256 + 38] == 0x00ff00 &&
            output[41 * 256 + 39] == 0x0000ff &&
            output[41 * 256 + 38] == 0xff0000 &&
            output[41 * 256 + 37] == 0x00ff00,
        "left-facing world sprites mirror around the world anchor");

  scene();
  provider_world[0] = synthetic_world(false, -1, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(narrow);
  check(output[40 * 256] == 0x00ff00 &&
            output[41 * 256] == 0xff0000,
        "partially offscreen world sprites clip at the narrow view edge");

  scene();
  provider_world[0] = synthetic_world(false, 0, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(wide);
  check(output[40 * wide.width + wide.extra] == 0xff0000 &&
            output[40 * wide.width + wide.extra + 2] == 0,
        "world sprites clip and project through the widescreen margin");

  /* Change the provider's live record after BeginFrame. Draw must retain the
   * copied world coordinate, just as it retains the player overlay snapshot. */
  scene();
  provider_world[0] = synthetic_world(false, 40, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  MmxRendererBeginFrame(ram);
  provider_world[0].world_x = 80;
  capture_and_draw(narrow);
  check(output[40 * 256 + 40] == 0xff0000 &&
            output[40 * 256 + 80] == 0,
        "world coordinates are snapshotted before mid-frame provider changes");
}

static void debug_rect_geometry_and_snapshot(void) {
  MmxRenderDebugRect snapshot[64];
  const MmxRenderView narrow = {256, 0, 4.0 / 3.0};
  const MmxRenderView wide = {342, 43, 16.0 / 9.0};

  scene();
  render_view(narrow);
  memcpy(right_output, output, sizeof(right_output));

  scene();
  MmxRendererSetDebugRectProvider(provide_debug);
  render_view(narrow);
  check(!memcmp(right_output, output, sizeof(right_output)) &&
            MmxRendererDebugRectSnapshot(snapshot, 64) == 0,
        "an empty debug-rectangle snapshot preserves the native output exactly");

  scene();
  provider_debug[0] = (MmxRenderDebugRect){40, 40, 3, 2, 0x001f};
  provider_debug_count = 1;
  MmxRendererSetDebugRectProvider(provide_debug);
  render_view(narrow);
  check(MmxRendererDebugRectSnapshot(snapshot, 64) == 1 &&
            snapshot[0].world_x == 40 && snapshot[0].w == 3,
        "BeginFrame captures one independent debug-rectangle snapshot");
  check(output[40 * 256 + 40] == 0xff0000 &&
            output[40 * 256 + 41] == 0xff0000 &&
            output[40 * 256 + 42] == 0xff0000 &&
            output[41 * 256 + 40] == 0xff0000 &&
            output[41 * 256 + 41] == 0xff0000 &&
            output[41 * 256 + 42] == 0xff0000,
        "debug rectangles draw a one-pixel outlined box at narrow width");

  scene();
  provider_debug[0] = (MmxRenderDebugRect){-1, 40, 3, 2, 0x001f};
  provider_debug_count = 1;
  MmxRendererSetDebugRectProvider(provide_debug);
  render_view(narrow);
  check(output[40 * 256] == 0xff0000 &&
            output[40 * 256 + 1] == 0xff0000 &&
            output[40 * 256 + 2] == 0 &&
            output[41 * 256] == 0xff0000 &&
            output[41 * 256 + 1] == 0xff0000,
        "offscreen debug rectangles clip safely at the narrow edge");

  scene();
  provider_debug[0] = (MmxRenderDebugRect){0, 40, 3, 2, 0x03e0};
  provider_debug_count = 1;
  MmxRendererSetDebugRectProvider(provide_debug);
  render_view(wide);
  check(output[40 * wide.width + wide.extra] == 0x00ff00 &&
            output[40 * wide.width + wide.extra + 1] == 0x00ff00 &&
            output[40 * wide.width + wide.extra + 2] == 0x00ff00 &&
            output[40 * wide.width + wide.extra + 3] == 0,
        "debug rectangles use the object camera and clip at widescreen width");

  scene();
  provider_debug[0] = (MmxRenderDebugRect){40, 40, 3, 2, 0x001f};
  provider_debug_count = 1;
  MmxRendererSetDebugRectProvider(provide_debug);
  MmxRendererBeginFrame(ram);
  provider_debug[0].world_x = 80;
  capture_and_draw(narrow);
  check(output[40 * 256 + 40] == 0xff0000 &&
            output[40 * 256 + 80] == 0,
        "debug rectangles are snapshotted before mid-frame provider changes");
}

static void world_sprite_composes_after_weapon_effect(void) {
  MmxWeaponCombatState combat = {0};
  const MmxRenderView view = {256, 0, 4.0 / 3.0};

  write_weapon_asset();
  check(MmxWeaponsLoadPage(MMX_SABER_DRAW_WEAPON_PATH, 1),
        "temporary weapon-effect cache loads");
  combat.shots[0].active = 1;
  combat.shots[0].age = 1;
  combat.shots[0].page = 1;
  combat.shots[0].weapon = 4;
  combat.shots[0].group = 70;
  combat.shots[0].pose = 0;
  combat.shots[0].x = 40 << 8;
  combat.shots[0].y = 40 << 8;

  scene();
  MmxWeaponsSetCombatState(combat);
  render_view(view);
  check(output[40 * 256 + 40] == 0x0000ff,
        "the synthetic weapon effect occupies the lower-z pixel first");

  scene();
  provider_world[0] = synthetic_world(false, 40, 40, 0xb680);
  provider_world_count = 1;
  MmxRendererSetWorldSpriteProvider(provide_world);
  render_view(view);
  check(output[40 * 256 + 40] == 0xff0000,
        "the world sprite draws above a lower-z weapon-effect pixel");

  MmxWeaponsDisable();
  check(remove(MMX_SABER_DRAW_WEAPON_PATH) == 0,
        "temporary weapon-effect cache removes");
}

static void inactive_matches_baseline(void) {
  scene();
  render();
  memcpy(right_output, output, sizeof(right_output));

  scene();
  memset(&provider_overlay, 0, sizeof(provider_overlay));
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  check(!memcmp(right_output, output, sizeof(right_output)),
        "inactive overlay preserves the native renderer output exactly");
}

static void body_geometry_and_mirror(void) {
  scene();
  provider_overlay = synthetic_overlay(false, 0);
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  check(checksum_region(output) == 0xd95cc1edu,
        "right-facing donor body matches the golden region checksum");
  check(output[38 * 256 + 37] == 0xff0000 &&
            output[38 * 256 + 38] == 0x00ff00 &&
            output[39 * 256 + 38] == 0x0000ff &&
            output[40 * 256 + 40] == 0x0000ff,
        "right-facing donor body lands on the signed-origin pixel probes");
  memcpy(right_output, output, sizeof(right_output));

  scene();
  provider_overlay = synthetic_overlay(true, 0);
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  check(checksum_region(output) == 0xc8ca190du,
        "left-facing donor body matches the golden region checksum");
  check(output[38 * 256 + 42] == 0xff0000 &&
            output[38 * 256 + 41] == 0x00ff00 &&
            output[39 * 256 + 41] == 0x0000ff &&
            output[40 * 256 + 39] == 0x0000ff,
        "left-facing donor body lands on the mirrored pixel probes");
  for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) {
    int mirror_x = 2 * ANCHOR_X - 1 - x;
    uint32_t expected = mirror_x >= 0 && mirror_x < 256 ?
        right_output[y * 256 + mirror_x] : 0;
    if (output[y * 256 + x] != expected) {
      check(false, "left-facing donor body is the exact anchor mirror");
      return;
    }
  }
  check(true, "left-facing donor body is the exact anchor mirror");
}

static void pilot_scene(uint8_t group) {
  scene();
  ram[0x0baa] = 0x2c;
  ram[0x0e18] = 1;
  ram[0x0e22] = 0x40;
  ram[0xba8 + 0x16] = group;
  ram[0xba8 + 0x17] = 0;
  MmxRendererReset();
  MmxRendererObserveObject(ram, 0xba8);
  MmxRendererRecordPiece(ram, 0);
  MmxRendererLatchSprites();
}

static void pilot_overlay_owns_submission(void) {
  static const uint32_t expected[3][5] = {
    {0xff0000, 0x00ff00, 0, 0, 0},
    {0, 0x0000ff, 0x00ff00, 0, 0},
    {0, 0, 0, 0x0000ff, 0xff0000},
  };
  for (unsigned group = 0; group < 2; ++group) {
    pilot_scene((uint8_t)(group ? 0x6b : 0x6a));
    provider_overlay = synthetic_overlay(false, 0);
    MmxRendererSetPlayerOverlayProvider(provide_overlay);
    render();
    bool exact = true;
    for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) {
      uint32_t value = 0;
      if (y >= 38 && y < 41 && x >= 37 && x < 42)
        value = expected[y - 38][x - 37];
      if (output[y * 256 + x] != value) exact = false;
    }
    check(exact,
          "an active player overlay replaces the native X/menu pose for both pilot groups");
  }
}

static void blade_order(void) {
  scene();
  provider_overlay = synthetic_overlay(false, 1);
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  check(output[39 * 256 + 38] == 0x0000ff &&
            output[39 * 256 + 39] == 0x00ff00 &&
            output[39 * 256 + 40] == 0xff0000,
        "layer-1 blade is behind body pixels and visible through transparency");

  scene();
  provider_overlay = synthetic_overlay(false, 2);
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  check(output[39 * 256 + 38] == 0xff0000 &&
            output[39 * 256 + 39] == 0x0000ff &&
            output[39 * 256 + 40] == 0xff0000,
        "layer-2 blade is in front of body pixels");
}

static void donor_origin_and_palette(const MmxSaberAssets *assets) {
  const MmxSaberFrame *frame = MmxSaberAssetsFrameForStep(assets, 1, 0);
  MmxRenderPlayerOverlay donor = {0};
  bool found = false;

  donor.active = true;
  donor.body = (MmxRenderPlayerOverlayPlane){
    frame->body.pixels, frame->body.width, frame->body.height,
    frame->body.origin_x, frame->body.origin_y};
  donor.palette = MmxSaberAssetsPalette(assets);
  donor.palette_count = MmxSaberAssetsPaletteCount(assets);
  /* The body-origin/palette probe is independent of blade overlap. Ordering is
   * covered by the controlled planes in blade_order(). */
  scene();
  provider_overlay = donor;
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  render();
  for (unsigned row = 0; row < frame->body.height && !found; ++row)
    for (unsigned col = 0; col < frame->body.width && !found; ++col) {
      unsigned pixel = frame->body.pixels[row * frame->body.width + col];
      int x = ANCHOR_X + frame->body.origin_x + (int)col - 64;
      int y = ANCHOR_Y - 64 + frame->body.origin_y + (int)row;
      if (pixel && pixel < donor.palette_count && x >= 0 && x < 256 &&
          y >= 0 && y < 224) {
        found = true;
        check(output[y * 256 + x] == expand_color(donor.palette[pixel]),
              "actual donor origin and palette reach the custom renderer");
      }
    }
  check(found, "actual donor frame exposes an in-view body pixel");
}

static void invisible_zero_is_empty(void) {
  scene();
  provider_overlay = synthetic_overlay(false, 2);
  MmxRendererSetPlayerOverlayProvider(provide_overlay);
  ppu.oam[32] = 0xe000;
  ppu.oam[33] = 0;
  render();
  bool any = false;
  for (int y = 0; y < 224 && !any; ++y)
    for (int x = 0; x < 256; ++x)
      any |= output[y * 256 + x] != 0;
  check(!any, "an invisible Zero OAM piece draws no overlay pixels");
}

int main(void) {
  char path[4096];
  char reason[128] = {0};
  MmxSaberAssets *assets;

  if (!MMX_SABER_RENDER_CACHE_DIR[0] ||
      snprintf(path, sizeof(path), "%s/%s", MMX_SABER_RENDER_CACHE_DIR,
               "saber-v1.bin") >= (int)sizeof(path)) {
    printf("SKIPPED: private Saber sidecar is not configured\n");
    return 77;
  }
  FILE *file = fopen(path, "rb");
  if (!file) {
    printf("SKIPPED: private Saber sidecar is absent (%s)\n", path);
    return 77;
  }
  fclose(file);
  assets = MmxSaberAssetsLoadFile(path, kSaberManifestSha, reason,
                                  sizeof(reason));
  if (!assets) {
    fprintf(stderr, "FAIL: Saber sidecar rejected: %s\n",
            reason[0] ? reason : "unknown reason");
    return 1;
  }
  check(MmxSaberAssetsFrameForStep(assets, 1, 0) != NULL,
        "private sidecar supplies the renderer donor frame");

  write_zero_asset();
  inactive_matches_baseline();
  debug_rect_geometry_and_snapshot();
  world_sprite_geometry_and_snapshot();
  world_sprite_composes_after_weapon_effect();
  body_geometry_and_mirror();
  pilot_overlay_owns_submission();
  blade_order();
  donor_origin_and_palette(assets);
  invisible_zero_is_empty();
  MmxSaberAssetsFree(assets);
  MmxRendererSetPlayerOverlayProvider(NULL);
  MmxRendererSetWorldSpriteProvider(NULL);
  MmxZeroDisable();
  g_mmx_custom_renderer = false;
  g_mmx_render_asset_repairs = true;
  return 0;
}
