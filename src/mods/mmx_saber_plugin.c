#include "mod_runtime.h"
#include "mod_audio.h"
#include "common_rtl.h"
#include "host_paths.h"
#include "recomp_launcher.h"
#include "mmx_source_assets.h"
#include "saber/mmx_saber_assets.h"
#include "saber/mmx_saber_sfx.h"
#include "saber/mmx_saber_wave.h"
#include "saber/mmx_saber_wave_assets.h"
#include "saber/mmx_saber_combo.h"
#include "saber/mmx_saber_frame.h"
#include "saber/mmx_saber_hitbox_debug.h"
#include "saber/mmx_saber_render.h"
#include "saber/mmx_saber_tuning.h"
#include "saber/mmx_saber_wave_runtime.h"
#include "mmx_coop.h"
#include "mmx_renderer.h"
#include "mmx_zero.h"
#include "saber/mmx_saber_plugin.h"
#include "sdl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_mmx_saber_enabled;
static MmxSaberAssets *g_saber_assets;
static MmxSaberAssets *g_ride_assets;
static MmxSaberWave *g_saber_wave;
static bool g_saber_sfx_warning;

/* Saber owns Zero's body, whichever seat the renderer is drawing. An X body
 * (single-player exchange, or X's co-op seat) is never given the overlay. */
static bool saber_overlay_provider(const uint8_t *ram, const MmxZeroState *zero,
                                   MmxRenderPlayerOverlay *out) {
  if (!g_mmx_saber_enabled || !g_saber_assets || !ram || !zero ||
      zero->active_x) {
    if (out) memset(out, 0, sizeof(*out));
    return false;
  }
  if (MmxSaberRenderResolveRide(g_ride_assets, ram, out))
    return true;
  return MmxSaberRenderResolve(g_saber_assets, zero, out);
}

static void coop_seat_pad(const uint8_t *ram, MmxSaberFramePad *pad) {
  (void)ram;
  MmxCoopSeatButtons(&pad->x_held, &pad->y_pressed);
}

static void saber_activation_failed(void) {
  MmxSaberFrameSetPadSource(NULL);
  MmxRendererSetPlayerOverlayProvider(NULL);
  MmxRendererSetWorldSpriteProvider(NULL);
  MmxRendererSetDebugRectProvider(NULL);
  MmxSaberRenderSetWave(NULL);
  MmxZeroSetExtension(NULL);
  MmxSaberFrameReset();
}

static void saber_sfx_warning(const char *reason, void *context) {
  (void)context;
  if (g_saber_sfx_warning) return;
  g_saber_sfx_warning = true;
  fprintf(stderr, "[mmx-saber-zero] Saber swing audio unavailable: %s\n",
          reason && reason[0] ? reason : "invalid sidecar");
}

static int saber_sfx_register_pcm(const int16_t *samples, uint32_t frames,
                                  uint32_t sample_rate, uint32_t channels,
                                  void *context) {
  (void)context;
  return snes_mod_audio_register_pcm_s16(samples, frames, sample_rate,
                                         channels);
}

static void saber_sfx_unregister(int clip, void *context) {
  (void)context;
  snes_mod_audio_unregister((SNESModAudioClip)clip);
}

static int saber_sfx_play(int clip, int volume_percent, void *context) {
  (void)context;
  return snes_mod_audio_play((SNESModAudioClip)clip, volume_percent);
}

static bool saber_sfx_suppressed(void *context) {
  (void)context;
  return RtlSpeculativeFrame();
}

static const MmxSaberSfxHost kSaberSfxHost = {
  saber_sfx_register_pcm,
  saber_sfx_unregister,
  saber_sfx_play,
  saber_sfx_suppressed,
  NULL
};

/* Tuning comes from the optional "Saber Zero settings" feature. A disabled
 * feature is left out of the netplay mod hash, so its stored values must not
 * reach the simulation either: without it, every option keeps its default. */
static bool saber_tuning_option_reader(const char *option_id, char *value,
                                       size_t value_size, void *context) {
  (void)context;
  return snes_mod_runtime_feature_enabled_c(MMX_SABER_SETTINGS_PACKAGE,
                                            MMX_SABER_SETTINGS_FEATURE) &&
      snes_mod_runtime_feature_option_value_c(
             MMX_SABER_SETTINGS_PACKAGE, MMX_SABER_SETTINGS_FEATURE, option_id,
             value, (uint32_t)value_size) != 0;
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

bool MmxSaberEnabled(void) {
  return g_mmx_saber_enabled;
}

bool MmxSaberAssetsLoaded(void) {
  return g_saber_assets != NULL;
}

bool MmxSaberRideAssetsLoaded(void) {
  return g_ride_assets != NULL;
}

bool MmxSaberWaveLoaded(void) {
  return g_saber_wave != NULL;
}

static int cache_path(const char *name, char path[4096]) {
  const char *test_cache = getenv("MMX_SABER_TEST_CACHE");
  char leaf[128];
  int written;
  if (!name || !path) return 0;
  if (test_cache && test_cache[0]) {
    written = snprintf(path, 4096, "%s/mmx-source/%s", test_cache, name);
    return written >= 0 && written < 4096;
  }
  written = snprintf(leaf, sizeof(leaf), "cache/mmx-source/%s", name);
  return written >= 0 && (size_t)written < sizeof(leaf) &&
      snesrecomp_exe_dir_path(leaf, path, 4096);
}

/* The X3 ROM is the character feature's own resource (Add Zero or Co-op). */
static int resolve_saber_rom(const char *package, const char *feature,
                             char path[4096]) {
  const RecompLauncherCModProvider *provider =
      snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource = {0};
  int written;
  if (!provider || !provider->feature_resource_get ||
      !provider->feature_resource_get(provider->ctx, package, feature, 0,
                                      &resource) ||
      !resource.path[0])
    return 0;
  written = snprintf(path, 4096, "%s", resource.path);
  return written >= 0 && written < 4096;
}

static void report_wave_prepare_failure(const char *wave_path,
                                        const char *reason) {
  char message[2048];
  snprintf(message, sizeof(message),
      "Saber Zero wave cache is missing or invalid:\n"
      "  wave: %s\n"
      "  reason: %s\n",
      wave_path && wave_path[0] ? wave_path : "<unresolved>",
      reason && reason[0] ? reason : "wave extraction failed");
  fprintf(stderr, "[mmx-saber-zero] %s\n", message);
  if (!getenv("MMX_SABER_TEST_CACHE_ONLY"))
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot enable Saber Zero",
                             message, NULL);
}

static int prepare_wave(const char rom[4096], char wave_path[4096]) {
  char error[512];
  if (!rom || !rom[0] || !cache_path("x3-saber-wave-v1.bin", wave_path)) {
    report_wave_prepare_failure(wave_path, "cannot resolve wave cache path");
    return 0;
  }
  if (MmxSaberWaveAssetsBuild(rom, wave_path, error, sizeof(error))) return 1;
  report_wave_prepare_failure(wave_path, error);
  return 0;
}

static void release_saber_assets(void) {
  MmxRendererSetPlayerOverlayProvider(NULL);
  MmxRendererSetWorldSpriteProvider(NULL);
  MmxRendererSetDebugRectProvider(NULL);
  MmxSaberRenderSetWave(NULL);
  MmxSaberWaveRuntimeSetCollisionRecord(NULL, 0);
  MmxSaberWaveFree(g_saber_wave);
  MmxSaberAssetsFree(g_saber_assets);
  MmxSaberAssetsFree(g_ride_assets);
  g_saber_wave = NULL;
  g_saber_assets = NULL;
  g_ride_assets = NULL;
}

static void report_saber_asset_failure(const char *saber_path,
                                       const char *ride_path,
                                       const char *wave_path,
                                       const char *reason) {
  char message[2048];
  snprintf(message, sizeof(message),
      "Saber Zero assets are missing or invalid:\n"
      "  saber: %s\n"
      "  ride: %s\n"
      "  wave: %s\n"
      "  reason: %s\n\n"
      "Regenerate them with:\n"
      "python -I tools/saber/convert_saber_zero.py --manifest "
      "tools/saber/saber_zero_manifest.json --source-dir "
      "assets/saber-zero/sprites "
      "--out <exe-dir>/cache/mmx-source/saber-v1.bin\n"
      "python -I tools/saber/convert_saber_zero.py --manifest "
      "tools/saber/ride_zero_manifest.json --source-dir "
      "assets/saber-zero/sprites "
      "--out <exe-dir>/cache/mmx-source/ride-zero-v1.bin",
      saber_path && saber_path[0] ? saber_path : "<unresolved>",
      ride_path && ride_path[0] ? ride_path : "<unresolved>",
      wave_path && wave_path[0] ? wave_path : "<unresolved>",
      reason && reason[0] ? reason : "invalid sidecar");
  fprintf(stderr, "[mmx-saber-zero] %s\n", message);
  /* The real plugin uses the upstream message-box style. The isolated ROM
   * runner opts out so a deliberate missing-cache test cannot block MinGW. */
  if (!getenv("MMX_SABER_TEST_CACHE_ONLY"))
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot enable Saber Zero",
                             message, NULL);
}

static int load_saber_assets(const char *saber_path, const char *ride_path,
                             char reason[256]) {
  MmxSaberAssets *saber;
  MmxSaberAssets *ride;
  char local_reason[128];

  release_saber_assets();
  saber = MmxSaberAssetsLoadFile(saber_path, kSaberManifestSha,
                                 local_reason, sizeof(local_reason));
  if (!saber) {
    snprintf(reason, 256, "saber-v1.bin: %s",
             local_reason[0] ? local_reason : "parse failed");
    return 0;
  }
  ride = MmxSaberAssetsLoadFile(ride_path, kRideManifestSha,
                                local_reason, sizeof(local_reason));
  if (!ride) {
    MmxSaberAssetsFree(saber);
    snprintf(reason, 256, "ride-zero-v1.bin: %s",
             local_reason[0] ? local_reason : "parse failed");
    return 0;
  }
  g_saber_assets = saber;
  g_ride_assets = ride;
  reason[0] = '\0';
  return 1;
}

static int load_saber_wave(const char *wave_path, char reason[256]) {
  MmxSaberWave *wave;
  char local_reason[128];

  MmxRendererSetWorldSpriteProvider(NULL);
  MmxSaberRenderSetWave(NULL);
  MmxSaberWaveFree(g_saber_wave);
  g_saber_wave = NULL;
  wave = MmxSaberWaveLoadFile(wave_path, local_reason, sizeof(local_reason));
  if (!wave) {
    snprintf(reason, 256, "x3-saber-wave-v1.bin: %s",
             local_reason[0] ? local_reason : "parse failed");
    return 0;
  }
  g_saber_wave = wave;
  MmxSaberWaveRuntimeSetCollisionRecord(
      MmxSaberWaveCollisionRecord(g_saber_wave),
      MmxSaberWaveCollisionSize(g_saber_wave));
  reason[0] = '\0';
  return 1;
}

bool MmxSaberActivate(const char *package, const char *feature, bool coop) {
  char rom[4096] = {0}, wave_path[4096] = {0};
  char saber_path[4096] = {0}, ride_path[4096] = {0};
  char sfx_path[4096] = {0};
  char reason[256] = {0};
  saber_activation_failed();
  g_mmx_saber_enabled = false;
  g_saber_sfx_warning = false;
  MmxSaberSfxResetRuntime();
  MmxSaberSfxSetHost(&kSaberSfxHost);
  MmxSaberSfxSetWarningCallback(saber_sfx_warning, NULL);
  MmxSaberTuningLoad(saber_tuning_option_reader, NULL);
  MmxSaberSfxSetVolume(MmxSaberTuningSaberSwingVolume());
  MmxSaberComboSetWindowFrames((unsigned)MmxSaberTuningFinisherWindowFrames());
  release_saber_assets();
  /* The character feature has already prepared and loaded X3 Zero and
   * registered its hooks; without Saber it remains ordinary X3 Zero. */
  if (!MmxZeroEnabled() || !resolve_saber_rom(package, feature, rom)) {
    saber_activation_failed();
    return false;
  }
  if (!prepare_wave(rom, wave_path)) {
    saber_activation_failed();
    return false;
  }
  if (!cache_path("saber-v1.bin", saber_path) ||
      !cache_path("ride-zero-v1.bin", ride_path) ||
      !load_saber_assets(saber_path, ride_path, reason)) {
    report_saber_asset_failure(saber_path, ride_path, wave_path, reason);
    saber_activation_failed();
    return false;
  }
  if (!load_saber_wave(wave_path, reason)) {
    release_saber_assets();
    report_wave_prepare_failure(wave_path, reason);
    saber_activation_failed();
    return false;
  }
  /* Activation runs before the desktop host creates its audio mutex.  Parse
   * the validated sidecar now; MmxSaberSfxPlay registers the copied PCM only
   * when the first real game-thread cue arrives.  Missing sound data does not
   * invalidate the character package. */
  if (!cache_path("saber-sfx-v2.bin", sfx_path)) {
    saber_sfx_warning("cannot resolve sidecar path", NULL);
  } else if (!MmxSaberSfxLoadRuntime(sfx_path, reason, sizeof(reason))) {
    /* The loader's warning callback already emits one concise diagnostic. */
  }
  MmxSaberFrameReset();
  MmxSaberFrameSetPadSource(coop ? coop_seat_pad : NULL);
  MmxZeroSetExtension(MmxSaberFrameExtension());
  g_mmx_saber_enabled = true;
  MmxRendererSetPlayerOverlayProvider(saber_overlay_provider);
  MmxSaberRenderSetWave(g_saber_wave);
  MmxRendererSetWorldSpriteProvider(MmxSaberRenderProvideWorldSprites);
  if (MmxSaberTuningShowHitboxes())
    MmxRendererSetDebugRectProvider(MmxSaberHitboxDebugProvide);
  fprintf(stderr, "[mmx-saber-zero] saber-v1.bin, ride-zero-v1.bin, and "
                  "x3-saber-wave-v1.bin loaded\n");
  fprintf(stderr, "[mmx-saber-zero] Saber Zero enabled for %s\n",
          coop ? "co-op" : "Add Zero");
  return true;
}

static void reset(void) {
  g_mmx_saber_enabled = false;
  MmxSaberFrameSetPadSource(NULL);
  MmxRendererSetPlayerOverlayProvider(NULL);
  MmxRendererSetWorldSpriteProvider(NULL);
  MmxRendererSetDebugRectProvider(NULL);
  MmxSaberRenderSetWave(NULL);
  MmxZeroSetExtension(NULL);
  MmxSaberFrameReset();
  MmxSaberSfxResetRuntime();
  g_saber_sfx_warning = false;
  release_saber_assets();
}

SNES_MOD_CONSTRUCTOR(mmx_register_saber_zero_plugin) {
  (void)snes_mod_register_reset_callback(reset);
}
