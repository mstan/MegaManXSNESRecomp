#include "mmx_boss_rush_foreign.h"
#include "mmx_mod_constructor.h"
#include "recomp_launcher.h"
#include "sdl_compat.h"
#include <stdio.h>

/* X2/X3 Mavericks join Boss Rush only with their own source ROM. The X3 pack
 * shares the canonical X3 selection with Zero, co-op and the X3 weapons. */
static void activate(unsigned game) {
  const char *package=game==2?"megaman-x.boss-rush.x2-bosses":"megaman-x.boss-rush.x3-bosses";
  const RecompLauncherCModProvider *provider=snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource={0};
  char error[512];
  if(!provider || !provider->feature_resource_get ||
      !provider->feature_resource_get(provider->ctx,package,"bosses",0,&resource) || !resource.path[0]) return;
  if(!MmxBossRushForeignLoad(game,resource.path,error,sizeof(error))) {
    fprintf(stderr,"[mmx-rush] %s\n",error);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare Boss Rush Mavericks",error,NULL);
    return;
  }
  fprintf(stderr,"[mmx-rush] X%u Mavericks loaded from the selected source ROM\n",game);
}
static void activate_x2(void) {activate(2);}
static void activate_x3(void) {activate(3);}
static void reset(void) {MmxBossRushForeignUnload();}

SNES_MOD_CONSTRUCTOR(mmx_register_boss_rush_foreign_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.boss-rush.x2-bosses",activate_x2);
  (void)snes_mod_register_activation_plugin("megaman-x.boss-rush.x3-bosses",activate_x3);
}
