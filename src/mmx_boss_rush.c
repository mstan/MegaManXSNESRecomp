#include "mmx_boss_rush.h"
#include <string.h>

/* Addresses verified through $80:F8DD's native class dispatch. DIZ labels
 * for Kuwanger and Octopus name related actors, not the boss bodies. */
const MmxBossRushDefinition kMmxBossRushBosses[8]={
  {"CHILL PENGUIN",0x81b516,0x02,8},
  {"SPARK MANDRILL",0x889be1,0x31,6},
  {"ARMORED ARMADILLO",0x83b144,0x14,3},
  {"LAUNCH OCTOPUS",0x81c433,0x07,1},
  {"BOOMER KUWANGER",0x878a7e,0x05,7},
  {"STING CHAMELEON",0x88853e,0x0a,2},
  {"STORM EAGLE",0x87d85c,0x52,5},
  {"FLAME MAMMOTH",0x8791a7,0x0c,4}
};
static MmxBossRushState state;
static MmxBossRushVisualState visual;
void MmxBossRushReset(void) {memset(&state,0,sizeof(state));memset(&visual,0,sizeof(visual));}
MmxBossRushVisualState MmxBossRushVisualGetState(void) {return visual;}
bool MmxBossRushVisualValidState(const MmxBossRushVisualState *s) {
  if(!s) return false;
  for(unsigned i=0;i<2;++i) {
    if(s->initialized[i]>1 || s->reserved[i]) return false;
    for(unsigned c=0;c<16;++c) if(s->colors[i][c]&0x8000) return false;
  }
  return true;
}
bool MmxBossRushVisualSetState(const MmxBossRushVisualState *s) {
  if(!MmxBossRushVisualValidState(s)) return false;
  visual=*s;return true;
}
void MmxBossRushVisualFade(unsigned owner,uint32_t generation,
    const uint16_t target[16],int direction) {
  if(owner>=2 || !target) return;
  if(!visual.initialized[owner] || visual.generation[owner]!=generation) {
    memcpy(visual.colors[owner],target,sizeof(visual.colors[owner]));
    visual.generation[owner]=generation;visual.initialized[owner]=1;
  }
  for(unsigned c=0;c<16;++c) {
    unsigned color=0;
    for(unsigned shift=0;shift<15;shift+=5) {
      unsigned value=(visual.colors[owner][c]>>shift)&31,limit=(target[c]>>shift)&31;
      value=!direction?0:direction<0?(value?value-1:0):(value<limit?value+1:limit);
      color|=value<<shift;
    }
    visual.colors[owner][c]=(uint16_t)color;
  }
}
bool MmxBossRushActive(void) {return state.mode!=MMX_RUSH_OFF;}
bool MmxBossRushCoop(void) {return state.mode==MMX_RUSH_OFF || state.coop!=0;}
MmxBossRushState MmxBossRushGetState(void) {return state;}
bool MmxBossRushValidState(const MmxBossRushState *s) {
  if(!s || s->mode>MMX_RUSH_PREPARING || s->coop>1 || s->loaded>MMX_RUSH_MUSIC_PENDING || s->queued>8 ||
      s->menu>2 || s->selection>3 || s->result_selection>1 || s->actor>2 ||
      s->stage_started>1 || s->return_title>1) return false;
  if(s->mode!=MMX_RUSH_OFF && !s->random) return false;
  if(s->loaded==MMX_RUSH_MUSIC_PENDING && s->mode!=MMX_RUSH_PREPARING) return false;
  if(s->menu==2 && s->selection>1) return false;
  if(s->actor && (s->actor_object<0xe68 || s->actor_object>=0x1d08 ||
      (s->actor_object>=0x1228 && s->actor_object<0x1428) ||
      !s->bosses[s->actor-1].phase)) return false;
  unsigned seen=0;
  for(unsigned i=0;i<s->queued;++i) {
    if(s->queue[i]>=8 || (seen&(1u<<s->queue[i]))) return false;
    seen|=1u<<s->queue[i];
  }
  for(unsigned i=0;i<2;++i) {
    const MmxBossRushBoss *b=&s->bosses[i];
    if(b->id>=8 || b->phase>MMX_RUSH_DYING || b->health>32) return false;
    if(b->phase && (b->object<0xe68 || b->object>=0x1228 || (b->object-0xe68)%64)) return false;
  }
  if(s->bosses[0].phase && s->bosses[1].phase &&
      (s->bosses[0].id==s->bosses[1].id || s->bosses[0].object==s->bosses[1].object)) return false;
  for(unsigned i=0;i<128;++i) if(s->owner[i]>2 || s->occupied[i]>1) return false;
  return true;
}
bool MmxBossRushSetState(const MmxBossRushState *s) {
  if(!MmxBossRushValidState(s)) return false;
  state=*s;return true;
}
static uint32_t random_next(void) {
  uint32_t x=state.random;x^=x<<13;x^=x>>17;x^=x<<5;
  return state.random=x;
}
void MmxBossRushStart(bool coop,uint32_t seed) {
  MmxBossRushReset();state.mode=MMX_RUSH_LOADING;state.coop=coop;
  state.random=seed?seed:0x4d4d5852u;
}
static bool present(unsigned id) {
  for(unsigned i=0;i<2;++i) if(state.bosses[i].phase && state.bosses[i].id==id) return true;
  return false;
}
int MmxBossRushNextBoss(void) {
  if(state.mode!=MMX_RUSH_LOADING && state.mode!=MMX_RUSH_PLAYING) return -1;
  for(unsigned attempt=0;attempt<2;++attempt) {
    for(unsigned i=0;i<state.queued;++i) if(!present(state.queue[i])) {
      unsigned id=state.queue[i];
      memmove(state.queue+i,state.queue+i+1,--state.queued-i);
      state.queue[state.queued]=0;return (int)id;
    }
    /* Carry unavailable bosses forward; append a new shuffled round without
     * duplicating its carried entries. A long-lived boss cannot stall rush. */
    uint8_t fresh[8];unsigned count=0,seen=0;
    for(unsigned i=0;i<state.queued;++i) seen|=1u<<state.queue[i];
    for(unsigned i=0;i<8;++i) if(!(seen&(1u<<i))) fresh[count++]=(uint8_t)i;
    for(unsigned i=count;i>1;--i) {
      unsigned j=random_next()%i;uint8_t b=fresh[i-1];fresh[i-1]=fresh[j];fresh[j]=b;
    }
    memcpy(state.queue+state.queued,fresh,count);state.queued+=(uint8_t)count;
  }
  return -1;
}
bool MmxBossRushAssign(unsigned slot,unsigned id,uint16_t object) {
  if(slot>=2 || id>=8 || state.bosses[slot].phase || present(id) ||
      object<0xe68 || object>=0x1228 || (object-0xe68)%64 ||
      (state.bosses[slot^1].phase && state.bosses[slot^1].object==object)) return false;
  state.bosses[slot]=(MmxBossRushBoss){.object=object,.id=(uint8_t)id,
      .phase=MMX_RUSH_ENTERING,.generation=++state.generation};
  MmxBossRushSetOwner(object,(int)slot);return true;
}
bool MmxBossRushDefeat(unsigned slot) {
  if(slot>=2 || state.bosses[slot].phase!=MMX_RUSH_FIGHTING) return false;
  state.bosses[slot].phase=MMX_RUSH_DYING;state.bosses[slot].ticks=0;
  state.bosses[slot].health=0;
  if(state.defeated!=UINT32_MAX) ++state.defeated;
  return true;
}
void MmxBossRushFinish(void) {state.mode=MMX_RUSH_FINISHED;state.actor=0;}
int MmxBossRushOwner(unsigned object) {
  if(object<0xe68 || object>=0x1d08) return -1;
  return (int)state.owner[(object-0xe68)/32]-1;
}
void MmxBossRushSetOwner(unsigned object,int owner) {
  if(object>=0xe68 && object<0x1d08 && owner>=-1 && owner<2)
    state.owner[(object-0xe68)/32]=(uint8_t)(owner+1);
}
