#include "mmx_qol.h"
#include "mmx_armor.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "mmx_weapon_combat.h"
#include "mmx_dash.h"
#include "mmx_source_assets.h"
#include "host_paths.h"
#include "mod_runtime.h"
#include "snes/interp_bridge.h"
#include "desktop/host_api.h"
#include <stdio.h>

extern uint8_t g_ram[0x20000];
extern bool MmxZeroRegisterQolDamageHook(void);
extern bool MmxZeroRegisterQolMovementHooks(void);
extern bool MmxZeroRegisterQolUpgradeHook(void);
static bool native_hadouken(const CpuState *cpu) {
  return cpu->X>=0x1228 && cpu->X<0x1428 && (cpu->X&63)==0x28 &&
      g_ram[cpu->X] && g_ram[cpu->X+10]==4 &&
      !MmxArmorProjectileOwned(g_ram,cpu->X) && !MmxWeaponsProjectileOwned(g_ram,cpu->X);
}
unsigned MmxQolNativeDamage(CpuState *cpu,unsigned original) {
  if(!MmxQolDamageEnabled())return original;
  return MmxQolDamage(g_ram,cpu->D,(0xef37+cpu->Y)&65535,original,native_hadouken(cpu));
}
unsigned MmxQolNativeUpgrade(CpuState *cpu,unsigned pc,unsigned original) {
  if(!MmxDashEnabled() && !MmxQolFeatureEnabled(MMX_QOL_NO_DOUBLE_TAP))return original;
  if(cpu->D!=0xba8)return original;
  bool x=!MmxZeroActive();
  if((pc&0x7fffff)==0x01971c || (pc&0x7fffff)==0x019793 || (pc&0x7fffff)==0x0198fc)
    original=MmxDashUpgradeBits(original,x);
  return MmxQolUpgradeRead(g_ram,pc+3,original,x);
}
unsigned MmxQolNativeShotRead(CpuState *cpu,unsigned original,bool held_read) {
  if(!MmxQolFeatureEnabled(MMX_QOL_AUTO_CHARGE|MMX_QOL_RAPID_FIRE))return original;
  if(cpu->D!=0xba8 || MmxZeroActive() || MmxWeaponsGetState().weapon ||
      MmxArmorOwnsFireInput())return original;
  bool ready=!g_ram[0xbdb] && (MmxArmorEnabled()?MmxArmorBusterReady():g_ram[0xc00]==4);
  return MmxQolShotValue(g_ram,original,held_read,ready);
}
void MmxQolNativePlayer(CpuState *cpu,unsigned phase) {
  if(!MmxDashEnabled())return;
  bool x=!MmxZeroActive(),armor=MmxArmorEnabled();
  if(!phase)MmxDashPrePlayer(g_ram,x,armor);
  else if(phase==1)MmxDashPlayerEnd(g_ram,x,armor);
  else MmxDashPlayerMotion(g_ram,cpu->D,x,armor);
}
uint32_t MmxQolApplyOpcode(CpuState *cpu,uint32_t pc) {
  if(!MmxQolEnabled())return 0;
  bool x=!MmxZeroActive();
  bool native_weapon=!MmxWeaponsGetState().weapon;
  bool ready=!g_ram[0xbdb] && (MmxArmorEnabled()?MmxArmorBusterReady():g_ram[0xc00]==4);
  /* Ordinary donor charging shares the native audio/mote producer. Stored
   * shots and burst sequences already own/mask fire input themselves. */
  bool native=native_weapon && !MmxArmorOwnsFireInput();
  uint32_t redirect=MmxQolOpcode(cpu,g_ram,pc,x,native,ready);
  MmxQolHadoukenOpcode(cpu,g_ram,pc,native_hadouken(cpu));
  return redirect;
}
static void hook(CpuState *cpu,uint32_t pc) {
  uint32_t redirect=MmxQolApplyOpcode(cpu,pc);
  if(redirect)interp_bridge_pre_opcode_redirect(redirect);
}
static void armor_intent(const uint8_t *r,bool ready,bool *held,bool *pressed) {
  if(!MmxZeroActive())MmxQolFireIntent(r,ready,held,pressed);
}
bool MmxQolRegisterHooks(void) {
  bool ok=true;
  MmxArmorSetFireQuery(armor_intent);
  static const uint32_t sites[]={0x81984c,0x81985b,0x81988c,
    0x818332,0x8183c0,0x818430,0x8184b8,0x818562,0x81862f,0x818759,
    0x8187a0,0x81887a,0x818956,0x8189d1,0x818e14,
    0x80c723,0x80c93f,0x83f5d1,0x83f5dc,0x83f605,0x83f60e,0x83f5e9,0x8080b5};
  for(unsigned i=0;i<sizeof(sites)/sizeof(*sites);++i)
    ok=interp_bridge_add_pre_opcode_hook(sites[i],hook) && ok;
  static const uint32_t extra[]={0x818777,0x87ca98,0x849e48,0x849e9d};
  for(unsigned i=0;i<sizeof(extra)/sizeof(*extra);++i)
    ok=interp_bridge_add_pre_opcode_hook(extra[i],hook) && ok;
  if(MmxQolDamageEnabled())ok=MmxZeroRegisterQolDamageHook() && ok;
  ok=MmxZeroRegisterQolUpgradeHook() && ok;
  if(MmxDashEnabled())ok=MmxZeroRegisterQolMovementHooks() && ok;
  return ok;
}
static void enable(unsigned feature) {
  MmxQolEnable(feature);
  if(!MmxQolRegisterHooks()) {
    fprintf(stderr,"[mmx-qol] Cannot register the selected native controls\n");
    snesrecomp_request_exit();
  }
}
#define OPTION(name,flag) static void name(void) {enable(flag);}
OPTION(double_tap,MMX_QOL_NO_DOUBLE_TAP)
OPTION(sub_tank,MMX_QOL_SUB_TANK)
OPTION(auto_charge,MMX_QOL_AUTO_CHARGE)
OPTION(rapid_fire,MMX_QOL_RAPID_FIRE)
OPTION(dialogue,MMX_QOL_DIALOGUE)
OPTION(skip_intro,MMX_QOL_SKIP_INTRO)
OPTION(escape,MMX_QOL_ESCAPE)
OPTION(wall_jump,MMX_QOL_WALL_JUMP)
OPTION(first_hadouken,MMX_QOL_FIRST_HADOUKEN)
OPTION(hadouken_iframes,MMX_QOL_HADOUKEN_IFRAMES)
OPTION(hadouken_sigma,MMX_QOL_HADOUKEN_SIGMA)
static void dash_enable(unsigned flag,const char *feature) {
  char source[4096]={0},cache[4096],error[512]={0};
  bool ok=snes_mod_runtime_resource_path_c("megaman-x.accessibility.qol",feature,
      "x2-rom",source,sizeof(source)) && source[0] &&
      snesrecomp_exe_dir_path("cache/mmx-source/x2-dash-v1.bin",cache,sizeof(cache));
  if(ok)ok=MmxDashAssetsBuild(source,cache,error,sizeof(error)) && MmxDashArtLoad(cache);
  if(!ok) {
    fprintf(stderr,"[mmx-qol] Cannot prepare dash data: %s\n",error[0]?error:"Select your original Mega Man X2 USA ROM in Mods.");
    snesrecomp_request_exit();return;
  }
  MmxDashEnable(flag);
  if(!MmxQolRegisterHooks()) {
    fprintf(stderr,"[mmx-qol] Cannot register native dash controls\n");snesrecomp_request_exit();
  }
}
static void early_dash(void) {dash_enable(MMX_DASH_EARLY,"early_dash");}
static void air_dash(void) {dash_enable(MMX_DASH_AIR,"air_dash");}
static void reset(void) {MmxQolReset();MmxDashReset();}
SNES_MOD_CONSTRUCTOR(mmx_register_qol_plugins) {
  snes_mod_register_reset_callback(reset);
  snes_mod_register_activation_plugin("megaman-x.qol.double-tap",double_tap);
  snes_mod_register_activation_plugin("megaman-x.qol.sub-tank",sub_tank);
  snes_mod_register_activation_plugin("megaman-x.qol.auto-charge",auto_charge);
  snes_mod_register_activation_plugin("megaman-x.qol.rapid-fire",rapid_fire);
  snes_mod_register_activation_plugin("megaman-x.qol.dialogue",dialogue);
  snes_mod_register_activation_plugin("megaman-x.qol.skip-intro",skip_intro);
  snes_mod_register_activation_plugin("megaman-x.qol.escape",escape);
  snes_mod_register_activation_plugin("megaman-x.qol.wall-jump",wall_jump);
  snes_mod_register_activation_plugin("megaman-x.qol.first-hadouken",first_hadouken);
  snes_mod_register_activation_plugin("megaman-x.qol.hadouken-iframes",hadouken_iframes);
  snes_mod_register_activation_plugin("megaman-x.qol.hadouken-sigma",hadouken_sigma);
  snes_mod_register_activation_plugin("megaman-x.qol.early-dash",early_dash);
  snes_mod_register_activation_plugin("megaman-x.qol.air-dash",air_dash);
}
