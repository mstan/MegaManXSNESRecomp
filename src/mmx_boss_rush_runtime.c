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
  if(at==0x00e68e && s.mode==MMX_RUSH_LOADING && s.stage_started && !s.return_title) {
    /* Stage initialization clears the checkpoint after the title callback.
     * Select it when the native checkpoint table is actually read. */
    g_ram[0x1f81]=2;g_ram[0x1f82]=0;
  }
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
    /* Chill Penguin's third checkpoint loads the enclosed boss-room art. */
    g_ram[0x1f7a]=8;g_ram[0x1f81]=2;g_ram[0x1f82]=0;
    equipment(g_ram);s.stage_started=1;MmxBossRushSetState(&s);
  }
  if(at==0x00dc36 && (playing() || s.mode==MMX_RUSH_PREPARING)) {
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xdcda);return;
  }
  if(!playing()) return;
  if(at==0x009ac7) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9ad9);return;}
  if(owner<0) return;
  if(at==0x048fca) {
    /* Boss art is decoded privately per pose. Its native DMA would read the
     * Penguin stage's staging buffer and overwrite X/weapon/other boss CHR. */
    g_ram[cpu->D+0x17]&=127;
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9085);return;
  }
  if(at==0x048fad) {
    /* Keep palette-controller timing, but do not replace shared CGRAM. This
     * point precedes PHD; $8FC7 unwinds the existing DB/status frames. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x8fc7);return;
  }
  if(s.bosses[owner].id==7 && s.bosses[owner].phase==MMX_RUSH_ENTERING) {
    if(at==0x079258) {
      put(g_ram+cpu->D+5,s.camera_x+144);put(g_ram+cpu->D+8,s.camera_y+160);
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
    /* Observe the native death entry itself. A weakness reaction can advance
     * through combat/hurt/death between the host's end-of-frame observations. */
    if(s.bosses[owner].object==cpu->D && s.bosses[owner].phase!=MMX_RUSH_DYING) {
      s.bosses[owner].phase=MMX_RUSH_FIGHTING;s.bosses[owner].health=0;
      s.bosses[owner].death_x=(int16_t)(word(g_ram+cpu->D+5)-s.camera_x);
      s.bosses[owner].death_y=(int16_t)(word(g_ram+cpu->D+8)-s.camera_y);
      MmxBossRushSetState(&s);MmxBossRushDefeat((unsigned)owner);
    }
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
  const unsigned bosses[]={0x849feb,0x84a003,0x84aadd,0x84a677,0x849b03,0x849b43,
    0x848fca,0x848fad,0x9ac7,0xdc36,0x94d9,0xe68e,0x879258,0x879276};
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
  uint16_t held=MmxBossRushGetState().input;
  MmxBossRushStart(coop,0x4d4d5852u);MmxCoopReset();
  MmxBossRushState s=MmxBossRushGetState();s.input=held;
  s.menu_input=held&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y);
  MmxBossRushSetState(&s);
  if(title(r)) {
    /* Enter the native confirmation/fade state without its buster script
     * ($92B0). Keep the fourth row visible until the title fades out. */
    MmxBossRushState s=MmxBossRushGetState();s.menu=1;s.selection=3;
    MmxBossRushSetState(&s);
    r[0x3c]=0;r[0x39]=4;r[0x3b]=0;r[0xc01]=0;
    memset(r+0xac,0,4);put(r+0xbb0,214);
  }
}
uint16_t MmxBossRushFilterInput(uint16_t input) {
  MmxBossRushState s=MmxBossRushGetState();
  if(s.mode!=MMX_RUSH_OFF && s.mode!=MMX_RUSH_FINISHED && s.menu_input) {
    /* Confirmation is consumed until released, including a long hold across
     * the fade/arrival. Store the release gate in snapshot-owned state. */
    s.menu_input&=input;MmxBossRushSetState(&s);input&=~s.menu_input;
  }
  return input;
}
static bool prepare(uint8_t *r) {
  if(!g_snes || !g_snes->cart) return false;
  /* Checkpoint 2 already spawns at $1D80,$0100 between the two doors.
   * Preserve its native arrival, room map, collision and camera. */
  MmxBossRushState s=MmxBossRushGetState();
  s.camera_x=0x1e00;s.camera_y=0x100;s.loaded=1;s.mode=MMX_RUSH_PREPARING;s.menu=0;
  /* The stage scanner normally supplies the boss-door marker. Rush skips
   * its stock encounter, but the native door still needs it for boss music. */
  r[0x1f26]=0xff;r[0x1fa0]=0;
  equipment(r);
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
      c.players[i].zero.swap_phase=0;put(c.players[i].body+5,word(r+0xbad)-16+i*32);
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
  MmxBossRushState s=MmxBossRushGetState();
  put(r+0x1e4d,s.camera_x);put(r+0x1e50,s.camera_y);
  put(r+0x1e56,s.camera_x);put(r+0x1e58,s.camera_x);
  put(r+0x1e5a,s.camera_y);put(r+0x1e5c,s.camera_y);
  put(r+0x1e5e,s.camera_x);put(r+0x1e60,s.camera_x);put(r+0x1e62,s.camera_y);
}
void MmxBossRushFrame(uint8_t *r,uint16_t input) {
#if MMX_VARIANT_JP
  (void)r;(void)input;return;
#else
  MmxBossRushState s=MmxBossRushGetState();unsigned pressed=input&~s.input;s.input=input;
  if(s.mode==MMX_RUSH_OFF) {
    if(!title(r)) {s.menu=0;s.menu_input=input;MmxBossRushSetState(&s);return;}
    /* Old playtest snapshots may contain the removed mode submenu. */
    if(s.menu==2) s.selection=3;
    s.menu=1;
    if(s.selection!=3) s.selection=(uint8_t)((word(r+0xbb0)-166)/16);
    if(s.selection==2 && (pressed&SNES_PAD_DOWN)) s.selection=3;
    if(s.selection==3) {
      memset(r+0xac,0,4);put(r+0xbb0,214);
      if(pressed&SNES_PAD_UP) {s.selection=2;put(r+0xbb0,198);}
      else if(pressed&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y)) {
        MmxBossRushSetState(&s);load(r,MmxCoopEnabled());return;
      }
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
        r[0xbaa]!=14 && !r[0xc0c] && !r[0xc16]) prepare(r);
    return;
  }
  if(s.mode==MMX_RUSH_PREPARING) return;
  camera(r);
  if(r[0xd3]==4 && !r[0x1f19] && !MmxWeaponsMenuVisible(r) && !MmxCoopGetState().menu_owner) {
    for(unsigned i=0;i<2;++i) {
      s=MmxBossRushGetState();MmxBossRushBoss *b=&s.bosses[i];
      if(b->phase==MMX_RUSH_DYING) {
        if(++b->ticks>=48) {
          for(unsigned d=0xe68;d<0x1d08;) {
            if(d==0x1228) {d=0x1428;continue;}
            unsigned n=(d-0xe68)/32;
            if(s.owner[n]==i+1) {memset(r+d,0,object_step(d));s.owner[n]=0;}
            d+=object_step(d);
          }
          memset(b,0,sizeof(*b));
        }
        /* Persist every tick, including frames before the cleanup threshold. */
        MmxBossRushSetState(&s);
      }
      if(!MmxBossRushGetState().bosses[i].phase) {
        unsigned object=0;
        for(unsigned d=0xe68;d<0x1228;d+=64) if(!r[d]) {object=d;break;}
        if(!object) continue;
        int id=MmxBossRushNextBoss();if(id<0) continue;
        if(!MmxBossRushAssign(i,(unsigned)id,(uint16_t)object)) continue;
        s=MmxBossRushGetState();
        for(unsigned n=0;n<sizeof(globals)/sizeof(*globals);++n)
          s.bosses[i].globals[n]=r[globals[n]];
        MmxBossRushSetState(&s);
        memset(r+object,0,64);r[object]=0x81;r[object+10]=kMmxBossRushBosses[id].kind;
        unsigned x=s.camera_x+144+i*48,y=s.camera_y+(id==4?43:96);
        put(r+object+5,x);put(r+object+8,y);put(r+object+0x22,x);put(r+object+0x24,y);
      }
    }
  }
#endif
}
void MmxBossRushAfterFrame(uint8_t *r) {
  if(MmxBossRushGetState().mode==MMX_RUSH_PREPARING) {
    /* The second native door owns its scroll, close animation and boss
     * music. Begin after it releases the player's door action. */
    if(word(r+0x1e4d)==0x1e00 && word(r+0xbad)>=0x1e20 &&
        r[0xbaa]!=0x1a && r[0xbaa]!=0x18 && !r[0xc16]) {
      MmxBossRushState s=MmxBossRushGetState();s.mode=MMX_RUSH_PLAYING;
      MmxBossRushSetState(&s);
    }
    return;
  }
  if(!playing()) return;
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned i=0;i<2;++i) {
    MmxBossRushBoss *b=&s.bosses[i];if(!b->phase) continue;
    unsigned native=r[b->object+1],hp=r[b->object+0x27]&127;
    /* State 4 is ordinary combat, 8 is a hit reaction and 6 is death.
     * Watching only state 4 misses a boss hit as its entrance completes. */
    if(b->phase==MMX_RUSH_ENTERING && native>=4) b->phase=MMX_RUSH_FIGHTING;
    if(b->phase==MMX_RUSH_DYING || (b->phase==MMX_RUSH_FIGHTING &&
        (native==6 || !r[b->object]))) hp=0;
    b->health=(uint8_t)(hp>32?32:hp);
    if(b->phase==MMX_RUSH_FIGHTING && !b->health) {
      b->death_x=(int16_t)(word(r+b->object+5)-s.camera_x);
      b->death_y=(int16_t)(word(r+b->object+8)-s.camera_y);
    }
  }
  MmxBossRushSetState(&s);
  for(unsigned i=0;i<2;++i) if(s.bosses[i].phase==MMX_RUSH_FIGHTING && !s.bosses[i].health)
    MmxBossRushDefeat(i);
  bool alive=(r[0xbcf]&127)!=0;
  bool death_complete=r[0xbaa]==12 && r[0xbab]>=6;
  if(s.coop) {
    MmxCoopCapture(r);MmxCoopState c=MmxCoopGetState();alive=false;
    death_complete=true;
    for(unsigned i=0;i<2;++i) {
      const MmxCoopPlayer *p=&c.players[i];
      alive|=p->status==MMX_COOP_ALIVE && (p->body[0x27]&127);
      death_complete&=p->status!=MMX_COOP_ALIVE || (p->body[2]==12 && p->body[3]>=6);
    }
  }
  if(!alive && death_complete) MmxBossRushFinish();
  camera(r);put(r+0x1f0e,0);
}
