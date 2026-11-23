#include "mmx_dash.h"
#include <stdio.h>
#include <string.h>

static MmxDashState state;
static MmxSpriteAsset art[6];
static uint8_t layouts[6][1+64*4];
static bool art_ready;
static unsigned options;
_Static_assert(sizeof(MmxDashState)==8,"fixed snapshot dash state");
static void put(uint8_t *r,unsigned at,int value) {unsigned bits=(uint16_t)value;r[at]=(uint8_t)bits;r[at+1]=(uint8_t)(bits>>8);}
void MmxDashResetState(void) {memset(&state,0,sizeof(state));}
void MmxDashEnable(unsigned value) {options|=value&(MMX_DASH_EARLY|MMX_DASH_AIR);}
void MmxDashReset(void) {options=0;MmxDashResetState();}
MmxDashState MmxDashGetState(void) {return state;}
bool MmxDashValidState(const MmxDashState *s) {
  return s && s->valid<=1 && s->stage<13 && s->used<=1 && s->ticks<=17 &&
      (s->facing==0 || s->facing==64) && s->dash_jump<=1 && s->recovery<=8 &&
      !s->reserved && !(s->ticks && s->recovery);
}
void MmxDashSetState(const MmxDashState *s) {
  if(MmxDashValidState(s))state=*s;else MmxDashResetState();
}
bool MmxDashEnabled(void) {return options!=0;}
unsigned MmxDashUpgradeBits(unsigned original,bool is_x) {
  return is_x && (options&MMX_DASH_EARLY)?original|8:original;
}
static void stop(uint8_t *r,bool recovery) {
  if(!state.ticks && !state.recovery)return;
  state.ticks=0;state.recovery=recovery?8:0;
  r[0xc06]&=(uint8_t)~4;r[0xbfd]=0;r[0xbc6]=0x40;
  if(r[0xbaa]==0x14) {r[0xbaa]=8;r[0xbab]=0;}
  if(recovery) {put(r,0xbc2,0);put(r,0xbc4,0);}
}
void MmxDashPrePlayer(uint8_t *r,bool is_x,bool full_armor) {
  if(!is_x)return; /* the other projected seat must not consume X's phase */
  if(full_armor) {MmxDashResetState();return;} /* its qualified movement owns X */
  if(r[0x1f7a]>=13) {stop(r,false);MmxDashResetState();return;}
  if(!state.valid || state.stage!=r[0x1f7a]) {
    MmxDashResetState();state.valid=1;state.stage=r[0x1f7a];
  }
  unsigned action=r[0xbaa];bool grounded=(r[0xbd3]&4)!=0;
  if(!(options&MMX_DASH_AIR) ||
      r[0xd1]!=2 || r[0xd2]!=4 || r[0xd3]!=4 || r[0xba9]!=2 ||
      !(r[0xbcf]&127) || r[0x1f0c] || r[0x1f23] || r[0x1f48] ||
      !(action<=10 || action==0x10 || action==0x12 || action==0x14 || action==0x20)) {
    stop(r,false);return;
  }
  if(grounded || action==0x10) {stop(r,false);state.used=state.dash_jump=0;}
  if(grounded && action==0x14 && (r[0xbe3]&128))state.dash_jump=1;
  if(state.ticks && (!(r[0xbde]&128) || (r[0xc06]&3) ||
      (r[0xbdf]&(state.facing?2:1))))stop(r,true);
  if(state.recovery && (r[0xbdf]&3))stop(r,false);
  if(!grounded && (action==6 || action==8) && !state.ticks && !state.recovery &&
      !state.used && !state.dash_jump && (r[0x1f99]&8) && (r[0xbe2]&128)) {
    state.used=1;state.ticks=17;
    state.facing=(r[0xbdf]&3)==1?64:(r[0xbdf]&3)==2?0:r[0xc11]&64;
    r[0xbaa]=0x14;r[0xbab]=0;
  }
  if(state.ticks) {
    r[0xc11]=state.facing;r[0xc06]|=4;r[0xc26]|=64;
    r[0xbde]|=128;r[0xbe3]&=(uint8_t)~128;put(r,0xbc4,0);
  }
}
void MmxDashPlayerMotion(uint8_t *r,unsigned object,bool is_x,bool full_armor) {
  if(!is_x || full_armor || object!=0xba8 || (!state.ticks && !state.recovery))return;
  put(r,0xc04,0x375);put(r,0xbc2,state.recovery?0:state.facing?0x375:-0x375);r[0xbfa]=0x20;
}
void MmxDashPlayerEnd(uint8_t *r,bool is_x,bool full_armor) {
  if(!is_x || full_armor)return;
  if(state.recovery && !--state.recovery)stop(r,false);
  if(!state.ticks)return;
  r[0xc06]&=(uint8_t)~4; /* only this frame's motion owns the gravity hold */
  if(r[0xbd3]&4) {stop(r,false);state.used=state.dash_jump=0;}
  else if(r[0xbaa]!=0x14)stop(r,false); /* keep hurt/death/door action authoritative */
  else if(state.ticks==1)stop(r,true);
  else --state.ticks;
}
bool MmxDashArtLoad(const char *path) {
  art_ready=false;FILE *f=path?fopen(path,"rb"):NULL;if(!f)return false;
  uint8_t header[12];bool ok=fread(header,1,12,f)==12 && !memcmp(header,"MMXDSH01",8) &&
      header[8]==6 && !header[9] && !header[10] && !header[11];
  for(unsigned i=0;i<6 && ok;++i) {
    uint8_t colors[32]={0};memset(art+i,0,sizeof(art[i]));
    ok=fread(art[i].tiles,1,8192,f)==8192 && fread(colors,1,32,f)==32;
    for(unsigned n=0;n<16;++n)art[i].colors[n]=(uint16_t)(colors[n*2]|colors[n*2+1]<<8);
    unsigned count=(unsigned)fgetc(f);ok=ok && count>0 && count<=64;
    if(ok) {layouts[i][0]=(uint8_t)count;ok=fread(layouts[i]+1,4,count,f)==count;}
  }
  ok=ok && fgetc(f)==EOF && !ferror(f);fclose(f);art_ready=ok;return ok;
}
const MmxSpriteAsset *MmxDashArt(unsigned pose,const uint8_t **arrangement,const uint16_t *colors) {
  if(!art_ready || pose<54 || pose>59)return NULL;
  if(arrangement)*arrangement=layouts[pose-54];
  if(colors) {
    memcpy(art[pose-54].colors,colors,32);
    /* Existing X2 armor bindings use this same material correspondence. */
    art[pose-54].colors[13]=colors[1];
  }
  return art+pose-54;
}
bool MmxDashPieceMissing(const uint8_t *native,const uint8_t donor[4]) {
  if(!native || !donor)return false;
  for(unsigned i=0;i<native[0];++i) {
    const uint8_t *p=native+1+i*4;
    /* X1 stores x/y/tile/flags; X2 stores flags/x/y/tile. Palette bits
     * belong to the live native body, size and flips belong to the piece. */
    if(p[0]==donor[1] && p[1]==donor[2] && p[2]==donor[3] &&
        (p[3]&0xe0)==(donor[0]&0xe0))return false;
  }
  return true;
}
bool MmxDashArtVisible(const uint8_t *r,bool is_x,bool full_armor) {
  return art_ready && is_x && !full_armor && MmxDashEnabled() && !(r[0x1f99]&8) &&
      r[0xd1]==2 && r[0xd2]==4 && r[0xd3]==4 && r[0xbaa]==0x14 && (r[0xbcf]&127);
}
