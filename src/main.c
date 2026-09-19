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
#include "mmx_renderer.h"
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
extern int mmx_tier2_diagnostics_enabled(void);

static bool s_render_valid;
static void MmxRomLoaded(const uint8_t *rom, size_t size) {
  if ((size & 0x7fff) == 512) { rom += 512; size -= 512; }
  MmxRendererSetRom(rom, size);
}

static void MmxPrepareFrame(int dw, int dh, int *w, int *h) {
  g_mmx_custom_renderer = !MMX_VARIANT_JP && g_config.widescreen;
  g_mmx_custom_view = MmxRendererViewport(g_mmx_custom_aspect, dw, dh);
  *w = g_mmx_custom_renderer ? g_mmx_custom_view.width : 256;
  *h = 224;
}
void MmxDisplay_SetWidescreenEnabled(bool enabled) {
  snesrecomp_desktop_set_widescreen(enabled);
}
bool MmxDisplay_IsWidescreenEnabled(void) { return g_config.widescreen; }
bool MmxDisplay_IsWidescreenActive(void) {
  return g_mmx_custom_renderer && g_mmx_custom_view.extra;
}
int MmxDisplay_GetCurrentFrameWidth(void) { return snesrecomp_desktop_frame_width(); }

static void MmxBeforeFrame(void) {
  /* MMX's draw hook runs the original per-line HDMA sequence itself. */
  snes_set_hdma_beam_enabled(g_snes, false);
  if (g_mmx_custom_renderer) MmxRendererLatchSprites();
}
static void MmxResetRenderer(void) {
  MmxRendererReset();
  s_render_valid = false;
}
static void MmxBeginFrame(unsigned number) {
  (void)number;
  if (!g_mmx_custom_renderer) return;
  uint32_t flags = g_config.new_renderer ? kPpuRenderFlags_NewRenderer : 0;
  if (g_config.no_sprite_limits) flags |= kPpuRenderFlags_NoSpriteLimits;
  PpuBeginDrawing(g_ppu, g_ppu->renderBuffer, 256 * 4, flags);
  /* The shared host raster stays native. All extension and HUD placement
   * belong to the compositor, never the PPU's rolling tilemap. */
  PpuSetWsHudOamShift(g_ppu, 0);
  PpuSetWsHudOamShiftRange2(g_ppu, 0, 0);
  PpuSetWidescreenBg3Widen(g_ppu, 0);
  PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL);
  extern void MmxWsChrRebindSweep(void);
  MmxWsChrRebindSweep();
  MmxRendererBeginFrame(g_ram);
}
static void MmxEndFrame(const uint8_t *field, unsigned number) {
  if (!g_mmx_custom_renderer) return;
  s_render_valid = MmxRendererEndFrame((const uint32_t *)field);
  const char *capture = getenv("MMX_RENDER_CAPTURE");
  const char *after = getenv("MMX_RENDER_CAPTURE_FRAME");
  if (capture && number == (unsigned)(after ? atoi(after) : 1))
    MmxRendererSaveCapture(capture);
}
static int MmxDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                        int w, int h, double alpha) {
  (void)alpha;
  if (!g_mmx_custom_renderer) return 0;
  static uint32_t output[MMX_RENDER_MAX_WIDTH * 224];
  if (!s_render_valid || !MmxRendererDraw(output, g_mmx_custom_view, g_mmx_custom_hud)) {
    memset(output, 0, (size_t)w * h * sizeof(*output));
    for (int y = 0; y < h; ++y)
      memcpy(output + y * w + (w - 256) / 2, field + y * 256 * 4, 256 * 4);
  }
  RtlWidescreenPresent(dst, pitch, (const uint8_t *)output, w, h);
  return 1;
}
static void MmxViewport(int w, int h, int dw, int dh, SnesDisplayViewport *view) {
  if (g_mmx_custom_renderer) *view = MmxRendererDestination(g_mmx_custom_view, dw, dh);
  else MmxDisplay_ComputeViewport(w, h, dw, dh,
      SnesDisplayAspect_Clamp(g_config.display_aspect), g_config.ignore_aspect_ratio, false, view);
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
  for (int arg = 1; arg + 1 < argc; ++arg) {
    if (strcmp(argv[arg], "--benchmark") && strcmp(argv[arg], "--benchmark-audio")) continue;
    s_benchmark_audio = !strcmp(argv[arg], "--benchmark-audio");
    s_benchmark_frames = atoi(argv[arg + 1]);
    if (s_benchmark_frames <= 0) return 1;
#ifdef _WIN32
    _putenv_s("SNESRECOMP_RUN_FRAMES", argv[arg + 1]);
    _putenv_s("SNESRECOMP_NO_LAUNCHER", "1");
#else
    setenv("SNESRECOMP_RUN_FRAMES", argv[arg + 1], 1);
    setenv("SNESRECOMP_NO_LAUNCHER", "1", 1);
#endif
    for (int i = arg; i + 2 <= argc; ++i) argv[i] = argv[i + 2];
    argc -= 2;
    break;
  }
  static RtlGameInfo mmx_game_info;
  mmx_game_info = kMmxGameInfo;
  mmx_game_info.tier2_capture = mmx_tier2_diagnostics_enabled();

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
    .game_info = &mmx_game_info,
    .default_config_ini = kMmxDefaultConfig,
    .env_prefix = "MMX", .debug_port = 4377,
    .native_widescreen = 0, .state_menu_hotkeys = 1,
    .display_aspect_supported = 1, .shader_supported = 1,
    /* Widescreen and MSU-1 remain USA Mods features. */
    .create_spc_player = SmwSpcPlayer_Create,
    .prepare_frame = MmxPrepareFrame, .begin_sim_frame = MmxBeginFrame,
    .draw_frame = MmxDrawFrame, .end_sim_frame = MmxEndFrame,
    .before_run_frame = MmxBeforeFrame, .on_reset = MmxResetRenderer,
    .on_rom_loaded = MmxRomLoaded, .compute_viewport = MmxViewport,
    .window_base_width = MmxWindowWidth,
    .window_base_height = MmxDisplay_GetWindowBaseHeight,
    .after_config = MmxAfterConfig, .after_run_frame = MmxAfterFrame,
  };
  return snesrecomp_desktop_main(&game, argc, argv);
}
