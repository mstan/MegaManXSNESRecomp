#include "mod_runtime.h"
#include "host_paths.h"
#include "snes/interp_bridge.h"
#include "mmx_armor.h"
#include "mmx_source_assets.h"
#include "sdl_compat.h"
#include <stdio.h>
#include <string.h>
extern uint8_t g_ram[0x20000];
extern void MmxZeroRegisterHooks(void);
static void award_hook(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;MmxArmorHadoukenAward(g_ram);
}
static void damage_hook(CpuState *cpu,uint32_t pc) {
  (void)pc;MmxArmorTakeDamage(g_ram,cpu->D);
}
static void activate(void) {
  char selected[16]={0},source[4096]={0},output[4096],error[512],leaf[80];
  snes_mod_runtime_feature_option_value_c("megaman-x.armor","armor","game",selected,sizeof(selected));
  unsigned game=!strcmp(selected,"x3")?3:2;
  const char *resource=game==2?"x2-rom":"x3-rom";
  if(!snes_mod_runtime_resource_path_c("megaman-x.armor","armor",resource,source,sizeof(source)) || !source[0]) {
    snprintf(error,sizeof(error),"Select your Mega Man X%u USA ROM for the selected armor.",game);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare X armor",error,NULL);return;
  }
  snprintf(leaf,sizeof(leaf),"cache/mmx-source/x%u-armor-v1.bin",game);
  if(!snesrecomp_exe_dir_path(leaf,output,sizeof(output)))return;
  if(!MmxArmorAssetsBuild(source,game,output,error,sizeof(error)) || !MmxArmorLoad(output,game)) {
    if(!error[0])snprintf(error,sizeof(error),"Cannot load the extracted armor data.");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare X armor",error,NULL);return;
  }
  MmxZeroRegisterHooks();
  /* $87:D0DB is the real Hadouken acquisition TSB, after the capsule's
   * demonstration reaches its reward. Never infer this from all-parts flags. */
  interp_bridge_set_pre_opcode_hook(0x07d0db,award_hook);
  interp_bridge_set_pre_opcode_hook(0x87d0db,award_hook);
  interp_bridge_set_pre_opcode_hook(0x049d50,damage_hook);
  interp_bridge_set_pre_opcode_hook(0x849d50,damage_hook);
  fprintf(stderr,"[mmx-armor] X%u armor prepared from the selected source ROM\n",game);
}
SNES_MOD_CONSTRUCTOR(mmx_register_armor_plugin) {
  (void)snes_mod_register_activation_plugin("megaman-x.armor",activate);
}
