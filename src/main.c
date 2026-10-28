/* Game identity and widescreen policy; the desktop loop belongs to snesrecomp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "desktop/host_main.h"
#include "desktop/config.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/apu.h"
#include "snes/ws_shadow.h"
#include "widescreen.h"
#include "mmx_rtl.h"
#include "mmx_display.h"
#include "mmx_renderer.h"
#include "mmx_armor.h"
#include "mmx_zero.h"
#include "mmx_knc_bugfix.h"
#include "mmx_hitbox_overlay.h"
#include "mmx_weapon_combat.h"
#include "mmx_weapons.h"
#include "snes/cart.h"
#include "mmx_spc_player.h"
#include "mmx_default_config.h"
#include "mmx_netplay.h"
#include "mmx_startup.h"
#include "mmx_coop_trace.h"
#include "mmx_coop.h"
#include "mmx_coop_view.h"
#include "mmx_replay.h"
#include "desktop/sdl_compat.h"
#include "mmx_boss_rush.h"
#include "mmx_boss_rush_audio.h"
#include "netplay/snes_netplay.h"
#include "host_paths.h"
#include <ctype.h>

#ifndef MMX_VARIANT_JP
#define MMX_VARIANT_JP 0
#endif
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif
#ifndef MMX_DESKTOP_ENTRY
#define MMX_DESKTOP_ENTRY main
#endif
#ifndef MMX_NATIVE_ROW_PLAN
#define MMX_NATIVE_ROW_PLAN 0
#endif
extern const RtlGameInfo kMmxGameInfo;
extern int mmx_tier2_diagnostics_enabled(void);

static bool s_render_valid;
static uint64_t s_benchmark_native_lines;
static uint32_t s_composed_pixels[MMX_RENDER_MAX_WIDTH * 224];
#if MMX_NATIVE_RASTER
static bool s_composed_ready;
static MmxRenderView s_composed_view;
static bool s_composed_hud;
#endif
static void MmxRomLoaded(const uint8_t *rom, size_t size) {
  if ((size & 0x7fff) == 512) { rom += 512; size -= 512; }
  MmxRendererSetRom(rom, size);
}

static void MmxPrepareFrame(int dw, int dh, int *w, int *h) {
  /* A room has one logical field. Window size and each peer's offline pixel
   * preference cannot widen it. The offline setting is never overwritten. */
  SnesDisplayAspect aspect = SnesDisplayAspect_Clamp(MmxNetplayActive() ? 0 : g_config.display_aspect);
  g_mmx_custom_renderer = !MMX_VARIANT_JP && (g_config.widescreen || MmxZeroEnabled() || MmxWeaponsEnabled() || MmxArmorEnabled() || MmxKncBugfixActive(0) || MmxKncBugfixActive(1) || MmxBossRushActive() || MmxBossRushGetState().menu || MmxRendererNeedsStormSkyRepair(g_ram));
  g_mmx_custom_view = MmxRendererViewport(g_mmx_custom_aspect, dw, dh,
      aspect);
  if (!g_config.widescreen) {
    int pw, ph;
    MmxDisplay_ComputePresentationSize(256, 224,
        aspect, &pw, &ph);
    g_mmx_custom_view = (MmxRenderView){256, 0, (double)pw / ph};
  }
  MmxReplayViewport(&g_mmx_custom_view.width,&g_mmx_custom_view.extra);
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
#if SNESRECOMP_TRACE
  MmxArmorDebugHostFrame();
#endif
  if(!MmxReplayPlaying()) {
#if SNESRECOMP_NET
  MmxCoopViewsSetOnline(snes_netplay_active() && MmxCoopEnabled() && MmxCoopViewsIndependent());
  MmxRendererSetPeerView(MmxCoopViewsOnline()?snes_netplay_local_slot():-1);
#else
  MmxCoopViewsSetOnline(false);MmxRendererSetPeerView(-1);
#endif
  }
  /* MMX's draw hook runs the original per-line HDMA sequence itself. */
  snes_set_hdma_beam_enabled(g_snes, false);
  if (g_snes->cart) {
    MmxZeroSetCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
    MmxWeaponsCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
    MmxCoopHostFrame();
    MmxBossRushHostFrame();
  }
  MmxRendererHoldFrame(MmxCoopTransitionActive());
  if (g_mmx_custom_renderer) MmxRendererLatchSprites();
}
static void MmxResetRenderer(void) {
  MmxKncBugfixReset();
  MmxRendererReset();
  s_render_valid = false;
#if MMX_NATIVE_RASTER
  s_composed_ready=false;
#endif
}
static void MmxBeginFrame(unsigned number) {
  (void)number;
  if (!g_mmx_custom_renderer) { MmxHitboxOverlayBeginStockFrame(); return; }
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
  MmxCoopState coop = MmxCoopGetState();
  MmxRendererCoopFrame(MmxCoopEnabled() ? &coop : NULL);
}
static void MmxEndFrame(const uint8_t *field, unsigned number) {
  if (!g_mmx_custom_renderer) return;
  s_render_valid = MmxRendererEndFrame((const uint32_t *)field);
  s_benchmark_native_lines+=MmxRendererNativeLines();
  const char *capture = getenv("MMX_RENDER_CAPTURE");
  const char *after = getenv("MMX_RENDER_CAPTURE_FRAME");
  if (capture && number == (unsigned)(after ? atoi(after) : 1))
    MmxRendererSaveCapture(capture);
}
#if MMX_NATIVE_RASTER
static void MmxPublishNativeField(uint8_t *field, unsigned number) {
  (void)number;
  if (!g_mmx_custom_renderer) { s_composed_ready=false; return; }
  s_composed_ready=false;
  if (s_render_valid) {
    if (!MmxRendererDraw(s_composed_pixels,g_mmx_custom_view,g_mmx_custom_hud))
      Die("Native frame composition failed");
    s_composed_view=g_mmx_custom_view; s_composed_hud=g_mmx_custom_hud;
    s_composed_ready=true;
    /* This is the primary native-width field consumed by screenshots,
     * snapshots and overlays. The independent original field remains
     * available through MmxRendererStockFrame's explicit readback. */
    for (unsigned y=0;y<224;++y)
      memcpy((uint32_t *)field+y*256,
          s_composed_pixels+y*g_mmx_custom_view.width+g_mmx_custom_view.extra,256*4);
  } else if (MmxRendererNativeLines()) {
    Die("Native raster capture did not complete");
  }
}
#endif
static int MmxDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                        int w, int h, double alpha) {
  (void)alpha;
  uint32_t *output=s_composed_pixels;
  if (!g_mmx_custom_renderer) {
    /* The overlay never enables the compositor: that also widens culling
     * and spawn policy, which would make a drawing aid change the game. */
    if (w != 256 || h != 224 || !MmxHitboxOverlayDrawStock(output, field)) return 0;
    RtlWidescreenPresent(dst, pitch, (const uint8_t *)output, w, h);
    return 1;
  }
  bool ready=false;
#if MMX_NATIVE_RASTER
  ready=s_composed_ready && s_composed_view.width==g_mmx_custom_view.width &&
      s_composed_view.extra==g_mmx_custom_view.extra && s_composed_hud==g_mmx_custom_hud;
  s_composed_ready=false;
#endif
  if (!s_render_valid || (!ready && !MmxRendererDraw(output, g_mmx_custom_view, g_mmx_custom_hud))) {
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
  return MmxDisplay_GetWindowBaseWidth(w, SnesDisplayAspect_Clamp(MmxNetplayActive() ? 0 : g_config.display_aspect));
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
static void MmxShutdown(void) {
  /* SDL owns the worker-thread handles. Drain them before host SDL teardown;
   * the idempotent atexit handlers remain a fallback for earlier exits. */
  MmxReplayShutdown();
  MmxCoopSetDiagnosticsEnabled(false);
}
static void MmxAfterFrame(const SnesDesktopHostFrameStats *stats) {
  if (MmxReplayPlaying() && (MmxReplayFinished() || MmxReplayFailed())) {
    SDL_Event quit = {0}; quit.type = SDL_QUIT; SDL_PushEvent(&quit);
  }
  MmxBossRushAudioPresent();
  /* Netplay resimulates frames and must never pause for disk writes. */
  MmxCoopTraceAfterFrame(!MmxNetplayActive());
  if (s_benchmark_frames && stats->frame == (unsigned)s_benchmark_frames) {
    double seconds = stats->run_seconds;
    printf("SNESRECOMP_BENCHMARK {\"game\":\"Mega Man X\",\"frames\":%u,"
           "\"seconds\":%.9f,\"fps\":%.3f,\"ms_per_frame\":%.6f,\"native_raster_lines\":%llu,"
           "\"apu_time_impl\":\"%s\",\"raster_impl\":\"%s\",\"row_plan\":%s,\"audio_output\":%s}\n",
           stats->frame, seconds, stats->frame / seconds,
           seconds * 1000.0 / stats->frame,(unsigned long long)s_benchmark_native_lines,
           apu_time_implementation(), MMX_NATIVE_RASTER ? "NATIVE" : "LLE",
           MMX_NATIVE_RASTER && MMX_NATIVE_ROW_PLAN ? "true" : "false",
           s_benchmark_audio ? "true" : "false");
  }
}

/* Co-op diagnostics: --coop-trace[=all], else MMX_COOP_TRACE, else
 * logging.ini [Logging] CoopTrace. 0/off disables; "all" adds every frame. */
static unsigned MmxCoopTraceValue(const char *value) {
  if (!value) return MMX_COOP_TRACE_OFF;
  while (isspace((unsigned char)*value)) ++value;
  if (!*value || *value == '0' || !strncmp(value, "off", 3)) return MMX_COOP_TRACE_OFF;
  if (*value == '2' || !strncmp(value, "all", 3)) return MMX_COOP_TRACE_ALL;
  return MMX_COOP_TRACE_ANOMALIES;
}
static void MmxConfigureCoopTrace(int *argc, char **argv) {
  const char *chosen = NULL;
  char ini[1024], line[256];
  for (int arg = 1; arg < *argc; ++arg) {
    if (strcmp(argv[arg], "--coop-trace") && strncmp(argv[arg], "--coop-trace=", 13)) continue;
    chosen = argv[arg][12] == '=' ? argv[arg] + 13 : "1";
    for (int i = arg; i + 1 <= *argc; ++i) argv[i] = argv[i + 1];
    --*argc;
    break;
  }
  if (!chosen) chosen = getenv("MMX_COOP_TRACE");
  FILE *config = !chosen && snesrecomp_exe_dir_path("logging.ini", ini, sizeof(ini)) ? fopen(ini, "r") : NULL;
  static char from_ini[64];
  while (config && fgets(line, sizeof(line), config)) {
    char *p = line;
    while (isspace((unsigned char)*p)) ++p;
    if (strncmp(p, "CoopTrace", 9)) continue;
    p += 9;
    while (isspace((unsigned char)*p)) ++p;
    if (*p != '=') continue;
    snprintf(from_ini, sizeof(from_ini), "%s", p + 1);
    chosen = from_ini;
  }
  if (config) fclose(config);
  MmxCoopTraceConfigure(MmxCoopTraceValue(chosen));
}

/* Every Mega Man X-series SNES dump a player is likely to pick, so a refused
 * ROM is named ("This is Mega Man X (USA) v1.0. Mega Man X needs Mega Man X
 * (USA) v1.1 (Rev 1) ...") instead of only "not recognized". SHA-1 and CRC32
 * are No-Intro's (libretro-database, DAT 2026.10.07); SHA-256 is given only
 * where this project verified it against a real dump. The build's own ROM is
 * the entry whose SHA-256 is expected_sha256_hex; the X2/X3 USA entries also
 * name the files the Mods source-ROM pickers ask for. */
#define MMX_COLLECTION_NOTE "Collection re-releases are not byte-identical to the cartridge; use a cartridge dump."
#if MMX_VARIANT_JP
#define MMX_X1_NOTE "This build is the Japanese Rockman X; Mega Man X dumps have different code."
#define MMX_X2_NOTE "That is a different game (X2)."
#define MMX_X3_NOTE "That is a different game (X3)."
#else
#define MMX_X1_NOTE "v1.0 is the first printing and v1.1 (Rev 1) a later one with different code, so they are not interchangeable."
#define MMX_X2_NOTE "That is Mega Man X2. X2 weapons take it under Mods; the game ROM itself must be Mega Man X."
#define MMX_X3_NOTE "That is Mega Man X3. Zero, co-op and X3 weapons take it under Mods; the game ROM itself must be Mega Man X."
#endif
static const SnesRomRevision kMmxRomCatalog[] = {
  {"Mega Man X (USA) v1.1 (Rev 1)",
   "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32",
   "c65216760ba99178100a10d98457cf11496c2097", "DED53C64", NULL},
  {"Mega Man X (USA) v1.0", NULL,
   "449a00631208fbcc8d58209e66d0d488674b7fb1", "1033EBA4", MMX_X1_NOTE},
  {"Mega Man X (Europe)", NULL,
   "8a32570fad3bfc92c0508c88022fb20412dd7bed", "41965122",
   "The European release is a PAL version with different code and timing."},
  {"Rockman X (Japan) v1.1 (Rev 1)",
   "76f80cdf704a0e1daf1af5bbf564e427b425a5ee42329417de6f29219fe63e5f",
   "03f8f99d27874465f8d3e5ec9628927ac5be6fae", "5584641E", NULL},
  {"Rockman X (Japan) v1.0", NULL,
   "86c18ba1fc762b6d0bcdee7314a29b5c97cac082", "6A1B6926",
   "v1.0 is the first Japanese printing; v1.1 (Rev 1) has different code."},
  {"Mega Man X (Legacy Collection extract)", NULL,
   "14aa83cd2eafad1a9b04187a854e20b33cefc207", "97A10846", MMX_COLLECTION_NOTE},
  {"Rockman X (Legacy Collection extract)", NULL,
   "67cc4bc4c9f23532d72d188ef96b431d3baed401", "94CA42BF", MMX_COLLECTION_NOTE},
  {"Mega Man X (USA) (Mega Man X Collection extract)", NULL,
   "df62ec045c78e9b9184e7c35c9bc3bb8de28afde", "FA542E48", MMX_COLLECTION_NOTE},
  {"Mega Man X (USA) (Capcom Town)", NULL,
   "f43c28fff43a9ee73ea1e01c3cbe8ebfd40f6209", "A7689FC4", MMX_COLLECTION_NOTE},
  {"Mega Man X2 (USA)",
   "f3246755f608a1e1dc9c848b61da3b824c7853b29b3be40df6fc7f2793a887ed",
   "637079014421563283cded6aeaa0604597b2e33c", "947B0355", MMX_X2_NOTE},
  {"Mega Man X2 (Europe)", NULL,
   "5c767285da713de2bc883a6291d32adc9b2d13fa", "3C49C39C", MMX_X2_NOTE},
  {"Rockman X2 (Japan)", NULL,
   "34dc37c8a1905ec5631fa666eba84bb78f9c5bdf", "299D7A89", MMX_X2_NOTE},
  {"Mega Man X2 (Legacy Collection extract)", NULL,
   "4a57802a0ad3817daf1ff60d62e094b0612b5612", "8972DF65", MMX_X2_NOTE},
  {"Rockman X2 (Legacy Collection extract)", NULL,
   "44431b4489bd3726b220d9a879df0a267c57cf2a", "011E5068", MMX_X2_NOTE},
  {"Mega Man X2 (USA) (Mega Man X Collection extract)", NULL,
   "9ee80aff5fdb8dff7eb44b33fda856a4a538bda5", "B8600893", MMX_X2_NOTE},
  {"Mega Man X3 (USA)",
   "65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7",
   "b226f7ec59283b05c1e276e2f433893f45027cac", "FA0FE671", MMX_X3_NOTE},
  {"Mega Man X3 (Europe)", NULL,
   "69a11324aeb57d005800771d6147603d5479b282", "C504DDFD", MMX_X3_NOTE},
  {"Rockman X3 (Japan)", NULL,
   "8e0156fc7d6af6f36b08a5e399c0284c6c5d81b8", "E476912B", MMX_X3_NOTE},
  {"Mega Man X3 (Legacy Collection extract)", NULL,
   "e1259c1bbd8cef0769f193d8eec83536bd15e4d0", "1ACB643D", MMX_X3_NOTE},
  {"Rockman X3 (Legacy Collection extract)", NULL,
   "10b62c6cde8ea29e60b409dc357690dd83811c2e", "741AB27B", MMX_X3_NOTE},
};
#undef MMX_X1_NOTE
#undef MMX_COLLECTION_NOTE
#undef MMX_X2_NOTE
#undef MMX_X3_NOTE

static void MmxConfigureLauncher(struct RecompLauncherCGameInfo *info) {
  MmxArmorConfigureLauncher(info);
  MmxNetplayConfigureLauncher(info);
}
static bool MmxArmorBusterSelected(void) {return !MmxWeaponsCombatActive();}
static uint8_t *MmxArmorBody(uint8_t *ram) {
  return MmxCoopEnabled()?MmxCoopCharacterBody(ram,MMX_COOP_X):MmxZeroActive()?NULL:ram+0xba8;
}
int MMX_DESKTOP_ENTRY(int argc, char **argv) {
  MmxArmorSetBusterQuery(MmxArmorBusterSelected);
  MmxArmorSetActorQuery(MmxCoopActorIsX);
  MmxArmorSetBodyQuery(MmxArmorBody);
  MmxStartupLogging();
  for(int arg=1;arg+1<argc;++arg)
    if(!strcmp(argv[arg],"--config")) MmxReplaySetConfigPath(argv[arg+1]);
  for (int arg = 1; arg < argc; ++arg) {
    if (strcmp(argv[arg], "--replay-diagnostic")) continue;
    if (arg + 1 == argc || !MmxReplayPlayback(argv[arg + 1])) return 2;
    for (int i = arg; i + 2 <= argc; ++i) argv[i] = argv[i + 2];
    argc -= 2; break;
  }
  MmxConfigureCoopTrace(&argc, argv);
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
    .rom_help = "Look for \"Rockman X (Japan) (Rev 1)\". Headered and "
                "headerless copies both work; patched ROMs do not.",
#else
    .display_name = "Mega Man X", .region = "USA",
    .num_players = 2,
    .rom_file = "mmx.sfc", .game_id = "megaman-x-us",
    .expected_crc32_hex = "DED53C64",
    .expected_sha256_hex = "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32",
    .rom_help = "Look for \"Mega Man X (USA) (Rev 1)\". Headered and "
                "headerless copies both work; patched ROMs (MSU-1, hacks) do "
                "not, as Mods adds those.",
#endif
    .rom_revisions = kMmxRomCatalog,
    .num_rom_revisions = (int)(sizeof(kMmxRomCatalog) / sizeof(kMmxRomCatalog[0])),
    .build_version = SNESRECOMP_BUILD_VERSION,
    .game_info = &mmx_game_info,
    .default_config_ini = kMmxDefaultConfig,
    .env_prefix = "MMX", .debug_port = 4377,
    .native_widescreen = 0, .state_menu_hotkeys = 1,
    .display_aspect_supported = 1, .shader_supported = 1,
    /* Rewind's ring has always been in the runtime and reachable on F8; this
     * puts its switch, depth and interval on the Settings page and persists
     * them, so a player can size it or turn it off without an env var. */
    .rewind_settings = 1,
    /* Ctrl+L / Select+L3 reopens the launcher over the frozen game (#46). */
    .in_game_launcher = 1,
    /* Widescreen and MSU-1 remain USA Mods features. */
    .create_spc_player = SmwSpcPlayer_Create,
    .prepare_frame = MmxPrepareFrame, .begin_sim_frame = MmxBeginFrame,
    .draw_frame = MmxDrawFrame, .end_sim_frame = MmxEndFrame,
#if MMX_NATIVE_RASTER
    .publish_native_field = MmxPublishNativeField,
#endif
    .before_run_frame = MmxBeforeFrame, .on_reset = MmxResetRenderer,
    .on_rom_loaded = MmxRomLoaded, .compute_viewport = MmxViewport,
    .window_base_width = MmxWindowWidth,
    .window_base_height = MmxDisplay_GetWindowBaseHeight,
    .after_config = MmxAfterConfig, .after_run_frame = MmxAfterFrame,
    .configure_launcher = MmxConfigureLauncher,
    .prepare_netplay = MmxNetplayPrepare,
    .netplay_ready = MmxNetplayReady,
    .on_shutdown = MmxShutdown,
  };
  int result = snesrecomp_desktop_main(&game, argc, argv);
  MmxReplayClose();
  return MmxReplayPlaying() && (MmxReplayFailed() || !MmxReplayFinished()) ? 2 : result;
}
