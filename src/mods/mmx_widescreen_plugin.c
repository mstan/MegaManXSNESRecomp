#include "mod_runtime.h"
#include "mmx_display.h"

/*
 * The surveyed renderer/HUD/BG/object-window implementation remains in the
 * game and engine. This plugin moves only its player-facing activation into
 * the package catalog.
 */
static void mmx_widescreen_reset(void) {
  MmxDisplay_SetWidescreenEnabled(false);
}

static void mmx_widescreen_activate(void) {
  MmxDisplay_SetWidescreenEnabled(true);
}

SNES_MOD_CONSTRUCTOR(mmx_register_widescreen_plugin) {
  (void)snes_mod_register_reset_callback(mmx_widescreen_reset);
  (void)snes_mod_register_activation_plugin(
      "megaman-x.widescreen", mmx_widescreen_activate);
}
