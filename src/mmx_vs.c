#include "mmx_vs.h"
#include <limits.h>
#include <string.h>

static bool enabled;
static MmxVsState state;
static const char *const names[8]={"C. PENGUIN","S. MANDRILL","A. ARMADILLO","L. OCTOPUS",
  "B. KUWANGER","S. CHAMELEON","S. EAGLE","F. MAMMOTH"};
const char *MmxVsBossName(unsigned boss) {return boss<8?names[boss]:"?";}
static const uint8_t slots[8]={8,6,3,1,7,2,5,4};
unsigned MmxVsWeaponSlot(unsigned boss) {return boss<8?slots[boss]:8;}
unsigned MmxVsSlotBoss(unsigned slot) {
  for(unsigned i=0;i<8;++i)if(slots[i]==slot)return i;
  return 0;
}
void MmxVsSetEnabled(bool value) {enabled=value;}
bool MmxVsEnabled(void) {return enabled;}
bool MmxVsActive(void) {return state.phase!=MMX_VS_OFF;}
void MmxVsReset(void) {memset(&state,0,sizeof(state));}
MmxVsState MmxVsGetState(void) {return state;}
bool MmxVsValidState(const MmxVsState *s) {
  if(!s || s->phase>MMX_VS_RESULT || s->first>1 || s->picker>1 || s->locked>3 ||
      s->cursor[0]>7 || s->cursor[1]>7 || s->winner>2 || s->paused>1 ||
      s->menu_ready>1 || s->menu_opening>1 || s->pause_choice>1 || s->advance>1 || s->exit_requested>1 || s->shake_ticks>60 || s->projected>2 || s->proxy>1 || s->proxy_seat>1 ||
      s->proxy_hp>32 || (s->proxy && !s->projected)) return false;
  for(unsigned i=0;i<2;++i) {
    const MmxVsFighter *f=&s->fighters[i];
    if(f->boss>7 || f->hp>32 || f->action>MMX_VS_TELEPORT || f->facing>1 ||
        f->grounded>1 || f->flying>1 || f->attached>1 || f->armor_broken>1 ||
        f->ticks>240 || f->invulnerable>120 || f->stun>240 || f->lockout>240 ||
        f->effect_ticks>180 || f->capture_ticks>120 || f->motion_ticks>60 || f->native_sub>2 ||
        f->started>1 || f->captured>2 || f->last_damage>32 || f->last_class>29 || (f->input|f->pressed|s->input[i]|s->release[i])&0xf000)
      return false;
  }
  for(unsigned i=0;i<128;++i)if(s->attack_class[i]>29)return false;
  if(s->phase==MMX_VS_SELECT && (s->locked==3 || (s->locked&(1u<<s->picker)))) return false;
  if(s->phase>=MMX_VS_LOADING && s->locked!=3) return false;
  return true;
}
bool MmxVsSetState(const MmxVsState *s) {if(!MmxVsValidState(s))return false;state=*s;return true;}
void MmxVsStart(void) {
  MmxVsReset();state.phase=MMX_VS_SELECT;state.winner=MMX_VS_NO_WINNER;state.round=1;
}
bool MmxVsChoose(unsigned seat,unsigned boss) {
  if(state.phase!=MMX_VS_SELECT || seat!=state.picker || boss>=8 || (state.locked&(1u<<seat))) return false;
  state.fighters[seat].boss=(uint8_t)boss;state.cursor[seat]=(uint8_t)boss;
  state.locked|=(uint8_t)(1u<<seat);
  if(state.locked==3) {
    for(unsigned i=0;i<2;++i) {
      unsigned id=state.fighters[i].boss;memset(&state.fighters[i],0,sizeof(state.fighters[i]));
      state.fighters[i].boss=(uint8_t)id;state.fighters[i].hp=32;state.fighters[i].facing=(uint8_t)!i;
    }
    state.phase=MMX_VS_LOADING;state.ticks=0;
  } else state.picker^=1;
  return true;
}
void MmxVsResolveRound(unsigned a,unsigned b) {
  if(state.phase!=MMX_VS_FIGHT || (a&&b)) return;
  state.fighters[0].hp=(uint8_t)(a>32?32:a);state.fighters[1].hp=(uint8_t)(b>32?32:b);
  state.winner=a?0:b?1:MMX_VS_NO_WINNER;
  if(state.winner<2) {
    if(state.score[state.winner]!=UINT32_MAX) ++state.score[state.winner];
    state.first=state.winner;
  }
  state.phase=MMX_VS_RESULT;state.ticks=0;state.paused=0;
  state.release[0]=state.input[0];state.release[1]=state.input[1];
}
void MmxVsNextRound(void) {
  if(state.phase!=MMX_VS_RESULT) return;
  state.phase=MMX_VS_SELECT;state.picker=state.first;state.locked=0;state.ticks=0;
  state.menu_ready=state.menu_opening=0;
  state.advance=0;
  state.shake_ticks=0;state.proxy_source=0;
  memset(state.attack_class,0,sizeof(state.attack_class));
  for(unsigned i=0;i<2;++i) {
    unsigned boss=state.fighters[i].boss;memset(&state.fighters[i],0,sizeof(state.fighters[i]));
    state.fighters[i].boss=(uint8_t)boss;
  }
  if(state.round!=UINT32_MAX)++state.round;
  state.release[0]=state.input[0];state.release[1]=state.input[1];
}
unsigned MmxVsAttackClass(unsigned boss,unsigned action,bool projectile) {
  static const uint8_t elements[8]={7,12,8,14,13,11,9,10};
  if(boss>=8)return 1;
  if(projectile) {
    if(boss==2 || (boss==6 && action!=MMX_VS_PRIMARY) ||
        (boss==5 && action!=MMX_VS_PRIMARY) || (boss==3 && action==MMX_VS_ABILITY))return 1;
    return elements[boss];
  }
  /* Kuwanger's horn grab has Cutter weakness damage, independently of its
   * grab reaction. Armadillo's roll is Shield; other body attacks are physical. */
  if(boss==2 && action==MMX_VS_SPECIAL)return 8;
  return boss==4 && action==MMX_VS_SPECIAL?13:1;
}
bool MmxVsPickVisible(const MmxVsState *s,unsigned viewer,unsigned seat,bool online) {
  return s && viewer<2 && seat<2 && (!online || viewer==seat || s->locked==3);
}
