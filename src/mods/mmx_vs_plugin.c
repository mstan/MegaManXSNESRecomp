#include "mmx_vs.h"
#include "mod_runtime.h"
static void reset(void) {MmxVsSetEnabled(false);}
static void activate(void) {MmxVsSetEnabled(true);}
SNES_MOD_CONSTRUCTOR(mmx_register_vs_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.vs",activate);
}
