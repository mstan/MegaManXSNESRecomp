#include "mod_runtime.h"
#include "mmx_display.h"
#include "mmx_renderer.h"
#include <string.h>
#if defined(RECOMP_LAUNCHER)
#include "recomp_launcher.h"
#endif

/*
 * Keep renderer selection, aspect and HUD anchoring in the mod catalog.
 * Gameplay widening stays in the game hooks for both renderer choices.
 */
static void mmx_widescreen_reset(void) {
  g_mmx_custom_renderer = false;
  g_mmx_expanded_sprites = false;
  MmxRendererReset();
  MmxDisplay_SetWidescreenEnabled(false);
}

static void mmx_widescreen_activate(void) {
  g_mmx_custom_renderer = true;
  g_mmx_custom_aspect = MMX_ASPECT_ADAPTIVE;
  g_mmx_custom_hud = true;
  g_mmx_expanded_sprites = false;
#if defined(RECOMP_LAUNCHER)
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModOption option;
  for (int i = 0; provider && provider->feature_option_get &&
       provider->feature_option_get(provider->ctx, "megaman-x.enhancement.widescreen", "widescreen", i, &option); ++i) {
    if (!strcmp(option.id, "renderer")) g_mmx_custom_renderer = strcmp(option.value, "legacy") != 0;
    if (!strcmp(option.id, "hud")) g_mmx_custom_hud = strcmp(option.value, "center") != 0;
    if (!strcmp(option.id, "expanded_sprites")) g_mmx_expanded_sprites = !strcmp(option.value, "on");
    if (!strcmp(option.id, "aspect")) {
      if (!strcmp(option.value, "16:9")) g_mmx_custom_aspect = MMX_ASPECT_16_9;
      if (!strcmp(option.value, "21:9")) g_mmx_custom_aspect = MMX_ASPECT_21_9;
      if (!strcmp(option.value, "32:9")) g_mmx_custom_aspect = MMX_ASPECT_32_9;
    }
  }
#endif
  MmxDisplay_SetWidescreenEnabled(true);
}

SNES_MOD_CONSTRUCTOR(mmx_register_widescreen_plugin) {
  (void)snes_mod_register_reset_callback(mmx_widescreen_reset);
  (void)snes_mod_register_activation_plugin(
      "megaman-x.widescreen", mmx_widescreen_activate);
}
