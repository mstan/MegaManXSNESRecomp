#include "mod_runtime.h"
#include "host_paths.h"
#include "snes/interp_bridge.h"
#include "mmx_armor.h"
#include "mmx_source_assets.h"
#include "sdl_compat.h"
#include "debug_server.h"
#include "common_rtl.h"
#include "mmx_coop.h"
#include "mmx_renderer.h"
#include "snes_rewind.h"
#include "desktop/host_api.h"
#include <stdio.h>
#include <string.h>
extern uint8_t g_ram[0x20000];
extern void MmxZeroRegisterHooks(void);
#if SNESRECOMP_TRACE
static SDL_mutex *debug_mutex;
static unsigned debug_request;
static char debug_path[4096];
static void debug_queue(unsigned request,const char *path,DebugServerGameSendLine send) {
  SDL_LockMutex(debug_mutex);
  if(debug_request) {SDL_UnlockMutex(debug_mutex);send("{\"error\":\"a command is already pending\"}");return;}
  if(path)snprintf(debug_path,sizeof(debug_path),"%s",path);
  debug_request=request;SDL_UnlockMutex(debug_mutex);send("{\"queued\":true}");
}
#endif
void MmxArmorDebugHostFrame(void) {
#if SNESRECOMP_TRACE
  if(!debug_mutex || !snesrecomp_host_frame_is_live())return;
  char path[4096];SDL_LockMutex(debug_mutex);unsigned request=debug_request;
  if(request==4)memcpy(path,debug_path,sizeof(path));debug_request=0;SDL_UnlockMutex(debug_mutex);
  if(!MmxArmorEnabled())return;
  if(request==1) {MmxArmorState s=MmxArmorGetState();s.energy=0x1c00;MmxArmorSetState(s);}
  else if(request==2)MmxArmorRefill();
  else if(request==3 && snes_rewind_open()) {
    for(unsigned i=0;i<6 && snes_rewind_selected_seconds()<0.0999f;++i)snes_rewind_step(-1);
    snes_rewind_commit();
  } else if(request==4)MmxRendererSaveCapture(path);
  else if(request==5)snesrecomp_request_exit();
#endif
}
static int armor_debug(const char *cmd,const char *args,DebugServerGameSendLine send) {
  if(strcmp(cmd,"armor"))return 0;
  MmxArmorState s=MmxArmorGetState();char out[2048];unsigned active=0;
  if(!strcmp(args,"players")) {
    MmxCoopState coop=MmxCoopGetState();
    uint8_t *x=MmxCoopEnabled()?MmxCoopCharacterBody(g_ram,MMX_COOP_X):g_ram+0xba8;
    uint8_t *z=MmxCoopEnabled()?MmxCoopCharacterBody(g_ram,MMX_COOP_ZERO):NULL;
    snprintf(out,sizeof(out),"{\"coop\":%u,\"current\":%u,\"anchor\":%u,\"p1_character\":%u,\"p2_character\":%u,\"p1_pad\":%u,\"p2_pad\":%u,\"x\":[%u,%u,%u],\"zero\":[%u,%u,%u]}",
      MmxCoopEnabled(),coop.current,coop.anchor,coop.players[0].character,coop.players[1].character,RtlGetPadState(0),RtlGetPadState(1),
      x?(unsigned)(x[5]|x[6]<<8):0,x?(unsigned)(x[8]|x[9]<<8):0,x?x[0x27]&127:0,
      z?(unsigned)(z[5]|z[6]<<8):0,z?(unsigned)(z[8]|z[9]<<8):0,z?z[0x27]&127:0);
    send(out);return 1;
  }
#if SNESRECOMP_TRACE
  if(!strcmp(args,"close")) {debug_queue(5,NULL,send);return 1;}
  if(!strncmp(args,"capture ",8)) {
    debug_queue(4,args+8,send);return 1;
  }
  if(!strcmp(args,"rewind-6")) {
    debug_queue(3,NULL,send);return 1;
  }
  /* Development setup bypasses the energy prerequisite only. Actual owned
   * armor, native contact handling and progression checks still apply. */
  if(!strcmp(args,"fill-giga")) {debug_queue(1,NULL,send);return 1;}
  if(!strcmp(args,"fill-hyper")) {debug_queue(2,NULL,send);return 1;}
#else
  (void)args;
#endif
  for(unsigned i=0;i<8;++i)active+=s.shots[i].active;
  uint8_t *body=MmxCoopEnabled()?MmxCoopCharacterBody(g_ram,MMX_COOP_X):g_ram+0xba8;
  snprintf(out,sizeof(out),"{\"game\":%u,\"gold\":%u,\"charge\":%u,\"burst\":%u,\"stored\":%u,\"shots\":%u,\"tracer\":%u,\"radar_stage\":%u,\"pad\":%u,\"valid\":%u,\"giga\":%u,\"energy\":%u,\"hyper\":%u,\"hyper_energy\":%u,\"x\":%u,\"y\":%u,\"barrier\":%u,\"barrier_level\":%u,\"dash_used\":%u,\"dash_ticks\":%u,\"dash_up\":%u,\"recovery\":%u}",
    MmxArmorGame(),s.gold,s.charge,s.burst,s.stored_shot,active,s.tracer,s.radar_stage,RtlGetPadState(0),MmxArmorValidState(&s),
    s.giga_tick,s.energy,s.hyper,s.hyper_energy,body?(unsigned)(body[5]|body[6]<<8):0,body?(unsigned)(body[8]|body[9]<<8):0,
    s.barrier_ticks,s.barrier_level,s.dash_used,s.dash_ticks,s.dash_up,s.recovery_ticks);
  send(out);return 1;
}
static void award_hook(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;MmxArmorHadoukenAward(g_ram);
}
static void damage_hook(CpuState *cpu,uint32_t pc) {
  (void)pc;MmxArmorTakeDamage(g_ram,cpu->D);
}
static void preparation_failed(const char *error) {
  fprintf(stderr,"[mmx-armor] preparation failed: %s\n",error);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare X armor",error,NULL);
  /* A selected simulation feature must not silently become vanilla gameplay.
   * Activation runs after SDL initialization, before the first frame. */
  snesrecomp_request_exit();
}
static void activate(void) {
  char selected[16]={0},source[4096]={0},output[4096],error[512],leaf[80];
  snes_mod_runtime_feature_option_value_c("megaman-x.armor","armor","game",selected,sizeof(selected));
  unsigned game=!strcmp(selected,"x3")?3:2;
  const char *resource=game==2?"x2-rom":"x3-rom";
  if(!snes_mod_runtime_resource_path_c("megaman-x.armor","armor",resource,source,sizeof(source)) || !source[0]) {
    snprintf(error,sizeof(error),"Select your Mega Man X%u USA ROM for the selected armor.",game);
    preparation_failed(error);return;
  }
  snprintf(leaf,sizeof(leaf),"cache/mmx-source/x%u-armor-v7.bin",game);
  if(!snesrecomp_exe_dir_path(leaf,output,sizeof(output))) {
    preparation_failed("Cannot resolve the local armor cache directory.");return;
  }
  if(!MmxArmorAssetsBuild(source,game,output,error,sizeof(error)) || !MmxArmorLoad(output,game)) {
    if(!error[0])snprintf(error,sizeof(error),"Cannot load the extracted armor data.");
    preparation_failed(error);return;
  }
  MmxZeroRegisterHooks();
#if SNESRECOMP_TRACE
  if(!debug_mutex)debug_mutex=SDL_CreateMutex();
  if(!debug_mutex) {preparation_failed("Cannot initialize armor diagnostics.");return;}
#endif
  debug_server_set_game_command_handler(armor_debug);
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
