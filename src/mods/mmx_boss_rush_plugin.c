#include "mmx_boss_rush.h"
#include "mod_runtime.h"

static void reset(void) { MmxBossRushSetEnabled(false); }
static void activate(void) {
#ifdef __ANDROID__
  extern int MmxAndroidBossRush(void);
  MmxBossRushSetEnabled(MmxAndroidBossRush()!=0);
#else
  MmxBossRushSetEnabled(true);
#endif
}

SNES_MOD_CONSTRUCTOR(mmx_register_boss_rush_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.boss-rush", activate);
}
