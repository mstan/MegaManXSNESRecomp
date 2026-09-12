/* Game identity and widescreen policy; the desktop loop belongs to snesrecomp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "desktop/host_main.h"
#include "desktop/config.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"
#include "widescreen.h"
#include "mmx_rtl.h"
#include "mmx_display.h"
#include "mmx_wide_preview.h"
#include "mmx_spc_player.h"
#include "mmx_default_config.h"

#ifndef MMX_VARIANT_JP
#define MMX_VARIANT_JP 0
#endif
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif
#ifndef MMX_DESKTOP_ENTRY
#define MMX_DESKTOP_ENTRY main
#endif
extern const RtlGameInfo kMmxGameInfo;

static void MmxPrepareFrame(int dw, int dh, int *w, int *h) {
  (void)dw; (void)dh;
  *w = MmxDisplay_ComputeFrameWidth(g_config.widescreen);
  int extra = PpuWsExtraOverride();
  if (extra >= 0) *w = g_config.widescreen ? 256 + 2 * extra : 256;
  *h = 224;
}
static void MmxConfigureHud(void) {
  /* MMX reserves OAM slots 0-15 for HUD sprites. Life/weapon bars hug the
   * native left edge and boss health hugs the right; keep both attached to
   * the corresponding widescreen border during live stage gameplay. */
  /* $C3 mirrors HDMAEN, not the gameplay mode. Spark Mandrill's light-streak
   * effect temporarily enables channels 6/7 ($C3 = $C0); using bit 7 as part
   * of this gate made the HUD snap back to its native positions throughout
   * the effect. $D1/$D2 remain the stable stage/gameplay discriminator. */
  bool in_stage = g_ws_active && g_ram[0x00d1] == 0x02 &&
                  g_ram[0x00d2] == 0x04;
  /* PpuSetWsHudOamShift originally carried fixed x<=24 / x>=216 edge
   * thresholds and applied to every scanline. The shared renderer now
   * requires games to publish the HUD band explicitly; preserve MMX's
   * original working anchors while limiting them to the measured top HUD. */
  PpuSetWsHudOamBand(g_ppu, in_stage ? 96 : 0, 25, 216);
  PpuSetWsHudOamShift(g_ppu, in_stage ? 16 : 0);
  /* Boss health bars are NOT in the fixed HUD reserve: they are general-pool
   * objects whose OAM slots (16+) legitimately hold X's own body or enemy
   * sprites outside boss fights (measured: Highway gameplay = X's parts,
   * Storm Eagle pre-fight = the boss body, fight = the bar at slots 16-22).
   * Anchoring that range unconditionally would side-shift gameplay sprites
   * at the screen-top edges, so detect the bar by its OAM signature every
   * frame - a vertical run of HUD-palette bar-segment tiles hugging the
   * native right edge - and anchor exactly the matching run. */
  {
    int bar_first = -1, bar_len = 0;
    if (in_stage) {
      int run_first = -1, run_len = 0;
      for (int slot = 16; slot <= 48; slot++) {
        uint16 w0 = g_ppu->oam[slot * 2];
        uint16 w1 = g_ppu->oam[slot * 2 + 1];
        uint8 x = (uint8)(w0 & 0xff);
        uint8 y = (uint8)(w0 >> 8);
        uint8 tile = (uint8)(w1 & 0xff);
        uint8 attr = (uint8)(w1 >> 8);
        uint8 x_hi = (uint8)((g_ppu->highOam[slot >> 2] >> ((slot & 3) * 2)) & 1);
        bool seg = !x_hi && x >= 216 && y < 96 && ((attr >> 1) & 7) == 2 &&
                   (tile == 128 || tile == 130 || tile == 132 ||
                    tile == 134 || tile == 170);
        if (seg) {
          if (run_first < 0)
            run_first = slot;
          run_len++;
        } else if (run_first >= 0) {
          break;
        }
      }
      /* A real bar is a stack of >= 4 segment sprites; anything shorter is
       * treated as coincidental gameplay tiles and left native. */
      if (run_len >= 4) {
        bar_first = run_first;
        bar_len = run_len;
      }
    }
    PpuSetWsHudOamShiftRange2(g_ppu, bar_first >= 0 ? (uint8)bar_first : 0,
                              bar_first >= 0 ? (uint8)bar_len : 0);
  }
  /* MMX draws its HUD with OBJ, leaving BG3 available for stage overlays.
   * Widen it during gameplay so effects such as Launch Octopus's foreground
   * water filter cover the side margins as well as the native viewport.
   * The API reserves zero for disabled; scanline 0 is hidden by overscan. */
  PpuSetWidescreenBg3Widen(g_ppu, in_stage ? 1 : 0);
  PpuSetWidescreenLineEnhancer(
      g_ppu, (g_ws_active && MmxWidePreview_IsMarginEnhancerReady())
                 ? MmxWidePreview_EnhancePpuLine : NULL,
      NULL);
}

void MmxDisplay_SetWidescreenEnabled(bool enabled) {
  snesrecomp_desktop_set_widescreen(enabled);
}
bool MmxDisplay_IsWidescreenEnabled(void) { return g_config.widescreen; }
bool MmxDisplay_IsWidescreenActive(void) { return g_ws_active; }
int MmxDisplay_GetCurrentFrameWidth(void) { return snesrecomp_desktop_frame_width(); }

/* Resolve one BG2 8x8 tile directly from MMX's decompressed level map.
 *
 * The game keeps a 32x32 table of 256px screen IDs at $EC00.  Each screen
 * expands to a 16x16 table of 16px metatile IDs at $A600, and each metatile
 * points at four raw SNES tilemap entries through $0B98-$0B9A.  The vanilla
 * B5DE/B670 streamer performs this same lookup, but only when a column reaches
 * the native 4:3 edge.  Reading the retained map here lets the renderer seed
 * first-visit widescreen margins without advancing the guest's camera, DMA
 * queue, or rolling-map bookkeeping. */
static uint16_t MmxDisplay_ResolveBg2Tile(uint16_t tile_x, uint16_t tile_y) {
  uint16_t px = (uint16_t)(tile_x << 3);
  uint16_t py = (uint16_t)(tile_y << 3);
  uint16_t screen_addr = (uint16_t)(0xec00 + ((px >> 8) & 0x1f) +
                                    (((py >> 8) & 0x1f) << 5));
  uint8_t screen = g_ram[screen_addr];
  uint16_t metatile_addr = (uint16_t)(0xa600 + ((uint16_t)screen << 9) +
                                      ((px & 0x00f0) >> 3) +
                                      ((py & 0x00f0) << 1));
  uint16_t metatile = (uint16_t)(g_ram[metatile_addr] |
                                 (g_ram[(uint16_t)(metatile_addr + 1)] << 8));
  uint16_t tiledef = (uint16_t)(g_ram[0x0b98] |
                                (g_ram[0x0b99] << 8));
  uint8_t tiledef_bank = g_ram[0x0b9a];
  uint16_t quadrant = (uint16_t)(((py & 8) ? 4 : 0) +
                                 ((px & 8) ? 2 : 0));
  uint16_t addr = (uint16_t)(tiledef + (metatile << 3) + quadrant);
  return (uint16_t)(cart_read(g_snes->cart, tiledef_bank, addr) |
                    (cart_read(g_snes->cart, tiledef_bank,
                               (uint16_t)(addr + 1)) << 8));
}

static void MmxDisplay_PrefillBg2Shadow(uint16_t h, uint16_t v,
                                        uint32_t shadow_x,
                                        uint32_t shadow_y) {
  static int s_enabled = -1;
  if (s_enabled < 0) {
    const char *e = getenv("SNESRECOMP_WS_BG2_PREFILL");
    s_enabled = (e && e[0] == '0') ? 0 : 1;
  }
  if (!s_enabled)
    return;

  /* Storm Eagle's boss roof replaces BG2 with a scrolling sky, but the
   * retained level map still describes the lower airport.  Treating that
   * map as exact margin history exposes the old airport at the native edges
   * until the live sky has scrolled through them.  The sky is periodic and
   * is already handled exactly by WsShadowSetPeriodicFold, so leave these
   * margins unseeded for the upper roof only. */
  uint8_t stage = g_ram[0x1f7a];
  uint16_t camera_y = (uint16_t)(g_ram[0x1e50] | (g_ram[0x1e51] << 8));
  if (stage == 5 && camera_y >= 0x0300)
    return;

  /* Seed only the off-native columns.  The center remains authentic VRAM,
   * and WsShadowTile never consults this cache for screen X 0..255. */
  int margin = (g_ws_extra + 7) & ~7;
  int x_ranges[2][2] = {{-margin, -1}, {256, 255 + margin}};
  /* PPU scroll wraps at 1024px, but the streamer's retained coordinates do
   * not.  Slot 0, for example, renders h=22 while BG2's source X is $0416;
   * keeping that high part selects the correct level-screen record. */
  uint16_t stream_h = (uint16_t)(g_ram[0x1e8d] | (g_ram[0x1e8e] << 8));
  uint16_t stream_v = (uint16_t)(g_ram[0x1e90] | (g_ram[0x1e91] << 8));
  int16_t dh = (int16_t)((h - stream_h) & 0x03ff);
  int16_t dv = (int16_t)((v - stream_v) & 0x03ff);
  if (dh >= 512) dh -= 1024;
  if (dv >= 512) dv -= 1024;
  stream_h = (uint16_t)(stream_h + dh);
  stream_v = (uint16_t)(stream_v + dv);
  int guest_ty0 = (int)(stream_v >> 3);
  int guest_ty1 = (int)((stream_v + 231) >> 3);
  int32_t shadow_tile_dx = ((int32_t)shadow_x - (int32_t)stream_h) >> 3;
  int32_t shadow_tile_dy = ((int32_t)shadow_y - (int32_t)stream_v) >> 3;

  for (int range = 0; range < 2; range++) {
    if (x_ranges[range][0] > x_ranges[range][1])
      continue;
    int guest_tx0 = ((int)stream_h + x_ranges[range][0]) >> 3;
    int guest_tx1 = ((int)stream_h + x_ranges[range][1]) >> 3;
    for (int guest_tx = guest_tx0; guest_tx <= guest_tx1; guest_tx++) {
      /* At a hard left stage boundary, keep the off-stage destination
       * columns but reflect their source into the first real BG2 tiles.
       * Clamping guest_tx0 to zero made the initial Highway left range
       * 0..-1, so the prefill ran zero iterations until the camera moved
       * past the widescreen margin. This mirrors BG1's edge policy. */
      int sample_tx = guest_tx < 0 ? -guest_tx - 1 : guest_tx;
      for (int guest_ty = guest_ty0; guest_ty <= guest_ty1; guest_ty++) {
        uint32_t world_tx = (uint32_t)(guest_tx + shadow_tile_dx);
        uint32_t world_ty = (uint32_t)(guest_ty + shadow_tile_dy);
        uint16_t entry = MmxDisplay_ResolveBg2Tile((uint16_t)sample_tx,
                                                   (uint16_t)guest_ty);
        /* At 16:9, Highway's leading gutter reaches farther than the prepared
         * half of its rolling BG2 map. OnVramWrite can therefore mark circular
         * VRAM contents as authoritative world history before those cells are
         * genuinely staged; Prefill correctly refuses to replace them. The
         * first attempted fix forced only x>=272 before camera $0180. Live
         * testing exposed both artificial boundaries: an interior dead strip
         * at x=256..271 and flat purple cells returning near the first pit.
         * Highway's retained BG2 map is exact and static, so make it the
         * authoritative source for the complete leading gutter throughout
         * stage 0. Other stages and the trailing gutter remain history-first. */
        int screen_tile_x = (int)(world_tx << 3) - (int)shadow_x;
        if (stage == 0 && range == 1 && screen_tile_x >= 256)
          WsShadowForceTile(1, world_tx, world_ty, entry);
        else
          WsShadowPrefillTile(1, world_tx, world_ty, entry);
      }
    }
  }
}

static void MmxDisplay_PrepareBg2Shadow(void) {
  static bool s_was_active;
  static uint64_t s_generation;
  if (s_generation != RtlStateGeneration()) {
    s_generation = RtlStateGeneration();
    s_was_active = false;
    WsShadowReset();
  }
  static int s_fold_enabled = -1;
  if (s_fold_enabled < 0) {
    const char *e = getenv("SNESRECOMP_WS_BG2_FOLD");
    s_fold_enabled = (e && e[0] == '0') ? 0 : 1;
  }
  bool active = s_fold_enabled && g_ws_active && g_ram[0x00d1] == 0x02 &&
                !(g_ram[0x00c3] & 0x80) && g_ram[0x00d2] == 0x04;

  if (!active) {
    if (s_was_active)
      WsShadowReset();
    s_was_active = false;
    /* A frame with no registration deactivates the renderer-side shadow. */
    WsShadowFrame(g_ppu);
    return;
  }

  /* The BG2 map holds two 256px world chunks with fixed half parity,
   * rewritten a whole half at a time as camera-line triggers fire
   * (WS-STAGE biases those ~margin early). Its content is a mix of
   * horizontally periodic filler (sky, repeating city glow) and
   * world-anchored features (towers, the lower highway). Margins layer
   * three sources, best first:
   *   1. periodic fold — rows whose natively displayed columns prove an
   *      exact period fold margins onto fresh native columns;
   *   2. world-keyed history — world-anchored rows serve columns
   *      captured while they scrolled through the native view, keyed by
   *      the unwrapped BG2 scroll below;
   *   3. plain map wrap — correct whenever the fetched half already
   *      holds the right chunk (the early-fired staging makes that the
   *      common case for the leading margin). */
  /* Key presentation-only history to the scroll actually rendered this
   * frame. MMX advances its WRAM shadow before the corresponding PPU write,
   * which otherwise makes the widescreen margins alternate one frame early. */
  uint16_t h = (uint16_t)(g_ppu->hScroll[1] & 0x03ff);
  uint16_t v = (uint16_t)(g_ppu->vScroll[1] & 0x03ff);
  static uint32_t s_world_x, s_world_y;
  static uint16_t s_prev_h, s_prev_v;
  if (!s_was_active) {
    WsShadowReset();
    s_world_x = (uint32_t)h + 2048;
    s_world_y = (uint32_t)v + 1024;
  } else {
    int32_t dx = (int32_t)((h - s_prev_h) & 0x03ff);
    int32_t dy = (int32_t)((v - s_prev_v) & 0x03ff);
    if (dx >= 512) dx -= 1024;
    if (dy >= 512) dy -= 1024;
    s_world_x = (uint32_t)((int32_t)s_world_x + dx);
    s_world_y = (uint32_t)((int32_t)s_world_y + dy);
  }
  s_prev_h = h;
  s_prev_v = v;
  s_was_active = true;
  WsShadowSetWorld(1, s_world_x, s_world_y);
  WsShadowSetBlankTile(1, -1);
  WsShadowSetPeriodicFold(1);
  WsShadowFrame(g_ppu);
  MmxDisplay_PrefillBg2Shadow(h, v, s_world_x, s_world_y);
}


static void MmxBeginFrame(unsigned number) {
  (void)number;
  MmxConfigureHud();
  MmxDisplay_PrepareBg2Shadow();
  extern void MmxWsChrRebindSweep(void);
  MmxWsChrRebindSweep();
}
static int MmxDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                        int w, int h, double alpha) {
  (void)alpha;
  if (!g_ws_active) return 0;
  /* The preview's optional compositor writes into a packed frame. */
  static uint8_t pixels[(256 + 2 * kWsExtraMax) * 240 * 4];
  memcpy(pixels, field, (size_t)w * h * 4);
  MmxWidePreview_Draw(pixels, w, h, g_ws_extra);
  RtlWidescreenPresent(dst, pitch, pixels, w, h);
  return 1;
}
static int MmxWindowWidth(int w) {
  return MmxDisplay_GetWindowBaseWidth(w, SnesDisplayAspect_Clamp(g_config.display_aspect));
}

/* Keep the existing benchmark entry points while using the host's run limit. */
static int s_benchmark_frames, s_benchmark_audio;
static void MmxAfterConfig(void) {
#if !MMX_VARIANT_JP
  /* The shared catalog now lives in mods/preloaded. Carry the player's old
   * activation state forward once, without replacing newer choices. */
  FILE *state = fopen("mods/preloaded/state.toml", "rb");
  if (state) {
    fclose(state);
  } else {
    FILE *legacy = fopen("mods/state.toml", "rb");
    if (legacy) {
      state = fopen("mods/preloaded/state.toml", "wb");
      if (state) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), legacy)) != 0)
          if (fwrite(buf, 1, n, state) != n) break;
        int failed = ferror(legacy) || ferror(state);
        if (fclose(state)) failed = 1;
        if (failed) remove("mods/preloaded/state.toml");
      }
      fclose(legacy);
    }
  }
#endif
  if (s_benchmark_frames) {
    g_config.enable_audio = s_benchmark_audio != 0;
    g_config.autosave = false;
    g_config.disable_frame_delay = true;
    g_config.skip_launcher = true;
    g_config.fullscreen = 0;
    g_config.output_method = kOutputMethod_SDL;
  }
}
static void MmxAfterFrame(const SnesDesktopHostFrameStats *stats) {
  if (s_benchmark_frames && stats->frame == (unsigned)s_benchmark_frames) {
    double seconds = stats->run_seconds;
    printf("SNESRECOMP_BENCHMARK {\"game\":\"Mega Man X\",\"frames\":%u,"
           "\"seconds\":%.9f,\"fps\":%.3f,\"ms_per_frame\":%.6f}\n",
           stats->frame, seconds, stats->frame / seconds,
           seconds * 1000.0 / stats->frame);
  }
}

int MMX_DESKTOP_ENTRY(int argc, char **argv) {
  ConfigUseStateMenuDefaults();
  if (argc >= 3 && (!strcmp(argv[1], "--benchmark") ||
                    !strcmp(argv[1], "--benchmark-audio"))) {
    s_benchmark_audio = !strcmp(argv[1], "--benchmark-audio");
    s_benchmark_frames = atoi(argv[2]);
    if (s_benchmark_frames <= 0) return 1;
#ifdef _WIN32
    _putenv_s("SNESRECOMP_RUN_FRAMES", argv[2]);
    _putenv_s("SNESRECOMP_NO_LAUNCHER", "1");
#else
    setenv("SNESRECOMP_RUN_FRAMES", argv[2], 1);
    setenv("SNESRECOMP_NO_LAUNCHER", "1", 1);
#endif
    for (int i = 1; i + 2 <= argc; ++i) argv[i] = argv[i + 2];
    argc -= 2;
  }
  static const SnesDesktopHostGame game = {
#if MMX_VARIANT_JP
    .display_name = "Rockman X", .region = "Japan",
    .rom_file = "rockmanx.sfc",
    .expected_crc32_hex = "5584641E",
    .expected_sha256_hex = "76f80cdf704a0e1daf1af5bbf564e427b425a5ee42329417de6f29219fe63e5f",
#else
    .display_name = "Mega Man X", .region = "USA",
    .rom_file = "mmx.sfc", .game_id = "megaman-x-us",
    .expected_crc32_hex = "DED53C64",
    .expected_sha256_hex = "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32",
#endif
    .build_version = SNESRECOMP_BUILD_VERSION,
    .game_info = &kMmxGameInfo,
    .default_config_ini = kMmxDefaultConfig,
    .env_prefix = "MMX", .debug_port = 4377,
    .native_widescreen = 1, .state_menu_hotkeys = 1,
    .display_aspect_supported = 1, .shader_supported = 1,
    /* Widescreen and MSU-1 remain USA Mods features. */
    .create_spc_player = SmwSpcPlayer_Create,
    .prepare_frame = MmxPrepareFrame, .begin_sim_frame = MmxBeginFrame,
    .draw_frame = MmxDrawFrame,
    .window_base_width = MmxWindowWidth,
    .window_base_height = MmxDisplay_GetWindowBaseHeight,
    .after_config = MmxAfterConfig, .after_run_frame = MmxAfterFrame,
  };
  return snesrecomp_desktop_main(&game, argc, argv);
}
