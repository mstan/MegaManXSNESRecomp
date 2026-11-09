#include "mmx_damage.h"
#include "mmx_coop.h"
#include <string.h>

static unsigned dealt = MMX_DAMAGE_NORMAL, received = MMX_DAMAGE_NORMAL;
static MmxDamageState state;

static unsigned bound(unsigned value) {
  return value < MMX_DAMAGE_MIN ? MMX_DAMAGE_MIN :
      value > MMX_DAMAGE_MAX ? MMX_DAMAGE_MAX : value;
}

unsigned MmxDamageParseMultiplier(const char *value) {
  /* Locale-independent fixed point; reject signs, exponents, NaN, infinity,
   * trailing text and excessive precision rather than converting to float. */
  if (!value || *value < '0' || *value > '9') return MMX_DAMAGE_NORMAL;
  unsigned whole = 0, fraction = 0, digits = 0;
  while (*value >= '0' && *value <= '9') {
    if (whole > 100) return MMX_DAMAGE_NORMAL;
    whole = whole * 10 + (unsigned)(*value++ - '0');
  }
  if (*value == '.') {
    ++value;
    while (*value >= '0' && *value <= '9') {
      if (++digits > 2) return MMX_DAMAGE_NORMAL;
      fraction = fraction * 10 + (unsigned)(*value++ - '0');
    }
    if (!digits) return MMX_DAMAGE_NORMAL;
  }
  if (*value || whole > 10) return MMX_DAMAGE_NORMAL;
  if (digits == 1) fraction *= 10;
  return bound(whole * 100 + fraction);
}

void MmxDamageSetDealt(unsigned value) {
  dealt = bound(value); memset(state.enemies,0,sizeof(state.enemies));
}
void MmxDamageSetReceived(unsigned value) {
  received = bound(value); memset(state.received,0,sizeof(state.received)); state.hp_valid=0;
}
void MmxDamageClearState(void) { memset(&state,0,sizeof(state)); }
void MmxDamageReset(void) { dealt = received = MMX_DAMAGE_NORMAL; MmxDamageClearState(); }
bool MmxDamageDealtEnabled(void) { return dealt != MMX_DAMAGE_NORMAL; }

MmxDamageState MmxDamageGetState(void) { return state; }
bool MmxDamageValidState(const MmxDamageState *s) {
  if(!s || s->hp_valid>3 || s->stage_valid>1) return false;
  for(unsigned i=0;i<2;++i) if(s->received[i]>=100 || s->hp[i]>127) return false;
  for(unsigned i=0;i<15;++i)
    if(s->enemies[i].fraction>=100 || s->enemies[i].hp>127 || s->enemies[i].active>1) return false;
  return true;
}
void MmxDamageSetState(const MmxDamageState *s) {
  if(MmxDamageValidState(s)) state=*s; else MmxDamageClearState();
}
void MmxDamageRestoreWorld(const MmxDamageState *s) {
  memcpy(state.enemies,s->enemies,sizeof(state.enemies));
  state.stage=s->stage; state.stage_valid=s->stage_valid;
}
static void stage(const uint8_t *r) {
  if(!state.stage_valid || state.stage!=r[0x1f7a]) {
    MmxDamageClearState(); state.stage=r[0x1f7a]; state.stage_valid=1;
  }
}
static void health(const uint8_t *r,unsigned actor,unsigned hp) {
  if(!hp || ((state.hp_valid&(1u<<actor)) && hp>state.hp[actor] && hp>=r[0x1f9a]))
    state.received[actor]=0;
  state.hp[actor]=(uint8_t)hp; state.hp_valid|=(uint8_t)(1u<<actor);
}
void MmxDamageFrame(const uint8_t *r) {
  if(dealt==100 && received==100) return;
  stage(r);
  if(dealt%100) for(unsigned i=0;i<15;++i) {
    unsigned d=0xe68+i*64; MmxDamageEnemy *e=state.enemies+i;
    if(!r[d] || !(r[d+0x27]&127) || (e->active && (r[d+10]!=e->kind || (r[d+0x27]&127)>e->hp)))
      memset(e,0,sizeof(*e));
  }
  if(received%100) {
    if(MmxCoopEnabled()) for(unsigned actor=0;actor<2;++actor) {
      const uint8_t *body=MmxCoopCharacterBody((uint8_t *)r,actor);
      health(r,actor,body?body[0x27]&127:0);
    } else health(r,MmxCoopActorIsX()?0:1,r[0xbcf]&127);
  }
}
static unsigned scale(unsigned damage, unsigned multiplier, uint8_t *fraction) {
  if (!damage || multiplier == MMX_DAMAGE_NORMAL) return damage;
  /* Carry hundredths between actual hits. Zero HP loss still keeps the native
   * hurt reaction; weak hits cannot be promoted to a full HP on every contact. */
  if (damage > 127) damage = 127;
  unsigned scaled = damage * multiplier + *fraction;
  unsigned result = scaled / 100;
  *fraction=(uint8_t)(result>=127?0:scaled%100);
  return result > 127 ? 127 : result;
}

unsigned MmxDamageDealt(const uint8_t *r,unsigned enemy,unsigned damage) {
  if(!damage || dealt==100 || !MmxCoopActorIsX()) return damage;
  stage(r);
  if(enemy<0xe68 || enemy>=0x1228 || (enemy-0xe68)%64) return damage;
  unsigned hp=r[enemy+0x27]&127;
  MmxDamageEnemy *e=state.enemies+(enemy-0xe68)/64;
  if(!e->active || e->kind!=r[enemy+10] || hp>e->hp) e->fraction=0;
  e->active=1; e->kind=r[enemy+10];
  unsigned result=scale(damage,dealt,&e->fraction);
  e->hp=(uint8_t)(result>=hp?0:hp-result);
  return result;
}
unsigned MmxDamageReceived(const uint8_t *r,unsigned damage, bool lethal) {
  /* Spikes, pits and crushers retain their native fatal result, including
   * the armor-reduced operand. Learner Mode's zero damage remains zero. */
  if(!damage || received==100) return damage;
  stage(r); unsigned actor=MmxCoopActorIsX()?0:1, hp=r[0xbcf]&127;
  health(r,actor,hp);
  if(lethal) {state.received[actor]=0;state.hp[actor]=0;return damage;}
  unsigned result=scale(damage,received,&state.received[actor]);
  state.hp[actor]=(uint8_t)(result>=hp?0:hp-result);
  return result;
}
