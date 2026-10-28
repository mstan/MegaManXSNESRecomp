#include "mmx_armor.h"
#include "mmx_zero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef MmxArmorGraphic ArmorPose;
typedef struct ArmorGroup {
  uint16_t id,count,animation_size,sequences,records_begin;
  const uint8_t *animation;
  ArmorPose poses[128];
} ArmorGroup;
static uint8_t *pack;
static size_t pack_size;
static unsigned game, frames, sequences, animation_size, records_begin;
static const uint8_t *animation;
static ArmorPose poses[112][5];
static uint16_t colors[2][16];
static uint16_t common_colors[128];
static ArmorGroup extra_groups[16];
static unsigned extra_count;
static const uint8_t *config[11];
static uint16_t config_size[11];
static MmxArmorState state;
static bool (*buster_query)(void);
static bool (*actor_query)(void);
static uint8_t *(*body_query)(uint8_t *);
static bool (*armor_terrain)(const uint8_t *,int,int);
static uint8_t *collision_rom;
static size_t collision_rom_size;
static void buster_player(uint8_t *ram);
static void buster_cancel(uint8_t *ram);
static void retire_shot(uint8_t *ram,unsigned slot);
static void barrier_start(void);
static void barrier_tick(uint8_t *ram);
static void giga_player(uint8_t *ram);
void MmxArmorSetBusterQuery(bool (*query)(void)) {buster_query=query;}
void MmxArmorSetActorQuery(bool (*query)(void)) {actor_query=query;}
void MmxArmorSetBodyQuery(uint8_t *(*query)(uint8_t *)) {body_query=query;}
void MmxArmorSetTerrainQuery(bool (*query)(const uint8_t *,int,int)) {armor_terrain=query;}
void MmxArmorRefill(void) {if(pack && game==3 && state.gold)state.hyper_energy=0x1c00;}
static bool armor_actor(void) {return actor_query?actor_query():!MmxZeroActive();}
const uint16_t *MmxArmorEffectColors(void) {return common_colors;}
static MmxArmorTarget collectibles[13][8];
static uint8_t collectible_count[13];
static unsigned word(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static void putword(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static const uint8_t *rom_at(const uint8_t *rom,size_t size,unsigned a,size_t n) {
  size_t offset=((a>>16)&127)*32768+(a&32767);
  return rom && (a&65535)>=32768 && offset<=size && n<=size-offset?rom+offset:NULL;
}
void MmxArmorSetRom(const uint8_t *rom,size_t size) {
  memset(collectibles,0,sizeof(collectibles));memset(collectible_count,0,sizeof(collectible_count));
  const uint8_t *directory=rom_at(rom,size,0x8582c2,26);
  if(!directory)return;
  for(unsigned stage=0;stage<13;++stage) {
    unsigned a=0x850000|word(directory+stage*2),previous=256;
    bool valid=true;
    for(unsigned guard=0;guard<4096;++guard) {
      const uint8_t *column=rom_at(rom,size,a++,1);
      if(!column) {valid=false;break;}
      if(*column==previous)break;
      if(previous<256 && *column<previous) {valid=false;break;}
      previous=*column;
      bool last=false;
      while(!last) {
        const uint8_t *r=rom_at(rom,size,a,7);a+=7;
        if(!r || a>=0x860000) {valid=false;break;}
        last=(r[6]&128)!=0;
        if(!(r[0]&15) && (r[3]==5 || r[3]==11)) {
          unsigned n=collectible_count[stage];if(n>=8) {valid=false;break;}
          collectibles[stage][n]=(MmxArmorTarget){(uint16_t)(word(r+5)&0x1fff),
              (uint16_t)word(r+1),(uint8_t)(r[3]==11?1:2),r[4]};
          ++collectible_count[stage];
        }
      }
      if(!valid)break;
    }
    if(!valid)collectible_count[stage]=0;
    /* $87:CAEF uses a separate position table, also covering Sting's capsule
     * created by RT-55J's script ($87:FC2B) instead of a stage spawn record. */
    if(stage>=1 && stage<=8) {
      const uint8_t *xy=rom_at(rom,size,0x86d33e + stage*4,4);
      const uint8_t *part=rom_at(rom,size,0x86d362+stage,1);
      if(xy && part && word(xy) && word(xy+2) && *part<4 && collectible_count[stage]<8) {
        unsigned n=collectible_count[stage]++;
        collectibles[stage][n]=(MmxArmorTarget){(uint16_t)word(xy),(uint16_t)word(xy+2),
            (uint8_t)(stage==3?4:3),(uint8_t)(1u<<*part)};
      }
    }
  }
}
unsigned MmxArmorTargets(const uint8_t *r,MmxArmorTarget *out,unsigned capacity) {
  if(!r || !out || r[0x1f7a]>=13)return 0;
  unsigned stage=r[0x1f7a],count=0;
  for(unsigned i=0;i<collectible_count[stage] && count<capacity;++i) {
    MmxArmorTarget t=collectibles[stage][i];
    bool owned=t.kind==1?(r[0x1f9c]&t.mask)!=0:(r[0x1f99]&t.mask)!=0;
    if(t.kind==4)owned=(r[0x1f7e]&128)!=0;
    if(!owned)out[count++]=t;
  }
  return count;
}
bool MmxArmorEnabled(void) {return pack!=NULL;}
unsigned MmxArmorGame(void) {return game;}
MmxArmorState MmxArmorGetState(void) {return state;}
void MmxArmorResetState(void) {memset(&state,0,sizeof(state));}
void MmxArmorDisable(void) {
  free(pack);pack=NULL;pack_size=0;game=frames=sequences=animation_size=records_begin=0;
  animation=NULL;memset(poses,0,sizeof(poses));MmxArmorResetState();
  memset(extra_groups,0,sizeof(extra_groups));memset(common_colors,0,sizeof(common_colors));extra_count=0;
  memset(config,0,sizeof(config));memset(config_size,0,sizeof(config_size));
}
bool MmxArmorGraphicGet(unsigned group,unsigned pose,MmxArmorGraphic *out) {
  if(!pack || !out)return false;
  for(unsigned i=0;i<extra_count;++i)if(extra_groups[i].id==group && pose<extra_groups[i].count) {
    *out=extra_groups[i].poses[pose];return true;
  }
  return false;
}
static const ArmorGroup *extended_group(void) {
  for(unsigned i=0;i<extra_count;++i)if(extra_groups[i].id==0x33)return extra_groups+i;
  return NULL;
}
static bool decode_pose(uint8_t *data,size_t n,size_t *at,ArmorPose *out) {
  if(n-*at<8)return false;
  unsigned x=word(data+*at),y=word(data+*at+2),w=word(data+*at+4),h=word(data+*at+6);*at+=8;
  if(x>128 || y>128 || w>128-x || h>128-y || n-*at<(size_t)w*h || (!w!=!h))return false;
  *out=(ArmorPose){data+*at,(uint16_t)x,(uint16_t)y,(uint16_t)w,(uint16_t)h};
  for(size_t k=0;k<(size_t)w*h;++k)if(data[*at+k]>=128)return false;
  *at+=(size_t)w*h;return true;
}
bool MmxArmorValidState(const MmxArmorState *s) {
  if(!s)return false;
  const ArmorGroup *g=s->anim_group?extended_group():NULL;
  unsigned begin=g?g->records_begin:records_begin,len=g?g->animation_size:animation_size,count=g?g->count:frames;
  for(unsigned i=0;i<8;++i) {
    const MmxArmorShot *p=s->shots+i;
    if(p->active>1 || p->kind>8 || p->sub>4 || p->parent>7 || p->group>0x9e ||
       p->pose>=128 || p->animation>8192 || p->palette>7 || p->history_pos>31 || p->cross_fragment>1 ||
       p->x < -0x1000000 || p->x>0x1000000 || p->y < -0x1000000 || p->y>0x1000000)return false;
  }
  return s->gold<=1 && s->dash_used<=2 && s->dash_ticks<=33 && s->dash_up<=1 &&
      (s->dash_facing==0 || s->dash_facing==64) && s->dash_jump<=1 && s->valid<=1 &&
      s->anim_valid<=1 && s->fire_held<=1 && s->stored_shot<=2 && s->charge<=201 &&
      s->barrier_level<=2 && s->barrier_ticks<=300 && s->recovery_ticks<=308 && s->energy<=0x1c00 &&
      s->barrier_delay<=6 && s->tracer<=1 && s->utility_held<=2 &&
      s->barrier_pose<13 && s->barrier_phase<=2 && s->barrier_offset<=8192 &&
      s->barrier_x>=-0x1000000 && s->barrier_x<=0x1000000 &&
      s->barrier_y>=-0x1000000 && s->barrier_y<=0x1000000 &&
      s->utility_ticks<=180 && s->radar_stage<=13 && s->radar_closing<=1 &&
      s->giga_tick<=242 && s->hyper_energy<=0x1c00 && s->giga_fired<=1 && s->hyper<=1 &&
      !s->utility_reserved &&
      s->anim_group<=1 && s->dash_recovery<=9 && !s->reserved_word &&
      s->burst<=2 && s->burst_air<=1 && s->burst_fired<=1 && s->burst_transition<=1 &&
      (s->burst_facing==0 || s->burst_facing==64) && s->shot_mask<=255 &&
      s->burst_end<=1 && s->burst_queued<=1 && s->burst_pose<112 &&
      (!s->burst || (s->burst_offset>=records_begin && s->burst_offset+3<=animation_size && s->burst_timer)) &&
      (!s->anim_valid || ((!s->anim_group || g) && s->anim_offset>=begin && s->anim_offset+3<=len &&
                          s->anim_timer && s->anim_pose<count));
}
void MmxArmorSetState(MmxArmorState s) {
  MmxArmorResetState();if(pack && MmxArmorValidState(&s)) state=s;
}
bool MmxArmorLoad(const char *path,unsigned selected) {
  MmxArmorDisable();if(!path || (selected!=2 && selected!=3))return false;
  FILE *f=fopen(path,"rb");if(!f)return false;
  if(fseek(f,0,SEEK_END) || ftell(f)<56 || ftell(f)>16*1024*1024) {fclose(f);return false;}
  size_t n=(size_t)ftell(f);rewind(f);uint8_t *data=malloc(n);
  if(!data) {fclose(f);return false;}
  bool ok=fread(data,1,n,f)==n && !memcmp(data,"MMXARMR1",8);fclose(f);
  if(!ok) {free(data);return false;}
  unsigned count=word(data+10),len=word(data+16),start=word(data+18);
  if(word(data+8)!=selected || count!=(selected==2?112u:100u) || word(data+12)!=5 ||
      word(data+14)!=(selected==2?133u:136u) || len!=(selected==2?0x41eu:0x584u) ||
      start!=(selected==2?266u:544u) || n<84+len) {free(data);return false;}
  size_t at=84+len;ArmorPose decoded[112][5]={0};
  for(unsigned i=0;i<count && ok;++i)for(unsigned j=0;j<5 && ok;++j)ok=decode_pose(data,n,&at,&decoded[i][j]);
  ArmorGroup extra[16]={0};unsigned extras=0;uint16_t common[128]={0};
  if(ok && at<n) {
    ok=n-at>=108 && !memcmp(data+at,"ARFX0001",8);at+=8;
    if(ok) {
      extras=word(data+at);ok=extras<=16 && !word(data+at+2);at+=4;
      for(unsigned p=0;p<3;++p)for(unsigned c=0;c<16;++c) {
        unsigned slot=p==0?0:p==1?32:48;
        common[slot+c]=(uint16_t)word(data+at);at+=2;
      }
    }
    for(unsigned i=0;i<extras && ok;++i) {
      if(n-at<12) {ok=false;break;}
      ArmorGroup *g=extra+i;g->id=(uint16_t)word(data+at);g->count=(uint16_t)word(data+at+2);
      g->animation_size=(uint16_t)word(data+at+4);g->sequences=(uint16_t)word(data+at+6);
      g->records_begin=(uint16_t)word(data+at+8);ok=!word(data+at+10);at+=12;
      if(!g->count || g->count>128 || n-at<g->animation_size || g->animation_size>8192 ||
          g->sequences>256 || (g->animation_size?
          g->records_begin<g->sequences*2 || g->records_begin>=g->animation_size:
          g->sequences || g->records_begin)) {ok=false;break;}
      for(unsigned j=0;j<i;++j)if(extra[j].id==g->id)ok=false;
      g->animation=data+at;at+=g->animation_size;
      for(unsigned j=0;j<g->count && ok;++j)ok=decode_pose(data,n,&at,g->poses+j);
    }
  }
  const uint8_t *configuration[11]={0};uint16_t sizes[11]={0};
  if(ok && at<n) {
    unsigned entries=n-at>=12?word(data+at+8):0;
    if(n-at<12 || memcmp(data+at,"ARCF0001",8) || (entries!=6 && entries!=8 && entries!=10) || word(data+at+10))ok=false;
    else {
      at+=12;
      const unsigned expected[]={0,selected==2?256:356,8,selected==2?92:117,36,4,selected==2?4:64,64,24,4,2};
      for(unsigned i=0;i<entries && ok;++i) {
        if(n-at<4) {ok=false;break;}
        unsigned id=word(data+at),size=word(data+at+2);at+=4;
        if(!id || id>10 || configuration[id] || size!=expected[id] || n-at<size) {ok=false;break;}
        configuration[id]=data+at;sizes[id]=(uint16_t)size;at+=size;
      }
    }
  }
  if(!ok || at!=n) {free(data);return false;}
  pack=data;pack_size=n;game=selected;frames=count;sequences=word(data+14);
  animation_size=len;records_begin=start;animation=data+84;
  for(unsigned i=0;i<32;++i)colors[i/16][i%16]=word(data+20+i*2);
  memcpy(poses,decoded,sizeof(poses));memcpy(extra_groups,extra,sizeof(extra_groups));extra_count=extras;
  memcpy(common_colors,common,sizeof(common_colors));
  memcpy(config,configuration,sizeof(config));memcpy(config_size,sizes,sizeof(config_size));return true;
}
static void animation_record(unsigned offset) {
  const ArmorGroup *g=state.anim_group?extended_group():NULL;
  const uint8_t *a=g?g->animation:animation;
  unsigned begin=g?g->records_begin:records_begin,len=g?g->animation_size:animation_size,count=g?g->count:frames;
  if((state.anim_group && !g) || offset<begin || offset+3>len || !a[offset] || a[offset+2]>=count) {
    state.anim_valid=0;return;
  }
  state.anim_offset=(uint16_t)offset;state.anim_timer=a[offset];
  state.anim_flags=a[offset+1];state.anim_pose=a[offset+2];state.anim_valid=1;
}
/* X3's X and Zero use parallel sequence directories. X1 aliases still resume
 * interior running/jumping/firing records rather than restarting each cycle. */
static unsigned source_sequence(unsigned sequence) {
  static const uint8_t x3[]={
    0,1,2,3,5,6,7,10,11,12,13,14,15,16,17,18,19,20,21,22,23,25,30,31,
    32,33,34,35,36,37,38,39,40,41,80,81,82,83,85,86,87,90,91,92,93,94,95,96,
    97,98,99,100,101,102,103,105,110,111,112,113,114,115,116,117,118,119,120,121,
    122,123,124,125,126,127,128,129,130,131,132,48,52};
  static const uint8_t x2[]={
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,
    25,26,27,28,29,30,31,32,33,34,73,74,75,76,77,78,79,80,81,82,83,84,85,86,
    87,88,89,90,91,92,93,95,96,97,98,99,100,101,102,103,104,105,106,107,
    108,109,110,111,112,113,114,115,116,117,118,35,38};
  return sequence<sizeof(x3)?(game==3?x3[sequence]:x2[sequence]):sequences;
}
void MmxArmorAnimationStart(unsigned object,unsigned sequence) {
  if(!pack || object!=0xba8 || MmxZeroActive())return;
  if(state.burst || (state.dash_up && state.dash_ticks) || state.dash_recovery)return;
  state.anim_group=0;
  unsigned mapped=source_sequence(sequence);
  if(mapped<sequences) animation_record(word(animation+mapped*2));else state.anim_valid=0;
}
void MmxArmorAnimationAdvance(unsigned object) {
  if(!pack || object!=0xba8 || MmxZeroActive() || state.burst || !state.anim_valid)return;
  if(--state.anim_timer)return;
  unsigned next=state.anim_offset+3;
  const ArmorGroup *g=state.anim_group?extended_group():NULL;
  const uint8_t *a=g?g->animation:animation;unsigned len=g?g->animation_size:animation_size;
  if(state.anim_flags&128) {
    if(next+2>len){state.anim_valid=0;return;}
    next=(unsigned)((int)next+(int16_t)word(a+next));
  }
  animation_record(next);
}
unsigned MmxArmorUpgradeBits(unsigned pc,unsigned original) {
  if(!pack || MmxZeroActive())return original;
  switch(pc&0x7fffff) {
    case 0x01971c:case 0x019793:case 0x0198fc:return original|8;
    default:return original;
  }
}
void MmxArmorHadoukenAward(const uint8_t *r) {
  if(pack && game==3 && r && r[0x1f7a]==3) {state.gold=1;MmxArmorRefill();}
}
void MmxArmorTakeDamage(uint8_t *r,unsigned object) {
  if(!pack || !r || !armor_actor() || !(r[0x1f99]&4) || object>0x1ffd9)return;
  unsigned raw=r[object+0x26],damage=raw;
  if(!raw || (raw&128))return;
  if(game==2) {
    /* X2 $88:DA15 subtracts one, clamps at one, then subtracts another
     * for values >=5. It does not use X1's rounded half-damage operation. */
    damage=raw>1?raw-1:1;if(damage>=5)--damage;
    /* X2's body converts hostile projectile contacts to Giga energy. Source
     * projectile IDs do not name the same attacks in X1; normalize this
     * adaptation by the actual unarmored X1 contact damage (8.8 units). */
    if(object>=0x1428 && object<0x1628) {
      unsigned energy=state.energy+raw*256;state.energy=(uint16_t)(energy>0x1c00?0x1c00:energy);
    }
  } else {
    /* X3 $84:CE3C: halve, then active standard barrier -1 (consumed),
     * or the enhancement barrier -2 (retained); minimum positive damage 1. */
    damage=raw/2;
    if(state.barrier_level==1 && state.barrier_ticks && !state.barrier_delay) {
      damage=damage>1?damage-1:1;state.barrier_ticks=0;state.barrier_level=0;
    } else if(state.barrier_level==2 && state.barrier_ticks && !state.barrier_delay) {
      damage=damage>2?damage-2:1;
    } else if(!state.barrier_ticks) {
      state.barrier_level=state.gold?2:1;state.barrier_ticks=300;state.barrier_delay=6;
      barrier_start();
    }
    if(!damage)damage=1;
  }
  r[0]=(uint8_t)damage;
}
static bool playable(const uint8_t *r) {
  unsigned a=r[0xbaa];
  return r[0xd1]==2 && r[0xd2]==4 && r[0xd3]==4 && r[0xba9]==2 &&
      (r[0xbcf]&127) && !r[0x1f0c] && !r[0x1f23] && !r[0x1f48] &&
      (a<=0x0a || a==0x10 || a==0x12 || a==0x14 || a==0x20);
}
static void end_dash(uint8_t *r) {
  if(!state.dash_ticks && !state.dash_recovery)return;
  state.dash_ticks=state.dash_up=state.dash_recovery=state.anim_group=state.anim_valid=0;
  r[0xbfd]=0;r[0xc06]&=(uint8_t)~4;
  r[0xbc6]=0x40;
  if(r[0xbaa]==0x14) {r[0xbaa]=8;r[0xbab]=0;}
}
static void dash_recovery(uint8_t *r) {
  state.dash_ticks=0;state.dash_recovery=state.dash_up?9:8;
  r[0xbfd]=0;r[0xc06]&=(uint8_t)~4;r[0xbc6]=0x40;
  putword(r+0xbc2,0);putword(r+0xbc4,0);r[0xbaa]=8;r[0xbab]=0;
  const ArmorGroup *g=extended_group();
  state.anim_group=state.dash_up && g;
  animation_record(state.anim_group?word(g->animation+0x21*2):word(animation+source_sequence(0x38)*2));
}
void MmxArmorPrePlayer(uint8_t *r) {
  if(!pack || !r || MmxZeroActive())return;
  if(r[0xd1]!=2) {MmxArmorResetState();return;}
  if(!state.valid || state.stage!=r[0x1f7a]) {
    state.valid=1;state.stage=r[0x1f7a];state.barrier_ticks=state.barrier_level=state.barrier_delay=0;
    state.dash_used=state.dash_ticks=state.dash_up=state.dash_jump=state.dash_recovery=0;
    state.giga_tick=state.giga_fired=state.hyper=0;MmxArmorRefill();
    if(game!=3)state.tracer=0;state.recovery_ticks=0;
    state.burst=state.burst_end=state.burst_queued=state.stored_shot=state.charge=state.fire_held=0;
    memset(state.shots,0,sizeof(state.shots));state.shot_mask=0;
  }
  barrier_tick(r);
  for(unsigned i=0;i<8;++i) {
    if(!(r[0xbcf]&127))retire_shot(r,i);
    else if(state.shots[i].active && !MmxArmorProjectileOwned(r,0x1228+i*64))
      memset(state.shots+i,0,sizeof(state.shots[i]));
  }
  if(!(r[0xbcf]&127))state.barrier_ticks=state.barrier_level=state.barrier_delay=0;
  if(state.giga_tick) {giga_player(r);return;}
  if(!playable(r)) {
    end_dash(r);
    if(!(r[0xbaa]==0x0e && state.charge && !state.burst && !state.stored_shot))buster_cancel(r);
    return;
  }
  if(state.utility_held) {
    r[0xbde]&=(uint8_t)~0x30;r[0xbe2]&=(uint8_t)~0x30;
    r[0xbdf]&=(uint8_t)~12;r[0xbe3]&=(uint8_t)~12;
  }
  buster_player(r);
  bool grounded=(r[0xbd3]&4)!=0;unsigned action=r[0xbaa];
  /* X3 $84:A9FC: stand still for 308 frames, then one HP every 180.
   * Once full, fill the first owned, non-full native Sub Tank instead. */
  if(game==3 && state.gold && (r[0x1f99]&1) && grounded && action==0 &&
      !(r[0xbde]|r[0xbdf]) && !word(r+0xbc2) && !word(r+0xbc4)) {
    if(!state.recovery_ticks)state.recovery_ticks=308;
    if(!--state.recovery_ticks) {
      state.recovery_ticks=180;
      unsigned hp=r[0xbcf]&127;
      /* X1 $80:D82C redraws the native health meter only when bit 7 is
       * set, then clears it. Notify it on every recovered point. */
      if(hp<r[0x1f9a])r[0xbcf]=(uint8_t)(128|(hp+1));
      else for(unsigned i=0;i<4;++i)if((r[0x1f83+i]&128) && r[0x1f83+i]<0x8e) {++r[0x1f83+i];break;}
    }
  } else state.recovery_ticks=0;
  if(grounded || action==0x10) {end_dash(r);state.dash_used=state.dash_jump=0;}
  if(state.dash_recovery && (r[0xbdf]&3))end_dash(r);
  if(grounded && action==0x14 && (r[0xbe3]&128))state.dash_jump=1;
  if(state.dash_ticks && (!(r[0xbde]&128) ||
      (!state.dash_up && ((r[0xbdf]&(state.dash_facing?2:1)) || (r[0xc06]&3))) ||
      (state.dash_up && !word(r+0xbc4))))dash_recovery(r);
  bool air=!grounded && (action==6 || action==8 || state.dash_ticks);
  unsigned available=game==3 && state.gold?2:1;
  if(air && !state.dash_ticks && !state.dash_recovery && (r[0x1f99]&8) && state.dash_used<available &&
      (!state.dash_jump || (game==3 && state.gold)) && (r[0xbe2]&128)) {
    ++state.dash_used;state.dash_up=game==3 && (r[0xbdf]&8)!=0 && extended_group()!=NULL;
    /* X3's $20 countdown performs its final movement before underflow. */
    state.dash_ticks=state.dash_up?33:17;
    state.dash_facing=(r[0xbdf]&3)==1?64:(r[0xbdf]&3)==2?0:r[0xc11]&64;
    r[0xbaa]=state.dash_up?6:0x14;r[0xbab]=state.dash_up?2:0;
    if(state.dash_up) {
      state.anim_group=1;const ArmorGroup *g=extended_group();animation_record(word(g->animation+0x20*2));
    }
  }
  if(state.dash_ticks) {
    r[0xc11]=state.dash_facing;
    if(state.dash_up) {
      /* $84:8C53 indexes $86:B346 by the native animation phase. Six
       * slow windup phases precede the terminal $0500 upward thrust. */
      r[0xbdf]|=128;r[0xbc6]=0;putword(r+0xbc4,(state.anim_flags&15)<6?0x0078:0x0500);
    } else {r[0xc06]|=4;r[0xc26]|=64;r[0xbde]|=128;putword(r+0xbc4,0);}
    r[0xbe3]&=(uint8_t)~128;
  }
}
void MmxArmorPlayerMotion(uint8_t *r,unsigned object) {
  if(!pack || !r || object!=0xba8 || MmxZeroActive() || (!state.dash_ticks && !state.dash_recovery))return;
  putword(r+0xc04,0x0375);
  putword(r+0xbc2,state.dash_up || state.dash_recovery?0:state.dash_facing?0x0375:-0x0375);r[0xbfa]=0x20;
}
void MmxArmorPlayerEnd(uint8_t *r) {
  if(!pack || !r || MmxZeroActive())return;
  if(state.dash_recovery) {
    if(state.dash_recovery==1) {end_dash(r);r[0xbaa]=8;r[0xbab]=0;}
    else --state.dash_recovery;
  }
  if(state.dash_ticks) {
    if(!state.dash_up)r[0xc06]&=(uint8_t)~4;
    if(r[0xbd3]&4)end_dash(r);
    else if(state.dash_ticks==1 || (!state.dash_up && r[0xbaa]!=0x14))dash_recovery(r);
    else --state.dash_ticks;
  }
}
static void composite(uint8_t *out,const ArmorPose *p) {
  for(unsigned y=0;y<p->h;++y)for(unsigned x=0;x<p->w;++x) {
    unsigned color=p->pixels[y*p->w+x];
    if(color)out[(p->y+y)*128+p->x+x]=(uint8_t)color;
  }
}
bool MmxArmorCompose(const uint8_t *r,const MmxArmorState *snapshot,bool is_x,bool menu,uint8_t *pixels,uint16_t *palette) {
  if(!pack || !r || !is_x || !pixels || !palette)return false;
  MmxArmorState s=snapshot?*snapshot:state;
  unsigned pose=menu?0:s.burst?s.burst_pose:s.anim_valid?s.anim_pose:r[0xbbf]&127;
  if(!menu && s.giga_tick)pose=s.giga_tick<=40?99:s.giga_tick<=46?100:s.giga_tick<=117?101:
      (s.giga_tick-118)%4?103:102;
  if(pose>=(s.anim_group?48:frames))pose=0;
  memset(pixels,0,128*128);memcpy(palette,common_colors,sizeof(common_colors));
  memcpy(palette+16,colors[game==3 && s.gold],sizeof(colors[0]));
  if(!menu && config[7] && s.charge>=141 && (s.charge&4))
    for(unsigned i=0;i<16;++i)palette[16+i]=(uint16_t)word(config[7]+(s.charge>=201?32:0)+i*2);
  unsigned parts=r[0x1f99]&15;
  if(!menu && (r[0xbbe]==0x6a || r[0xbbe]==0x6b)) {
    MmxArmorGraphic p;
    if(MmxArmorGraphicGet((parts&1)?0x6b:0x6a,r[0xbbf]&127,&p)) {composite(pixels,&p);return true;}
    return false;
  }
  if(s.anim_group && !menu) {
    MmxArmorGraphic graphic;
    if(MmxArmorGraphicGet((parts&1)?0x34:0x33,pose,&graphic))composite(pixels,&graphic);
    for(int part=2;part>=0;--part)if((parts&(2u<<part)) && MmxArmorGraphicGet(0x35+part,pose,&graphic))composite(pixels,&graphic);
  } else {
    composite(pixels,&poses[pose][(parts&1)!=0]);
    for(int part=2;part>=0;--part)if(parts&(2u<<part))composite(pixels,&poses[pose][2+part]);
  }
  return true;
}
#include "mmx_armor_combat.inc"
#include "mmx_armor_utilities.inc"
