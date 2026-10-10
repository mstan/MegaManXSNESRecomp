#include "mod_runtime.h"
#include "host_paths.h"
#include "recomp_launcher.h"
#include "snes/interp_bridge.h"
#include "mmx_renderer.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "mmx_weapon_combat.h"
#include "mmx_source_assets.h"
#include "mmx_coop.h"
#include "mmx_coop_view.h"
#include "saber/mmx_saber_plugin.h"
#include "sdl_compat.h"
#include <stdio.h>
#include <string.h>

extern uint8_t g_ram[0x20000];
static void weapon_energy_hook(CpuState *cpu, uint32_t pc) {
  if (!MmxWeaponsEnabled()) return;
  pc &= 0x7fffff;
  if (pc == 0x9ef9) { MmxWeaponsRefill(); return; }
  if (pc == 0x01e169) { MmxWeaponsEnergyOverflow(g_ram,cpu->X); return; }
  bool store = pc == 0xd91d || pc == 0xd95e || pc == 0xd9ab || pc == 0x01e0ce;
  if (store) {
    bool wide = pc == 0x01e0ce;
    if (MmxWeaponsEnergyStore(cpu->A,wide)) {
      /* Skip only this inventory STA. Preserve its accumulator and flags,
       * and retain the generated tier's ordinary absolute,Y write timing. */
      extern uint8_t g_memsel;
      cpu->cycles += wide ? 6 : 5;
      cpu->master_cycles += (wide ? 6 : 5) * (g_memsel ? 6 : 8);
      interp_bridge_pre_opcode_redirect((cpu->PB << 16) | ((pc + 3) & 65535));
    }
    return;
  }
  bool index = pc == 0xd90f || pc == 0xd94d || pc == 0x01e058 || pc == 0x01e0a7;
  bool y = pc == 0x01e058 || pc == 0x01e0a7, wide = pc == 0x01e0ac;
  unsigned mask = wide ? 65535 : 255;
  unsigned original = y ? cpu->Y : cpu->A & mask;
  unsigned value = MmxWeaponsEnergyRead(index ? 0xbdb : wide ? 0x1f85 : 0x1f86, original);
  if (y) cpu->Y = (uint16_t)value;
  else cpu->A = (uint16_t)((cpu->A & ~mask) | value);
  cpu->_flag_Z = !value; cpu->_flag_N = (value & (wide ? 32768 : 128)) != 0;
  cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 128 : 0);
}
static void weapon_menu_hook(CpuState *cpu, uint32_t pc) {
  if (!MmxWeaponsEnabled()) return;
  pc &= 0xffff;
  if (pc == 0xc67f) { MmxWeaponsMenuTick(g_ram, cpu->D); return; }
  /* Interpreted hooks run just after the original LDA. */
  unsigned source = pc == 0xc487 ? 0xc484 : pc == 0xc76c ? 0xc767 :
      pc == 0xce2b ? 0xce28 : pc == 0xce35 ? 0xce33 :
      pc == 0xc79a ? 0xc792 : pc == 0xc7bd ? 0xc7b5 :
      pc == 0xc80d ? 0xc805 : pc == 0xc82c ? 0xc826 :
      pc == 0xc86d ? 0xc865 : 0xc886;
  unsigned value = MmxWeaponsMenuRead(g_ram, source, cpu->D, cpu->X, cpu->A & 255);
  cpu->A = (cpu->A & 0xff00) | value;
  cpu->_flag_Z = value == 0; cpu->_flag_N = (value & 128) != 0;
  cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 128 : 0);
}
static void hook(CpuState *cpu, uint32_t pc) {
  if (!MmxZeroEnabled() && !MmxWeaponsEnabled()) return;
  switch (pc & 0x7fffff) {
    case 0x009dca: MmxZeroHealthRespawn(g_ram); break;
    case 0x00d6a7: MmxRendererObserveObject(g_ram, (uint16_t)(cpu->D + cpu->X)); break;
    case 0x00d76a: MmxRendererRecordPiece(g_ram, cpu->D); break;
    case 0x01971f: case 0x019796: case 0x0198ff: if (MmxZeroActive()) cpu->A |= 8; break;
    case 0x01815c:
      MmxZeroExtPrePlayer(g_ram);
      MmxZeroSlideTick(g_ram);
      MmxZeroMovementTick(g_ram);
      MmxWeaponsPlayerTick(g_ram);
      if (!MmxWeaponsCombatActive()) MmxZeroPlayerTick(g_ram);
      break;
    case 0x019d47: MmxWeaponsMarkShot(g_ram, cpu->X); break;
    case 0x0194af: MmxWeaponsSelectShot(g_ram, cpu->X); break;
    case 0x018165: MmxZeroPlayerEnd(g_ram); MmxZeroExtPlayerEnd(g_ram); break;
    case 0x0491dc: MmxWeaponsTerrainEnd(g_ram,cpu->D); break;
    case 0x01898e: case 0x018999: case 0x018965: {
      static const struct { unsigned from, to; } slide[] = {
        {0x898e,0x8991}, {0x8999,0x899c}, {0x8965,0x8971}};
      for (unsigned i = 0; i < 3; ++i)
        if ((pc & 0xffff) == slide[i].from && MmxZeroSlideHold(g_ram, pc))
          interp_bridge_pre_opcode_redirect((pc & 0xff0000) | slide[i].to);
      break;
    }
    case 0x02823e:
      MmxZeroPlayerMotion(g_ram,cpu->D);
      MmxWeaponsPlayerMotion(g_ram,cpu->D); break;
    case 0x018b04: MmxZeroDeathOrbSpawn(g_ram, cpu->D, cpu->X); break;
    case 0x048f07: MmxZeroAnimationStart(cpu->D, cpu->A & 255); break;
    case 0x048eea: MmxZeroAnimationAdvance(cpu->D); break;
    case 0x028403: case 0x03958f: case 0x039dcf: {
      unsigned value = MmxZeroWeaponOrigin(g_ram,cpu->D,(pc & 0xffff) != 0x958f,cpu->A);
      cpu->A = (uint16_t)value;
      /* The intercepted STA does not set flags. Preserve the original LDA/
       * ADC flags; only the coordinate being stored changes. */
      break;
    }
    case 0x01a57d: case 0x038b6f: case 0x038d87: case 0x038ed7:
    case 0x03951d: case 0x039841: case 0x039993: case 0x03a3cd:
    case 0x01a589: case 0x038b7b: case 0x038d93: case 0x038ee3:
    case 0x039529: case 0x03984d: case 0x03999f: case 0x03a3d9: {
      unsigned axis = (pc & 0xffff) == 0xa589 || (pc & 0xffff) == 0x8b7b ||
          (pc & 0xffff) == 0x8d93 || (pc & 0xffff) == 0x8ee3 ||
          (pc & 0xffff) == 0x9529 || (pc & 0xffff) == 0x984d ||
          (pc & 0xffff) == 0x999f || (pc & 0xffff) == 0xa3d9;
      unsigned value = MmxZeroMuzzle(g_ram,cpu->D,cpu->X,axis,cpu->A & 255);
      cpu->A = (cpu->A & 0xff00) | value;
      cpu->_flag_Z = !value; cpu->_flag_N = (value & 128) != 0;
      cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 128 : 0);
      break;
    }
    case 0x00d4f4: case 0x00d511: {
      unsigned value=MmxWeaponsEnemyActive(g_ram,cpu->D,cpu->A&255);
      cpu->A=(cpu->A&0xff00)|value;
      cpu->_flag_Z=!value;cpu->_flag_N=(value&128)!=0;
      cpu->P=(cpu->P&~0x82)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
      break;
    }
    case 0x00d3e7: {
      unsigned value = MmxWeaponsProjectileTick(g_ram, cpu->D,
          MmxZeroWeaponTick(g_ram, cpu->D, cpu->A & 255));
      cpu->A = (cpu->A & 0xff00) | value;
      cpu->_flag_Z = !value; cpu->_flag_N = (value & 128) != 0;
      cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 128 : 0);
      break;
    }
    case 0x049e1d: case 0x049e3a: {
      bool reaction=(pc&65535)==0x9e3a;
      unsigned value=MmxWeaponsContactClass(g_ram,cpu->D,cpu->X,cpu->A,reaction);
      cpu->A=(uint16_t)value;
      cpu->_flag_Z=!(value&(reaction?255:65535));cpu->_flag_N=(value&(reaction?128:32768))!=0;
      cpu->P=(cpu->P&~0x82)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
      break;
    }
    case 0x049e45: {
      /* Interpret the pre-opcode $EF37+Y load directly; A is not published
       * yet at this point, so this mirrors the compiled hook's input. */
      unsigned original = cpu_read8(cpu, cpu->DB, (uint16_t)(0xef37 + cpu->Y));
      unsigned value = MmxZeroResponse(g_ram, cpu->D, cpu->X, original);
      value &= 255;
      cpu->A = (cpu->A & 0xff00) | value;
      cpu->_flag_Z = (value & 255) == 0;
      cpu->_flag_N = (value & 128) != 0;
      cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) |
          (cpu->_flag_N ? 128 : 0);
      break;
    }
    case 0x049e76: {
      unsigned original = cpu_read8(cpu, cpu->DB, (uint16_t)(0xef37 + cpu->Y));
      unsigned damage = MmxWeaponsDamage(g_ram, cpu->D, cpu->X,
          MmxZeroDamage(g_ram, cpu->D, cpu->X, original));
      if (damage == original) break;
      /* Post-SBC: retain the interpreter's instruction timing, but recompute
       * its value and all arithmetic flags with our damage operand. The HP
       * store has not happened yet and SEC immediately precedes the SBC. */
      unsigned hp = g_ram[cpu->D + 0x27] & 127, result;
      if (cpu->_flag_D) {
        unsigned complement = damage ^ 255;
        int decimal = (hp & 15) + (complement & 15) + 1;
        if (decimal < 16) decimal = (decimal - 6) & (decimal < 6 ? 15 : 31);
        decimal += (hp & 240) + (complement & 240);
        cpu->_flag_V = ((hp & 128) == (complement & 128)) && ((complement & 128) != (decimal & 128));
        if (decimal < 256) decimal -= 96;
        cpu->_flag_C = decimal > 255; result = (unsigned)decimal & 255;
      } else {
        result = (hp - damage) & 255;
        cpu->_flag_C = hp >= damage;
        cpu->_flag_V = ((hp ^ damage) & (hp ^ result) & 128) != 0;
      }
      cpu->A = (cpu->A & 0xff00) | result;
      cpu->_flag_Z = result == 0; cpu->_flag_N = (result & 128) != 0;
      cpu->P = (cpu->P & ~0xc3) | cpu->_flag_C | (cpu->_flag_Z ? 2 : 0) |
          (cpu->_flag_V ? 64 : 0) | (cpu->_flag_N ? 128 : 0);
      break;
    }
    case 0x049c19:
      cpu->Y = (uint16_t)MmxWeaponsHitbox(g_ram, cpu->D, cpu->X,
          MmxZeroHitbox(g_ram, cpu->D, cpu->X, cpu->Y));
      cpu->_flag_Z = cpu->Y == 0; cpu->_flag_N = (cpu->Y & 0x8000) != 0;
      cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 128 : 0);
      break;
  }
}
static bool zero_terrain_solid(const uint8_t *r, int x, int y) {
  return MmxWeaponsTerrainSolid(r, x, y, false, NULL);
}
void MmxZeroRegisterHooks(void) {
  MmxZeroSetTerrainQuery(zero_terrain_solid);
  const unsigned pcs[] = {0x009dca, 0x00d6a7, 0x00d76a, 0x01971f, 0x019796, 0x0198ff,
                          0x01815c, 0x018165, 0x019d47, 0x0194af, 0x00d3e7, 0x00d4f4, 0x00d511, 0x049e1d, 0x049e3a, 0x049e45, 0x049e76, 0x049c19, 0x048f07, 0x048eea, 0x018b04, 0x0491dc, 0x02823e,
                          0x01898e, 0x018999, 0x018965,
                          0x028403,0x03958f,0x039dcf,
                          0x01a57d,0x038b6f,0x038d87,0x038ed7,0x03951d,0x039841,0x039993,0x03a3cd,
                          0x01a589,0x038b7b,0x038d93,0x038ee3,0x039529,0x03984d,0x03999f,0x03a3d9};
  for (unsigned i = 0; i < sizeof(pcs) / sizeof(pcs[0]); ++i)
    interp_bridge_set_pre_opcode_hook(pcs[i], hook);
  const unsigned menu_pcs[] = {0xc67f,0xc487,0xc76c,0xce2b,0xce35,
      0xc79a,0xc7bd,0xc80d,0xc82c,0xc86d,0xc88c};
  for (unsigned i = 0; i < sizeof(menu_pcs) / sizeof(menu_pcs[0]); ++i)
    interp_bridge_set_pre_opcode_hook(menu_pcs[i], weapon_menu_hook);
  const unsigned energy_pcs[] = {0x9ef9,0xd90f,0xd91b,0xd91d,0xd94d,0xd951,0xd95e,0xd9a7,0xd9ab,
      0x01e058,0x01e065,0x01e0a7,0x01e0ac,0x01e0ce,0x01e169};
  for (unsigned i=0;i<sizeof(energy_pcs)/sizeof(energy_pcs[0]);++i)
    interp_bridge_set_pre_opcode_hook(energy_pcs[i],weapon_energy_hook);
}
static int prepare(const char *package, const char *feature, unsigned game, int zero, char path[4096]) {
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource = {0};
  char leaf[100],error[512];
  if (!provider || !provider->feature_resource_get ||
      !provider->feature_resource_get(provider->ctx,package,feature,0,&resource) || !resource.path[0]) return 0;
  snprintf(leaf,sizeof(leaf),"cache/mmx-source/x%u-%s.bin",game,zero?"zero-v7":"weapons-v5");
  if (!snesrecomp_exe_dir_path(leaf,path,4096)) return 0;
  if (MmxSourceAssetsBuild(resource.path,game,zero,path,error,sizeof(error))) return 1;
  fprintf(stderr,"[mmx-source] %s\n",error);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare Mega Man mod",error,NULL);
  return 0;
}
static void activate(void) {
  /* Co-op claims this existing plugin as its character-mode exclusion key.
   * Its dedicated activation below owns preparation in that mode. */
  if(snes_mod_runtime_feature_enabled_c("megaman-x.coop","coop")) return;
  char path[4096],start[16]={0},behavior[16]={0};
  if (!prepare("megaman-x.character.zero","zero",3,1,path)) return;
  snes_mod_runtime_feature_option_value_c("megaman-x.character.zero","zero","start",start,sizeof(start));
  MmxZeroSetStartCharacter(strcmp(start,"zero") != 0);
  snes_mod_runtime_feature_option_value_c("megaman-x.character.zero","zero","behavior",behavior,sizeof(behavior));
  MmxZeroSetModern(!strcmp(behavior,"modern"));
  MmxZeroResetState();
  if (!MmxZeroLoad(path)) {
    fprintf(stderr, "[mmx-zero] Cannot load extracted Zero assets: %s\n", path); return;
  }
  MmxZeroRegisterHooks();
  if (!strcmp(behavior,"saber")) MmxSaberActivate("megaman-x.character.zero","zero",false);
  fprintf(stderr, "[mmx-zero] Zero 0.0.1 enabled; starting as %s\n", strcmp(start,"zero") ? "X" : "Zero");
}
static void activate_coop(void) {
  char path[4096],character[32]={0},behavior[16]={0},cameras[32]={0},hud[32]={0};
  if(!prepare("megaman-x.coop","coop",3,1,path) || !MmxZeroLoad(path)) return;
  snes_mod_runtime_feature_option_value_c("megaman-x.coop","coop","player1",character,sizeof(character));
  snes_mod_runtime_feature_option_value_c("megaman-x.coop","coop","behavior",behavior,sizeof(behavior));
  MmxZeroSetModern(!strcmp(behavior,"modern"));
  snes_mod_runtime_feature_option_value_c("megaman-x.coop","coop","cameras",cameras,sizeof(cameras));
  MmxCoopViewsSetIndependent(!strcmp(cameras,"independent"));
  snes_mod_runtime_feature_option_value_c("megaman-x.coop","coop","hud",hud,sizeof(hud));
  MmxRendererSetCompactCoopHud(strcmp(hud,"horizontal")!=0);
  unsigned p1=!strcmp(character,"zero") ? MMX_COOP_ZERO : MMX_COOP_X;
  MmxZeroRegisterHooks();MmxCoopRegisterHooks();
  if(!MmxCoopEnable(p1)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot enable co-op","Cannot initialize the selected characters.",NULL);
    return;
  }
  if(!strcmp(behavior,"saber")) MmxSaberActivate("megaman-x.coop","coop",true);
  fprintf(stderr,"[mmx-coop] Co-op enabled; P1 is %s\n",p1==MMX_COOP_X?"X":"Zero");
}
static void activate_weapons(unsigned game) {
  char path[4096];
  const char *package=game==2?"megaman-x.weapons.x2":"megaman-x.weapons.x3";
  if (!prepare(package,"weapons",game,0,path)) return;
  if (!MmxWeaponsLoadPage(path,game-1)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot prepare Mega Man mod","Cannot load the generated weapon data.",NULL);
    return;
  }
  MmxZeroRegisterHooks();
  fprintf(stderr,"[mmx-weapons] X%u weapons loaded from the selected source ROM\n",game);
}
static void activate_x2(void) { activate_weapons(2); }
static void activate_x3(void) { activate_weapons(3); }
static void reset(void) {
  MmxRendererSetCompactCoopHud(true);
  MmxCoopViewsSetIndependent(false);
  MmxCoopDisable();
  if (MmxWeaponsEnabled()) g_ram[0x1f12] = 0;
  MmxZeroSetStartCharacter(false);
  MmxZeroSetModern(false);
  MmxWeaponsCancelShots(g_ram); MmxZeroCancel(g_ram); MmxZeroDisable(); MmxWeaponsDisable();
}
SNES_MOD_CONSTRUCTOR(mmx_register_zero_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.zero", activate);
  (void)snes_mod_register_activation_plugin("megaman-x.coop",activate_coop);
  (void)snes_mod_register_activation_plugin("megaman-x.weapons.x2",activate_x2);
  (void)snes_mod_register_activation_plugin("megaman-x.weapons.x3",activate_x3);
}
