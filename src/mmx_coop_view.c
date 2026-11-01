#include "mmx_coop_view.h"
#include "mmx_wide_policy.h"

static bool active;
static bool local_split;
void MmxCoopViewsSetLocalSplit(bool enabled) { local_split=enabled; }
bool MmxCoopViewsLocalSplit(void) { return local_split; }
/* Agreed online camera option; local layout is a separate preference. */
static bool independent=false;
void MmxCoopViewsSetIndependent(bool enabled) { independent=enabled; }
bool MmxCoopViewsIndependent(void) { return independent; }
static MmxCoopViewWorldState world;
static MmxCoopViewBoundsState bounds;
static const uint8_t *rom;
static size_t rom_size;
void MmxCoopViewsSetRom(const uint8_t *bytes,size_t size) {rom=bytes;rom_size=size;}
MmxCoopViewBoundsState MmxCoopViewsGetBoundsState(void) {return bounds;}
bool MmxCoopViewsValidBoundsState(const MmxCoopViewBoundsState *b) {
  if(!b || b->valid>3 || b->camera_valid>3 || b->stage>12) return false;
  for(unsigned seat=0;seat<2;++seat) for(unsigned i=0;i<4;++i)
    if((b->valid&(1u<<seat)) && b->limits[seat][i]>8192) return false;
  for(unsigned seat=0;seat<2;++seat) if(b->camera_valid&(1u<<seat)) {
    for(unsigned i=0;i<4;++i) if(b->current[seat][i]>8192) return false;
    for(unsigned i=0;i<2;++i) if(b->camera[seat][i]>8192) return false;
  }
  return true;
}
void MmxCoopViewsSetBoundsState(const MmxCoopViewBoundsState *b) {bounds=*b;}
MmxCoopViewWorldState MmxCoopViewsGetWorldState(void) { return world; }
void MmxCoopViewsSetWorldState(const MmxCoopViewWorldState *s) { world=*s; }
void MmxCoopViewsResetWorld(void) { memset(&world,0,sizeof(world));memset(&bounds,0,sizeof(bounds)); }
void MmxCoopViewsActorReturn(unsigned player) { world.actor_return=(uint8_t)player; }
void MmxCoopViewsContactPlayer(unsigned player) { world.contact_player=(uint8_t)player; }
void MmxCoopViewsBeginStage(unsigned stage) {
  if(!world.initialized || world.stage!=stage) {
    MmxCoopViewsResetWorld();world.stage=(uint8_t)stage;world.initialized=1;
  }
}
bool MmxCoopViewsSeen(unsigned flag) {
  return flag>=0xf800 && flag<=0xffff && (world.seen[(flag-0xf800)>>3]&(1u<<(flag&7)));
}
void MmxCoopViewsMark(unsigned flag,bool seen) {
  if(flag<0xf800 || flag>0xffff) return;
  unsigned n=(flag-0xf800)>>3,bit=1u<<(flag&7);
  if(seen) world.seen[n]|=bit;else world.seen[n]&=~bit;
}
void MmxCoopViewsSetActive(bool enabled) {
  if(active!=enabled) bounds.camera_valid=0;
  active=enabled;
}
bool MmxCoopViewsActive(void) { return active; }
static int word(const uint8_t *r) { return r[0]|r[1]<<8; }
static void putword(uint8_t *r,unsigned value) {r[0]=(uint8_t)value;r[1]=(uint8_t)(value>>8);}
static int clamp(int v,int lo,int hi) { if(hi<lo) hi=lo;return v<lo?lo:v>hi?hi:v; }
static bool living(const MmxCoopPlayer *p) {
  return p->status==MMX_COOP_ALIVE && (p->body[0x27]&127) && p->body[2]!=12;
}
static unsigned rom_word(unsigned address) {
  size_t offset=0x30000+(address&0x7fff);
  return rom && offset+2<=rom_size ? (unsigned)word(rom+offset) : 0;
}
static unsigned rom_byte(unsigned address) {
  size_t offset=0x30000+(address&0x7fff);
  return rom && offset<rom_size ? rom[offset] : 0;
}
static void player_limits(const uint8_t *r,const MmxCoopState *s,unsigned seat,
                          const MmxCoopViewBoundsState *b,int out[4]) {
  for(unsigned i=0;i<4;++i) out[i]=word(r+0x1e56+i*2);
  if(active && s->initialized && !s->scene_owner && !s->stage_pending &&
      seat<2 && b->stage==r[0x1f7a] && (b->valid&(1u<<seat)))
    for(unsigned i=0;i<4;++i) out[i]=b->limits[seat][i];
}
void MmxCoopViewsPlayerLimits(const uint8_t *r,const MmxCoopState *s,unsigned seat,int out[4]) {
  player_limits(r,s,seat,&bounds,out);
}
void MmxCoopViewsUpdateBounds(const uint8_t *r,const MmxCoopState *s) {
  if(!active || !s->initialized || r[0x1f7a]>12) return;
  unsigned stage=r[0x1f7a];
  if(bounds.stage!=stage) {memset(&bounds,0,sizeof(bounds));bounds.stage=(uint8_t)stage;}
  /* Native scripted rooms and transport still own both cameras. Re-seed
   * after they release their locks; menus preserve the travel history.
   * Vile's arena lock ($83:E842) is script-set, absent from region history. */
  if(s->scene_owner || s->stage_pending || r[0xd3]!=4) {bounds.valid=0;return;}
  if(s->menu_owner || r[0x1f10]>=6) return;
  for(unsigned d=0xe68;d<0x1228;d+=64) if(r[d] &&
      (MmxWidePolicy_IsBossEncounter(r[d+10]) || (stage==9 && (r[d+10]==0x67 || r[d+10]==0x69)) ||
       (stage==1 && r[d+10]==0x21) || (stage==2 && r[d+10]==9))) {
    bounds.valid=0;return;
  }
  /* Thirteen stage lists precede the first rectangle. The final list has
   * no following stage pointer; its end is that first rectangle's address. */
  unsigned data=rom_word(rom_word(0xe4e2));
  unsigned first=rom_word(0xe4e2+stage*2),end=stage<12?rom_word(0xe4e4+stage*2):data;
  if(data<0xe4fc || data>=0xecd0 || first<0xe4fc || end<first ||
      end>data || (end-first)%2) return;
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned seat=0;seat<2;++seat) {
    if(!living(&s->players[seat]) || s->players[seat].zero.swap_phase) {
      bounds.valid&=(uint8_t)~(1u<<seat);continue;
    }
    if(!(bounds.valid&(1u<<seat))) {
      for(unsigned i=0;i<4;++i) bounds.limits[seat][i]=(uint16_t)word(r+targets[i]);
      bounds.valid|=(uint8_t)(1u<<seat);
    }
    const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
    unsigned x=word(body+5),y=word(body+8);
    for(unsigned entry=first;entry<end;entry+=2) {
      unsigned record=rom_word(entry);
      if(record<data || record+8>=0xecd0 || x>=rom_word(record) ||
          x<rom_word(record+2) || y>=rom_word(record+4) || y<rom_word(record+6)) continue;
      /* Same exclusive upper edges and byte command list as F6E0..F71D.
       * Only authored camera limits are copied; no guest script runs twice. */
      for(unsigned n=8;n<40 && record+n<0xecd0;++n) {
        unsigned command=rom_byte(record+n);if(!command) break;
        unsigned address=rom_word(0xecd0+(command-1)*4);
        unsigned value=rom_word(0xecd2+(command-1)*4);
        if(value>8192) continue;
        for(unsigned i=0;i<4;++i) if(address==targets[i]) bounds.limits[seat][i]=(uint16_t)value;
      }
    }
  }
}
/* $00:DE9D/DEBC keep the player inside a native dead zone; DE7E/DE5F
 * limit tracking speed. E1B5..E2BA ease each room bound at $1E52 pixels per
 * frame. Preserve those two phases instead of clamping to the final bound. */
static int follow(int body,int camera,int low,int high,int speed) {
  int delta=body-camera;
  int desired=delta<low?body-low:delta>high?body-high:camera;
  return clamp(desired,camera-speed,camera+speed);
}
static int limit_step(int target,int current,int camera,int body,int previous,
                      int zone,int speed,bool minimum) {
  if(target==current) return current;
  if(minimum ? target>=camera : target<camera)
    return minimum ? (camera+speed<target?camera+speed:target) :
        (camera-speed>=target?camera-speed:target);
  if((uint16_t)(body-camera-zone+2)<4) return target;
  int movement=(int16_t)(body-previous);
  if(minimum) {if(movement>0) movement=0;}
  else if(movement<0) movement=0;
  int next=camera+movement+(minimum?-speed:speed);
  return minimum?(next>=target?next:target):(next<target?next:target);
}
void MmxCoopViewsEndFrame(const uint8_t *r,const MmxCoopState *s) {
  if(!active || !s->initialized || s->scene_owner || s->stage_pending || r[0xd3]!=4) {
    bounds.camera_valid=0;return;
  }
  if(s->menu_owner || r[0x1f10]>=6) return;
  for(unsigned seat=0;seat<2;++seat) {
    unsigned bit=1u<<seat;
    const MmxCoopPlayer *p=&s->players[seat];
    if(!living(p) || p->zero.swap_phase) {bounds.camera_valid&=(uint8_t)~bit;continue;}
    const uint8_t *body=seat==s->current?r+0xba8:p->body;
    /* Shared encounters own the native camera. Retain their actual final
     * position/current bounds so release starts the same pan on both seats. */
    if(seat==s->anchor || !(bounds.valid&bit)) {
      bounds.camera[seat][0]=(uint16_t)word(r+0x1e4d);
      bounds.camera[seat][1]=(uint16_t)word(r+0x1e50);
      for(unsigned i=0;i<4;++i) bounds.current[seat][i]=(uint16_t)word(r+0x1e56+i*2);
      bounds.camera_valid|=(uint8_t)bit;continue;
    }
    int x=word(body+5),y=word(body+8);
    int zones[4]={word(r+0x1e74),word(r+0x1e76),word(r+0x1e70),word(r+0x1e72)};
    if(!(bounds.camera_valid&bit)) {
      for(unsigned i=0;i<4;++i) bounds.current[seat][i]=bounds.limits[seat][i];
      bounds.camera[seat][0]=(uint16_t)clamp(x-zones[0],bounds.current[seat][0],bounds.current[seat][1]);
      bounds.camera[seat][1]=(uint16_t)clamp(y-zones[2],bounds.current[seat][2],bounds.current[seat][3]);
      bounds.camera_valid|=(uint8_t)bit;
    }
    int tracking=word(r+0x1e54),pan=word(r+0x1e52);
    int cx=clamp(follow(x,bounds.camera[seat][0],zones[0],zones[1],tracking),bounds.current[seat][0],bounds.current[seat][1]);
    int cy=clamp(follow(y,bounds.camera[seat][1],zones[2],zones[3],tracking),bounds.current[seat][2],bounds.current[seat][3]);
    bounds.camera[seat][0]=(uint16_t)cx;bounds.camera[seat][1]=(uint16_t)cy;
    for(unsigned i=0;i<4;++i) bounds.current[seat][i]=(uint16_t)limit_step(
        bounds.limits[seat][i],bounds.current[seat][i],i<2?cx:cy,i<2?x:y,
        word(body+(i<2?0x22:0x24)),zones[i],pan,!(i&1));
  }
}
MmxCoopView MmxCoopViewWithBounds(const uint8_t *r,const MmxCoopState *s,unsigned seat,
                                 const MmxCoopViewBoundsState *b) {
  MmxCoopView v={word(r+0x1e4d),word(r+0x1e50),s->anchor};
  if(!active || !s->initialized || seat>1) return v;
  /* A transported partner watches the native script from its first frame,
   * including the teleport beams. Fallen/withdrawn seats watch the survivor. */
  if(s->scene_owner || s->stage_pending || r[0xd3]!=4) return v;
  const MmxCoopPlayer *p=&s->players[seat];
  if(!living(p) || p->zero.swap_phase) seat=s->anchor;
  v.player=seat;
  if(seat==s->anchor) return v;
  if(b->stage==r[0x1f7a] && (b->camera_valid&(1u<<seat))) {
    v.x=b->camera[seat][0];v.y=b->camera[seat][1];return v;
  }
  const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
  /* Use the authored stage/room camera limits, not the other player's view.
   * X1's feet lie in the lower dead zone of the 224px native viewport. */
  int limits[4];player_limits(r,s,seat,b,limits);
  v.x=clamp(word(body+5)-128,limits[0],limits[1]);
  v.y=clamp(word(body+8)-160,limits[2],limits[3]);
  return v;
}
MmxCoopView MmxCoopViewForPlayer(const uint8_t *r,const MmxCoopState *s,unsigned seat) {
  return MmxCoopViewWithBounds(r,s,seat,&bounds);
}
void MmxCoopViewsAdoptPlayer(uint8_t *r,const MmxCoopState *s,unsigned seat) {
  if(!active || !s->initialized || seat>1 || seat==s->anchor || s->scene_owner ||
      s->stage_pending || r[0xd3]!=4 || bounds.stage!=r[0x1f7a] ||
      !(bounds.valid&(1u<<seat)) || !living(&s->players[seat]) ||
      s->players[seat].zero.swap_phase) return;
  MmxCoopView v=MmxCoopViewForPlayer(r,s,seat);
  /* Sigma 1's upper door ($81:EC98) can be reached while the world actor
   * is still at the shaft bottom. Once the door freezes gameplay, native
   * scrolling must start from its entrant's view and room limits; keeping
   * the lower actor's camera leaves the door waiting forever. Copy both
   * current and target limits, plus the camera's previous-position pair,
   * before the caller changes the anchor and begins scene transport. */
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned i=0;i<4;++i) {
    putword(r+0x1e56+i*2,bounds.limits[seat][i]);
    putword(r+targets[i],bounds.limits[seat][i]);
  }
  putword(r+0x1e4d,(unsigned)v.x);putword(r+0x1e6a,(unsigned)v.x);
  putword(r+0x1e50,(unsigned)v.y);putword(r+0x1e6c,(unsigned)v.y);
}
bool MmxCoopViewContains(const uint8_t *r,const MmxCoopState *s,int x,int y,
                         int left,int right,int top,int bottom,int margin) {
  for(unsigned seat=0;seat<2;++seat) {
    if(!living(&s->players[seat]) || s->players[seat].zero.swap_phase) continue;
    if(s->scene_owner && seat!=s->anchor) continue;
    MmxCoopView v=MmxCoopViewForPlayer(r,s,seat);
    int dx=(int16_t)(x-v.x),dy=(int16_t)(y-v.y);
    if(dx>=-left-margin && dx<right+margin && dy>=-top && dy<bottom) return true;
  }
  return false;
}
