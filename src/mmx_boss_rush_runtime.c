#include "mmx_boss_rush.h"
#include "mmx_coop.h"
#include "mmx_renderer.h"
#include "mmx_render_assets.h"
#include "mmx_rtl.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "snes_overlay_draw.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include <string.h>
#include <stdio.h>

extern uint8_t g_ram[0x20000];
static unsigned word(const uint8_t *p) {return p[0]|p[1]<<8;}
static void put(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
/* These are scene/camera flags, not inventory or player combat state. Native
 * entrances may borrow them during their own update, never across actors. */
static const uint16_t globals[]={0x1f0c,0x1f0e,0x1f0f,0x1f13,0x1f14,0x1f15,
  0x1f16,0x1f17,0x1f18,0x1f1a,0x1f23,0x1f26,0x1f31,0x1f3b,0xc16,
  0x1e5c,0x1e5d,0x1e5e,0x1e5f,0x1e60,0x1e61,0x1e62,0x1e63,
  0xc0,0xc1,0xc9,0xca,0xcb,0xcc,0xcd,
  0xc0c,0xbd8,0x1e54,0x1e55,0x1e74,0x1e75,0x1e76,0x1e77};
static bool playing(void) {return MmxBossRushGetState().mode==MMX_RUSH_PLAYING;}
static void equipment(uint8_t *r);
static unsigned object_step(unsigned d) {return d<0x1628?64:d<0x1928?48:32;}
static void remember_objects(MmxBossRushState *s) {
  for(unsigned d=0xe68;d<0x1d08;) {
    if(d==0x1228) {d=0x1428;continue;}
    s->occupied[(d-0xe68)/32]=g_ram[d]!=0;d+=object_step(d);
  }
}
static void actor_hook(CpuState *cpu,uint32_t pc) {
  if(!playing()) return;
  MmxBossRushState s=MmxBossRushGetState();unsigned at=pc&65535;
  bool returning=at==0xd4f9 || at==0xd522 || at==0xd49c || at==0xd4c5 || at==0xd35c;
  if(returning) {
    if(!s.actor || s.actor_object!=cpu->D || s.actor_s!=cpu->S) return;
    unsigned owner=s.actor-1;
    for(unsigned d=0xe68;d<0x1d08;) {
      if(d==0x1228) {d=0x1428;continue;}
      unsigned n=(d-0xe68)/32;
      if(!g_ram[d]) s.owner[n]=0;
      else if(!s.occupied[n]) s.owner[n]=(uint8_t)(owner+1);
      d+=object_step(d);
    }
    for(unsigned i=0;i<sizeof(globals)/sizeof(*globals);++i) {
      s.bosses[owner].globals[i]=g_ram[globals[i]];g_ram[globals[i]]=s.globals[i];
    }
    s.actor=0;MmxBossRushSetState(&s);return;
  }
  int owner=MmxBossRushOwner(cpu->D);
  if(owner<0 || s.actor) return;
  if(s.bosses[owner].phase==MMX_RUSH_DYING && s.bosses[owner].object==cpu->D) {
    unsigned end=at==0xd4f6?0xd4f9:0xd522;
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|end);return;
  }
  s.actor=(uint8_t)(owner+1);s.actor_object=cpu->D;s.actor_s=cpu->S;
  for(unsigned i=0;i<sizeof(globals)/sizeof(*globals);++i) {
    s.globals[i]=g_ram[globals[i]];g_ram[globals[i]]=s.bosses[owner].globals[i];
  }
  remember_objects(&s);
  put(g_ram+0x1f0e,s.bosses[owner].object);g_ram[0x1f26]=0;
  MmxBossRushSetState(&s);
}
static void boss_hook(CpuState *cpu,uint32_t pc) {
  MmxBossRushState s=MmxBossRushGetState();int owner=MmxBossRushOwner(cpu->D);
  unsigned at=pc&0x7fffff;
  if(at==0x0094d9 && s.return_title) {
    MmxBossRushReset();MmxCoopReset();
    memset(g_ram+0xd1,0,4);
    memset(g_ram+0x38,0,5);g_ram[0x38]=2;
    /* Resume the title loop with its menu state selected, bypassing the
     * startup attract screen that waits for an extra Start press. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x8c56);return;
  }
  if(at==0x0094d9 && s.mode==MMX_RUSH_LOADING && !s.stage_started) {
    g_ram[0xd1]=2;g_ram[0xd2]=2;g_ram[0xd3]=g_ram[0xd4]=0;
    g_ram[0x1f7a]=11;g_ram[0x1f81]=g_ram[0x1f82]=0;
    equipment(g_ram);s.stage_started=1;MmxBossRushSetState(&s);
  }
  if(!playing()) return;
  if(at==0x009ac7) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9ad9);return;}
  /* Stage event scans and camera follow cannot introduce an unrelated actor
   * or move this room. Their entry/return frames remain native and balanced. */
  if(at==0x00dc36) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xdcda);return;}
  if(owner<0) return;
  if(s.bosses[owner].id==7 && s.bosses[owner].phase==MMX_RUSH_ENTERING) {
    if(at==0x079258) {
      put(g_ram+cpu->D+5,144);put(g_ram+cpu->D+8,160);
    }
    /* The factory waits for a player to walk under the entrance platform.
     * In this fixed room the arrival starts independently of that distance. */
    if(at==0x079276) {
      interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9281);return;
    }
  }
  if(at==0x049feb && s.bosses[owner].phase==MMX_RUSH_ENTERING) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9ff6);return;}
  if(at==0x04a003 && s.bosses[owner].phase==MMX_RUSH_ENTERING) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xa00c);return;}
  if(at==0x04aadd) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xaaf3);return;}
  if(at==0x04a677) {
    /* Retail death starts a global freeze, palette flash and victory task.
     * Rush owns its explosion interval; the boss's eventual removal is local. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xa6d2);return;
  }
  if((at==0x049b03 || at==0x049b43) && s.bosses[owner].phase!=MMX_RUSH_FIGHTING)
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9b79);
}
void MmxBossRushHostFrame(void) {
#if !MMX_VARIANT_JP
  bool registered=true;
  /* Idempotent registration: mod activation can replace shared hooks. Add
   * ours after its setup so co-op contact replay stays authoritative. */
  const unsigned actors[]={0xd4f6,0xd4f9,0xd515,0xd522,0xd499,0xd49c,0xd4b8,0xd4c5,0xd359,0xd35c};
  for(unsigned i=0;i<sizeof(actors)/sizeof(*actors);++i)
    registered&=interp_bridge_add_pre_opcode_hook(actors[i],actor_hook);
  const unsigned bosses[]={0x849feb,0x84a003,0x84aadd,0x84a677,0x849b03,0x849b43,0x9ac7,0xdc36,0x94d9,0x879258,0x879276};
  for(unsigned i=0;i<sizeof(bosses)/sizeof(*bosses);++i)
    registered&=interp_bridge_add_pre_opcode_hook(bosses[i],boss_hook);
  static bool warned;
  if(!registered && !warned) {warned=true;fprintf(stderr,"[mmx-rush] native hook registration failed: interpreter hook table is full\n");}
#endif
}
static bool title(const uint8_t *r) {
  unsigned y=word(r+0xbb0);
  return r[0xd1]==0 && r[0xba9]==2 && word(r+0xbad)==32 && y>=166 && y<=214;
}
static void equipment(uint8_t *r) {
  r[0x1f99]=0xff;r[0x1f9a]=32;r[0x1f98]=0x40;r[0xbcf]=0xa0;
  r[0x1f9c]=0xff;r[0x1f7e]&=0x7f; /* All hearts; Hadouken remains unacquired. */
  for(unsigned i=0;i<4;++i) r[0x1f83+i]=0x9c;
  for(unsigned i=0;i<8;++i) {r[0x1f87+i*2]=0;r[0x1f88+i*2]=0xdc;}
  MmxZeroHealthRespawn(r);
  r[0x1f80]=0; /* No spare-life re-entry. */
}
static void load(uint8_t *r,bool coop) {
  MmxBossRushStart(coop,0x4d4d5852u);MmxCoopReset();
  if(title(r)) {
    /* Let the native title selection/fade finish. The mode loop at $94D9
     * launches the arena only after that coroutine has returned. */
    r[0x3c]=0;put(r+0xbb0,166);r[0xac]=0x10;
  }
}
static bool arena(uint8_t *r) {
  if(!g_snes || !g_snes->cart) return false;
  const uint8_t *rom=g_snes->cart->rom;size_t size=g_snes->cart->romSize;
  unsigned table=word(r+0xb92)|(r[0xb94]<<16);
  int empty=-1,solid=-1;
  /* Select actual loaded metatiles, preserving the native collision and
   * background formats. No binary ROM or editor-generated ROM is shipped. */
  for(unsigned p=0x2000;p<0xe000;p+=2) {
    unsigned tile=word(r+p),address=table+tile;
    if((address&65535)<0x8000) continue;
    size_t off=((address>>16)&127)*32768+(address&32767);
    if(off>=size) continue;
    unsigned type=rom[off]&63;
    if(!type && empty<0) empty=(int)tile;
    if((type==0x13 || type==0x3b) && solid<0) solid=(int)tile;
  }
  if(empty<0 || solid<0) return false;
  MmxBossRushState s=MmxBossRushGetState();s.empty_tile=(uint16_t)empty;s.solid_tile=(uint16_t)solid;
  s.camera_x=s.camera_y=0;s.loaded=1;s.mode=MMX_RUSH_PLAYING;
  memset(r+0xe800,0,1024);
  for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x)
    put(r+0x2000+y*32+x*2,(y==0 || y>=12 || x==0 || x==15)?solid:empty);
  memset(r+0xe18,0,0x1d08-0xe18);memset(r+0x1f0c,0,0x40);
  put(r+0xbad,80);put(r+0xbb0,176);put(r+0xbca,80);put(r+0xbcc,176);
  r[0xba9]=2;r[0xbaa]=r[0xbab]=r[0xc16]=0;
  r[0xbac]=r[0xbaf]=r[0xc0c]=0;put(r+0xbc2,0);put(r+0xbc4,0);
  r[0xbd3]=4;
  equipment(r);
  /* Populate the native animation -> tile/palette bindings as well as the
   * compositor's private art. The arena's stage only loads half the refights. */
  for(unsigned kind=1;kind<=0x68;++kind) {
    size_t p=0x325e4+(kind-1)*2;if(p+2>size) break;
    unsigned stage=11;
    for(unsigned id=0;id<8;++id) if(kMmxBossRushBosses[id].kind==kind) stage=kMmxBossRushBosses[id].stage;
    const MmxSpriteAsset *a=MmxRenderAssetsRushSprite(stage,rom[p]);
    if(a) {r[0x18200+rom[p+1]]=a->tile_base;r[0x18300+rom[p+1]]=a->attributes;}
  }
  MmxBossRushSetState(&s);
  if(MmxCoopEnabled()) {
    MmxCoopInitialize(r);MmxCoopCapture(r);MmxCoopState c=MmxCoopGetState();
    c.stage_pending=0;c.scene_owner=c.scene_phase=0;c.enrolled=1;
    for(unsigned i=0;i<2;++i) {
      memcpy(c.players[i].body,r+0xba8,sizeof(c.players[i].body));
      c.players[i].status=MMX_COOP_ALIVE;
      c.players[i].body[0x27]=0xa0;c.players[i].body[2]=c.players[i].body[3]=0;
      c.players[i].zero.hp[0]=c.players[i].zero.hp[1]=32;
      c.players[i].zero.hp_max=32;c.players[i].zero.hp_valid=1;
      c.players[i].zero.swap_phase=0;put(c.players[i].body+5,64+i*32);put(c.players[i].body+8,176);
      for(unsigned n=1;n<16;n+=2) c.players[i].energy[n]=28;
    }
    /* Select captures the outgoing body before projecting another seat.
     * Keep the currently projected body in sync with the new run as well. */
    memcpy(r+0xba8,c.players[c.current].body,sizeof(c.players[c.current].body));
    MmxCoopSetState(&c);MmxCoopSelect(r,c.anchor);
  }
  return true;
}
static void camera(uint8_t *r) {
  put(r+0x1e4d,0);put(r+0x1e50,0);
  for(unsigned a=0x1e56;a<=0x1e62;a+=2) put(r+a,0);
}
void MmxBossRushFrame(uint8_t *r,uint16_t input) {
#if MMX_VARIANT_JP
  (void)r;(void)input;return;
#else
  MmxBossRushState s=MmxBossRushGetState();unsigned pressed=input&~s.input;s.input=input;
  if(s.mode==MMX_RUSH_OFF) {
    if(!title(r)) {s.menu=0;s.menu_input=input;MmxBossRushSetState(&s);return;}
    if(s.menu==2) {
      memset(r+0xac,0,4);
      if(pressed&(SNES_PAD_UP|SNES_PAD_DOWN)) s.selection^=1;
      if(pressed&SNES_PAD_B) {s.menu=1;s.selection=3;}
      else if(pressed&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y)) {
        if(!s.selection || MmxCoopEnabled()) {
          MmxBossRushSetState(&s);load(r,s.selection!=0);return;
        }
      }
      MmxBossRushSetState(&s);return;
    }
    s.menu=1;
    if(s.selection!=3) s.selection=(uint8_t)((word(r+0xbb0)-166)/16);
    if(s.selection==2 && (pressed&SNES_PAD_DOWN)) s.selection=3;
    if(s.selection==3) {
      memset(r+0xac,0,4);put(r+0xbb0,214);
      if(pressed&SNES_PAD_UP) {s.selection=2;put(r+0xbb0,198);}
      else if(pressed&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y)) {s.menu=2;s.selection=0;}
    }
    MmxBossRushSetState(&s);return;
  }
  if(s.mode==MMX_RUSH_FINISHED) {
    if(pressed&(SNES_PAD_UP|SNES_PAD_DOWN)) s.result_selection^=1;
    MmxBossRushSetState(&s);
    if(pressed&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y)) {
      if(!s.result_selection) load(r,s.coop!=0);
      else {
        s.mode=MMX_RUSH_LOADING;s.return_title=1;s.input=input;
        MmxBossRushSetState(&s);
      }
    }
    return;
  }
  MmxBossRushSetState(&s);
  if(s.mode==MMX_RUSH_LOADING) {
    if(r[0xd1]==2 && r[0xd2]==4 && r[0xd3]==4 && r[0xba9]==2 &&
        r[0xbaa]!=14 && !r[0xc0c] && !r[0xc16]) arena(r);
    return;
  }
  camera(r);
  if(r[0xd3]==4 && !r[0x1f19] && !MmxWeaponsMenuVisible(r) && !MmxCoopGetState().menu_owner) {
    for(unsigned i=0;i<2;++i) {
      s=MmxBossRushGetState();MmxBossRushBoss *b=&s.bosses[i];
      if(b->phase==MMX_RUSH_DYING && ++b->ticks>=48) {
        for(unsigned d=0xe68;d<0x1d08;) {
          if(d==0x1228) {d=0x1428;continue;}
          unsigned n=(d-0xe68)/32;
          if(s.owner[n]==i+1) {memset(r+d,0,object_step(d));s.owner[n]=0;}
          d+=object_step(d);
        }
        memset(b,0,sizeof(*b));MmxBossRushSetState(&s);
      }
      if(!MmxBossRushGetState().bosses[i].phase) {
        unsigned object=0;
        for(unsigned d=0xe68;d<0x1228;d+=64) if(!r[d]) {object=d;break;}
        if(!object) continue;
        int id=MmxBossRushNextBoss();if(id<0) continue;
        if(!MmxBossRushAssign(i,(unsigned)id,(uint16_t)object)) continue;
        memset(r+object,0,64);r[object]=0x81;r[object+10]=kMmxBossRushBosses[id].kind;
        unsigned y=id==4?43:96; /* Kuwanger's entrance traverses 128 px down. */
        put(r+object+5,144+i*48);put(r+object+8,y);put(r+object+0x22,144+i*48);put(r+object+0x24,y);
      }
    }
  }
#endif
}
void MmxBossRushAfterFrame(uint8_t *r) {
  if(!playing()) return;
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned i=0;i<2;++i) {
    MmxBossRushBoss *b=&s.bosses[i];if(!b->phase) continue;
    b->health=r[b->object+0x27]&127;
    if(b->phase==MMX_RUSH_FIGHTING && !b->health) {
      b->death_x=(int16_t)word(r+b->object+5);b->death_y=(int16_t)word(r+b->object+8);
    }
    if(b->phase==MMX_RUSH_ENTERING && r[b->object+1]==4 && b->health)
      b->phase=MMX_RUSH_FIGHTING;
  }
  MmxBossRushSetState(&s);
  for(unsigned i=0;i<2;++i) if(s.bosses[i].phase==MMX_RUSH_FIGHTING && !s.bosses[i].health)
    MmxBossRushDefeat(i);
  bool alive=(r[0xbcf]&127)!=0;
  if(s.coop) {
    MmxCoopCapture(r);MmxCoopState c=MmxCoopGetState();alive=false;
    for(unsigned i=0;i<2;++i) alive|=c.players[i].status==MMX_COOP_ALIVE && (c.players[i].body[0x27]&127);
  }
  if(!alive) MmxBossRushFinish();
  camera(r);put(r+0x1f0e,0);
}
