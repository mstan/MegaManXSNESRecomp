#include "mmx_complete.h"
#include "mod_runtime.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <stdio.h>

extern Snes *g_snes;
static void reset(void) {
  /* Reset is invoked against the current machine, before activation. */
  MmxCompleteBindRom(g_snes && g_snes->cart ? g_snes->cart->rom : NULL,
      g_snes && g_snes->cart ? g_snes->cart->romSize : 0);
}
#define ACTIVATE(name, feature) static void name(void) { \
  if (!MmxCompleteSetEnabled(feature, true)) \
    fprintf(stderr, "[complete-edition] Could not activate " #name "\n"); \
}
ACTIVATE(script, MMX_COMPLETE_SCRIPT)
ACTIVATE(colours, MMX_COMPLETE_COLOURS)
ACTIVATE(typos, MMX_COMPLETE_TYPOS)
ACTIVATE(hoganmer, MMX_COMPLETE_HOGANMER)
ACTIVATE(damage, MMX_COMPLETE_DAMAGE)
static void debug(void) {
  (void)MmxCompleteSetEnabled(MMX_COMPLETE_DEBUG, true);
}
SNES_MOD_CONSTRUCTOR(mmx_register_complete_plugins) {
  snes_mod_register_reset_callback(reset);
  snes_mod_register_activation_plugin("megaman-x.complete.script", script);
  snes_mod_register_activation_plugin("megaman-x.complete.colours", colours);
  snes_mod_register_activation_plugin("megaman-x.complete.typos", typos);
  snes_mod_register_activation_plugin("megaman-x.complete.hoganmer", hoganmer);
  snes_mod_register_activation_plugin("megaman-x.complete.damage", damage);
  snes_mod_register_activation_plugin("megaman-x.complete.debug", debug);
}
