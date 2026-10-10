/* Developer hitbox overlay: retail boot to Highway, then the real draw hook. */
#include "mmx_hitbox_overlay.h"

static void hitbox_boot(void) {
  zero_replay(1400);
  for (unsigned n = 0; n < 3; ++n) { coop_frames(30, SNES_PAD_START, 0); if (n < 2) zero_replay(181); }
  unsigned wait;
  for (wait = 0; wait < 3600; ++wait) {
    MmxCoopState s = MmxCoopGetState();
    if (g_ram[0xd1] == 2 && g_ram[0xd2] == 4 && g_ram[0xd3] == 4 &&
        (!MmxCoopEnabled() || (s.initialized && s.players[1].status == MMX_COOP_ALIVE &&
                               !s.stage_pending && !MmxCoopTransitionActive()))) break;
    coop_frames(1, 0, 0);
  }
  check(wait < 3600, "retail boot reaches Highway gameplay");
}
static void hitbox_bmp(const uint32_t *pixels, unsigned width, const char *suffix) {
  const char *base = getenv("MMX_ZERO_TEST_CAPTURE");
  if (!base) return;
  char path[4096]; snprintf(path, sizeof(path), "%s.%s.bmp", base, suffix);
  FILE *f = fopen(path, "wb"); check(f != NULL, "hitbox image opens");
  uint32_t bytes = width * 224 * 4;
  uint32_t header[] = {54 + bytes, 0, 54, 40, width, (uint32_t)-224, 0x200001, 0, bytes, 0, 0, 0, 0};
  fwrite("BM", 1, 2, f); fwrite(header, sizeof(header), 1, f); fwrite(pixels, bytes, 1, f); fclose(f);
}
/* The box drawn for the live player, which must enclose its origin. */
static const MmxRenderDebugRect *hitbox_player(const MmxRenderDebugRect *r, unsigned n,
                                               int x, int y) {
  for (unsigned i = 0; i < n; ++i)
    if (r[i].rgb555 == MMX_HITBOX_PLAYER && x >= r[i].world_x && x < r[i].world_x + r[i].w &&
        y >= r[i].world_y && y < r[i].world_y + r[i].h) return &r[i];
  return NULL;
}
static uint32_t hitbox_pixels[MMX_RENDER_MAX_WIDTH * 224];
static void hitbox_overlay_checks(const char *assets, uint8_t *start, uint8_t *expected,
                                  uint8_t *actual, size_t cap) {
  bool coop = assets && assets[0];
  if (coop) {
    check(MmxZeroLoad(assets), "hitbox co-op loads owned X3 Zero");
    MmxZeroRegisterHooks(); MmxCoopRegisterHooks(); check(MmxCoopEnable(0), "hitbox co-op enables");
  }
  hitbox_boot();
  coop_frames(40, SNES_PAD_RIGHT, SNES_PAD_RIGHT | SNES_PAD_Y);
  /* Drawing must not change the game. */
  size_t n = RtlSaveSnapshotToMemory(start, cap);
  coop_frames(30, SNES_PAD_Y, SNES_PAD_Y);
  size_t en = RtlSaveSnapshotToMemory(expected, cap);
  check(RtlLoadSnapshotFromMemory(start, n), "hitbox start snapshot restores");
  MmxHitboxOverlaySetEnabled(true);
  coop_frames(30, SNES_PAD_Y, SNES_PAD_Y);
  same(expected, en, actual, RtlSaveSnapshotToMemory(actual, cap),
       "the hitbox overlay leaves the simulation unchanged");

  MmxRenderDebugRect rects[64];
  unsigned count = MmxHitboxOverlayProvide(rects, 64);
  int px = (int16_t)(g_ram[0xbad] | g_ram[0xbae] << 8), py = (int16_t)(g_ram[0xbb0] | g_ram[0xbb1] << 8);
  const MmxRenderDebugRect *player = hitbox_player(rects, count, px, py);
  if (!player) {
    unsigned ptr = g_ram[0xbc8] | g_ram[0xbc9] << 8;
    fprintf(stderr, "player %d,%d +20=%04x facing=%02x action=%02x\n", px, py, ptr, g_ram[0xbb9], g_ram[0xbaa]);
    for (unsigned i = 0; i < count; ++i)
      fprintf(stderr, "  rect %04x %d,%d %ux%u\n", rects[i].rgb555, rects[i].world_x, rects[i].world_y, rects[i].w, rects[i].h);
  }
  check(player != NULL, "the live player's native hitbox encloses its origin");
  unsigned shots = 0, players = 0;
  for (unsigned i = 0; i < count; ++i) {
    shots += rects[i].rgb555 == MMX_HITBOX_SHOT || rects[i].rgb555 == MMX_HITBOX_MELEE;
    players += rects[i].rgb555 == MMX_HITBOX_PLAYER;
  }
  fprintf(stderr, "hitbox overlay: %u boxes, %u players, %u shots; player %dx%d at %d,%d\n",
          count, players, shots, player->w, player->h, player->world_x, player->world_y);
  check(shots > 0, "held fire puts player shots in the overlay");

  if (!coop) {
    /* Stock raster: the overlay draws without enabling the compositor,
     * which here only widescreen would otherwise turn on. */
    g_config.widescreen = false; g_mmx_custom_renderer = false;
    frame(0);
    check(!g_mmx_custom_renderer, "the overlay does not turn on the custom compositor");
    check(MmxDrawFrame((uint8_t *)hitbox_pixels, 256 * 4, (const uint8_t *)g_my_pixels,
                       256, 224, 1.0), "the stock frame is drawn with hitboxes");
    count = MmxHitboxOverlayProvide(rects, 64);
    player = hitbox_player(rects, count, (int16_t)(g_ram[0xbad] | g_ram[0xbae] << 8),
                           (int16_t)(g_ram[0xbb0] | g_ram[0xbb1] << 8));
    int sx = player->world_x - (g_ram[0x1e4d] | g_ram[0x1e4e] << 8);
    int sy = player->world_y - (g_ram[0x1e50] | g_ram[0x1e51] << 8);
    check(sx >= 0 && sx < 256 && sy >= 0 && sy < 224 &&
          (hitbox_pixels[sy * 256 + sx] & 0xffffff) == 0x00ff00,
          "the player's box is outlined in green on screen");
    hitbox_bmp(hitbox_pixels, 256, "hitbox-stock");
    MmxHitboxOverlaySetEnabled(false);
    frame(0);
    check(!MmxDrawFrame((uint8_t *)hitbox_pixels, 256 * 4, (const uint8_t *)g_my_pixels,
                        256, 224, 1.0), "turning the overlay off returns the stock frame to the host");
  } else {
    check(players == 2, "co-op outlines both players");
    frame(0);
    int width = g_mmx_custom_view.width;
    check(g_mmx_custom_renderer && width >= 256 && width <= MMX_RENDER_MAX_WIDTH &&
          MmxDrawFrame((uint8_t *)hitbox_pixels, (size_t)width * 4, (const uint8_t *)g_my_pixels,
                       width, 224, 1.0), "the compositor frame is drawn with hitboxes");
    hitbox_bmp(hitbox_pixels, (unsigned)width, "hitbox-coop");
    MmxRenderDebugRect drawn[64];
    check(MmxRendererDebugRectSnapshot(drawn, 64) > 0, "the compositor takes the overlay's boxes");
  }
  puts("MMX HITBOX OVERLAY CHECKS PASSED");
}
