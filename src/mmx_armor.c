#include "mmx_armor.h"
#include "mmx_zero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ArmorPose {const uint8_t *pixels; uint16_t x,y,w,h;} ArmorPose;
static uint8_t *pack;
static size_t pack_size;
static unsigned game, frames, sequences, animation_size, records_begin;
static const uint8_t *animation;
static ArmorPose poses[112][5];
static uint16_t colors[2][16];
static MmxArmorState state;
static unsigned word(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static void putword(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
bool MmxArmorEnabled(void) {return pack!=NULL;}
unsigned MmxArmorGame(void) {return game;}
MmxArmorState MmxArmorGetState(void) {return state;}
void MmxArmorResetState(void) {memset(&state,0,sizeof(state));}
void MmxArmorDisable(void) {
  free(pack);pack=NULL;pack_size=0;game=frames=sequences=animation_size=records_begin=0;
  animation=NULL;memset(poses,0,sizeof(poses));MmxArmorResetState();
}
bool MmxArmorValidState(const MmxArmorState *s) {
  return s && s->gold<=1 && s->dash_used<=2 && s->dash_ticks<=32 && s->dash_up<=1 &&
      (s->dash_facing==0 || s->dash_facing==64) && s->dash_jump<=1 && s->valid<=1 &&
      s->anim_valid<=1 && s->fire_held<=1 && s->stored_shot<=2 && s->charge<=201 &&
      s->barrier_level<=2 && s->barrier_ticks<=300 && s->recovery_ticks<=308 && s->energy<=0x1c00 &&
      s->barrier_delay<=6 && s->tracer<=1 && s->utility_held<=1 &&
      !s->reserved[0] && !s->reserved[1] && !s->reserved_word &&
      (!s->anim_valid || (s->anim_offset>=records_begin && s->anim_offset+3<=animation_size &&
                          s->anim_timer && s->anim_pose<frames));
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
  for(unsigned i=0;i<count && ok;++i)for(unsigned j=0;j<5 && ok;++j) {
    if(n-at<8) {ok=false;break;}
    unsigned x=word(data+at),y=word(data+at+2),w=word(data+at+4),h=word(data+at+6);at+=8;
    if(x>128 || y>128 || w>128-x || h>128-y || n-at<(size_t)w*h || (!w!=!h)) {ok=false;break;}
    decoded[i][j]=(ArmorPose){data+at,(uint16_t)x,(uint16_t)y,(uint16_t)w,(uint16_t)h};
    for(size_t k=0;k<(size_t)w*h;++k) if(data[at+k]>=128) {ok=false;break;}
    at+=(size_t)w*h;
  }
  if(!ok || at!=n) {free(data);return false;}
  pack=data;pack_size=n;game=selected;frames=count;sequences=word(data+14);
  animation_size=len;records_begin=start;animation=data+84;
  for(unsigned i=0;i<32;++i)colors[i/16][i%16]=word(data+20+i*2);
  memcpy(poses,decoded,sizeof(poses));return true;
}
static void animation_record(unsigned offset) {
  if(offset<records_begin || offset+3>animation_size || !animation[offset] || animation[offset+2]>=frames) {
    state.anim_valid=0;return;
  }
  state.anim_offset=(uint16_t)offset;state.anim_timer=animation[offset];
  state.anim_flags=animation[offset+1];state.anim_pose=animation[offset+2];state.anim_valid=1;
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
  unsigned mapped=source_sequence(sequence);
  if(mapped<sequences) animation_record(word(animation+mapped*2));else state.anim_valid=0;
}
void MmxArmorAnimationAdvance(unsigned object) {
  if(!pack || object!=0xba8 || MmxZeroActive() || !state.anim_valid)return;
  if(--state.anim_timer)return;
  unsigned next=state.anim_offset+3;
  if(state.anim_flags&128) {
    if(next+2>animation_size){state.anim_valid=0;return;}
    next=(unsigned)((int)next+(int16_t)word(animation+next));
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
  if(pack && game==3 && r && r[0x1f7a]==3)state.gold=1;
}
void MmxArmorTakeDamage(uint8_t *r,unsigned object) {
  if(!pack || !r || MmxZeroActive() || !(r[0x1f99]&4) || object>0x1ffd9)return;
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
  if(!state.dash_ticks)return;
  state.dash_ticks=state.dash_up=0;r[0xbfd]=0;r[0xc06]&=(uint8_t)~4;
  r[0xbc6]=0x40;
  if(r[0xbaa]==0x14) {r[0xbaa]=8;r[0xbab]=0;}
}
void MmxArmorPrePlayer(uint8_t *r) {
  if(!pack || !r || MmxZeroActive())return;
  if(r[0xd1]!=2) {MmxArmorResetState();return;}
  if(!state.valid || state.stage!=r[0x1f7a]) {
    state.valid=1;state.stage=r[0x1f7a];state.barrier_ticks=state.barrier_level=state.barrier_delay=0;
    state.dash_used=state.dash_ticks=state.dash_up=state.dash_jump=0;
    state.tracer=0;state.recovery_ticks=0;
  }
  if(state.barrier_ticks && !--state.barrier_ticks)state.barrier_level=state.barrier_delay=0;
  if(state.barrier_delay)--state.barrier_delay;
  if(!(r[0xbcf]&127))state.barrier_ticks=state.barrier_level=state.barrier_delay=0;
  if(!playable(r)) {end_dash(r);return;}
  bool grounded=(r[0xbd3]&4)!=0;unsigned action=r[0xbaa];
  /* X3 $84:A9FC: stand still for 308 frames, then one HP every 180.
   * Once full, fill the first owned, non-full native Sub Tank instead. */
  if(game==3 && state.gold && (r[0x1f99]&1) && grounded && action==0 &&
      !(r[0xbde]|r[0xbdf]) && !word(r+0xbc2) && !word(r+0xbc4)) {
    if(!state.recovery_ticks)state.recovery_ticks=308;
    if(!--state.recovery_ticks) {
      state.recovery_ticks=180;
      unsigned hp=r[0xbcf]&127;
      if(hp<r[0x1f9a])r[0xbcf]=(uint8_t)((r[0xbcf]&128)|(hp+1));
      else for(unsigned i=0;i<4;++i)if((r[0x1f83+i]&128) && r[0x1f83+i]<0x8e) {++r[0x1f83+i];break;}
    }
  } else state.recovery_ticks=0;
  if(grounded || action==0x10) {end_dash(r);state.dash_used=state.dash_jump=0;}
  if(grounded && action==0x14 && (r[0xbe3]&128))state.dash_jump=1;
  if(state.dash_ticks && (!(r[0xbde]&128) ||
      (!state.dash_up && ((r[0xbdf]&(state.dash_facing?2:1)) || (r[0xc06]&3))) ||
      (state.dash_up && !word(r+0xbc4))))end_dash(r);
  bool air=!grounded && (action==6 || action==8 || state.dash_ticks);
  unsigned available=game==3 && state.gold?2:1;
  if(air && !state.dash_ticks && (r[0x1f99]&8) && state.dash_used<available &&
      (!state.dash_jump || (game==3 && state.gold)) && (r[0xbe2]&128)) {
    ++state.dash_used;state.dash_ticks=17;state.dash_up=game==3 && (r[0xbdf]&8)!=0;
    state.dash_facing=(r[0xbdf]&3)==1?64:(r[0xbdf]&3)==2?0:r[0xc11]&64;
    r[0xbaa]=state.dash_up?6:0x14;r[0xbab]=state.dash_up?2:0;
  }
  if(state.dash_ticks) {
    r[0xc11]=state.dash_facing;
    if(state.dash_up) {
      r[0xbdf]|=128;r[0xbc6]=0;putword(r+0xbc4,0x0375);
    } else {r[0xc06]|=4;r[0xc26]|=64;r[0xbde]|=128;putword(r+0xbc4,0);}
    r[0xbe3]&=(uint8_t)~128;
  }
}
void MmxArmorPlayerMotion(uint8_t *r,unsigned object) {
  if(!pack || !r || object!=0xba8 || MmxZeroActive() || !state.dash_ticks)return;
  putword(r+0xc04,0x0375);
  putword(r+0xbc2,state.dash_up?0:state.dash_facing?0x0375:-0x0375);r[0xbfa]=0x20;
}
void MmxArmorPlayerEnd(uint8_t *r) {
  if(!pack || !r || MmxZeroActive())return;
  if(state.dash_ticks) {
    if(!state.dash_up)r[0xc06]&=(uint8_t)~4;
    if((r[0xbd3]&4) || state.dash_ticks==1 ||
        (!state.dash_up && r[0xbaa]!=0x14))end_dash(r);
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
  unsigned pose=menu?0:s.anim_valid?s.anim_pose:r[0xbbf]&127;
  if(pose>=frames)pose=0;
  memset(pixels,0,128*128);memset(palette,0,128*sizeof(*palette));
  memcpy(palette+16,colors[game==3 && s.gold],sizeof(colors[0]));
  unsigned parts=r[0x1f99]&15;
  composite(pixels,&poses[pose][(parts&1)!=0]);
  for(int part=2;part>=0;--part)if(parts&(2u<<part))composite(pixels,&poses[pose][2+part]);
  return true;
}
