#include "mmx_vs.h"
#include "mmx_boss_rush.h"
#include "mmx_boss_rush_audio.h"
#include "mmx_coop.h"
#include "mmx_rtl.h"
#include "mmx_replay.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t g_ram[0x20000];
static unsigned word(const uint8_t *r) {return r[0]|r[1]<<8;}
static void put(uint8_t *r,unsigned v) {r[0]=(uint8_t)v;r[1]=(uint8_t)(v>>8);}
static unsigned step(unsigned d) {return d<0x1628?64:d<0x1928?48:32;}
/* Complete native checkpoint/door/menu and entrance work within one host tick.
 * This runs before RtlRunFrame installs its CPU/watchdog frame. Recursive
 * frames keep their guest clocks and raster IRQs, but never record another
 * host input or expose their PCM. The state, not wall time, ends the batch. */
static bool setup_batch;
static bool setup_pending(void) {
  MmxVsState s=MmxVsGetState();
  return (s.phase==MMX_VS_SELECT && !s.menu_ready && MmxBossRushGetState().stage_started) ||
      s.phase==MMX_VS_LOADING || s.phase==MMX_VS_ENTRANCE;
}
bool MmxVsFrameBegin(uint32_t *inputs) {
  if(setup_batch)return true;
  if(!MmxReplayBegin(inputs))return false;
  if(!setup_pending())return true;
  setup_batch=true;
  RtlApuLock();
  uint32_t audio=RtlAudioProducerCursor();
  unsigned n=0;
  for(;n<2400 && setup_pending();++n) {
    MmxVsRunSetupFrame(*inputs&0xc0000000u);
    RtlAudioRewindProducer(audio);
  }
  MmxBossRushAudioReset();
  RtlApuUnlock();setup_batch=false;
  MmxReplayInternalFramesComplete();
  MmxVsState s=MmxVsGetState();
  s.release[0]|=*inputs&4095;s.release[1]|=(*inputs>>12)&4095;MmxVsSetState(&s);
  if(n==2400)fprintf(stderr,"[mmx-vs] setup timeout phase=%u menu=%u:%u native=%u:%u c3=%u rush=%u\n",
      s.phase,s.menu_ready,s.menu_opening,g_ram[0x1ec9],g_ram[0x1f10],g_ram[0xc3],MmxBossRushGetState().mode);
  return true;
}
void MmxVsFrameEnd(void) {if(!setup_batch)MmxReplayEnd();}
/* Verified USA combat JSR dispatches. Native init, animation and child updates
 * execute normally; only autonomous choice is replaced by seat input. */
static const uint32_t dispatches[8]={0x81b638,0x889d78,0x83b2ea,0x81c540,
  0x878c20,0x888710,0x87d9a7,0x879309};
static const uint8_t actions[8][4]={
  {0,4,8,6}, {0,4,2,6}, {2,0,4,0}, {4,6,2,0},
  {2,6,4,0}, {8,6,2,0}, {4,2,6,0}, {4,2,0,0}
};
static bool body(unsigned seat,unsigned d) {return MmxBossRushGetState().bosses[seat].object==d;}
static void call_long(CpuState *cpu,uint32_t pc,uint32_t target) {
  cpu_write8(cpu,0,cpu->S--,(uint8_t)(pc>>16));
  cpu_write8(cpu,0,cpu->S--,(uint8_t)((pc+2)>>8));
  cpu_write8(cpu,0,cpu->S--,(uint8_t)(pc+2));
  interp_bridge_pre_opcode_redirect(target);
}
static void call_short(CpuState *cpu,uint32_t pc,uint32_t target) {
  cpu_write8(cpu,0,cpu->S--,(uint8_t)((pc+2)>>8));
  cpu_write8(cpu,0,cpu->S--,(uint8_t)(pc+2));
  interp_bridge_pre_opcode_redirect(target);
}
static void clear_round(uint8_t *r) {
  MmxBossRushState rush=MmxBossRushGetState();
  for(unsigned d=0xe68;d<0x1d08;) {
    unsigned n=(d-0xe68)/32;
    if(rush.owner[n])memset(r+d,0,step(d));
    d+=step(d);
  }
  memset(r+0x1228,0,0x200);memset(rush.bosses,0,sizeof(rush.bosses));
  memset(rush.owner,0,sizeof(rush.owner));memset(rush.occupied,0,sizeof(rush.occupied));
  rush.actor=0;MmxBossRushSetState(&rush);MmxBossRushAudioReset();
  MmxBossRushVisualState visual={0};MmxBossRushVisualSetState(&visual);
}
void MmxVsPrepareArena(uint8_t *r) {
  r[0xbcf]=0xa0;
  for(unsigned i=0;i<4;++i)r[0x1f83+i]=0;
}
void MmxVsSpawned(uint8_t *r,unsigned seat) {
  MmxVsState s=MmxVsGetState();unsigned d=MmxBossRushGetState().bosses[seat].object;
  r[d+0x27]=0xa0;s.fighters[seat].hp=32;MmxVsSetState(&s);
}
void MmxVsChildAllocated(const uint8_t *r,unsigned d,unsigned parent,unsigned seat) {
  MmxVsState s=MmxVsGetState();MmxVsFighter *f=&s.fighters[seat];unsigned cls=0;
  if(body(seat,parent)) {
    if(f->action!=MMX_VS_IDLE && f->action!=MMX_VS_JUMP && f->action!=MMX_VS_CEILING) {
      cls=MmxVsAttackClass(f->boss,f->action,true);
      if(f->boss==2 || (f->boss==6 && f->action!=MMX_VS_PRIMARY) ||
          (f->boss==5 && f->action!=MMX_VS_PRIMARY) || (f->boss==3 && f->action==MMX_VS_ABILITY))cls=1;
      if(f->boss==7 && f->action!=MMX_VS_PRIMARY)cls=0;
    }
  } else if(parent>=0xe68 && parent<0x1d08)cls=s.attack_class[(parent-0xe68)/32];
  if((f->boss==0 && d>=0x1628 && r[d+10]==0x12) ||
      (f->boss==7 && r[d+10]==0x0e))cls=0;
  s.attack_class[(d-0xe68)/32]=(uint8_t)cls;MmxVsSetState(&s);
}
/* Convert the configured native shoot/jump/dash bindings to VS actions.
 * Additional X special remains separately bindable through host Controls. */
static unsigned mapped(const uint8_t *r,unsigned input) {
  unsigned native=0;for(unsigned b=0;b<12;++b)if(input&(1u<<b))native|=0x8000u>>b;
  unsigned buttons=((native&255)>>2&0x3c)|(native>>8&0xc0)|(native>>12&3);
  unsigned out=input&~(SNES_PAD_Y|SNES_PAD_B|SNES_PAD_A);
  static const unsigned bits[]={SNES_PAD_Y,SNES_PAD_B,SNES_PAD_A};
  for(unsigned i=0;i<3;++i) {unsigned mask=r[0xffc0+i];if(mask && (buttons&mask)==mask)out|=bits[i];}
  return out;
}
bool MmxVsFrame(uint8_t *r,uint16_t p1,uint16_t p2) {
  if(!MmxVsActive())return false;
  MmxVsState s=MmxVsGetState();unsigned in[2]={p1&4095,p2&4095},press[2];
  for(unsigned i=0;i<2;++i) {
    s.release[i]&=in[i];press[i]=in[i]&~s.input[i]&~s.release[i];s.input[i]=(uint16_t)in[i];
  }
  if(s.exit_requested) {
    /* Drain a suspended native actor before releasing its private slots. */
    if(s.projected || MmxBossRushGetState().actor)return false;
    clear_round(r);MmxVsReset();MmxBossRushState rush=MmxBossRushGetState();
    rush.mode=MMX_RUSH_LOADING;rush.return_title=1;MmxBossRushSetState(&rush);return false;
  }
  if(s.phase==MMX_VS_SELECT) {
    MmxBossRushState rush=MmxBossRushGetState();
    if(rush.mode!=MMX_RUSH_PLAYING) {MmxVsSetState(&s);return false;}
    if(!s.menu_ready) {
      /* Invoke the retail Start-menu task; all of its initialization,
       * camera save/restore, native tiles, cursor and preview still run. */
      if(!s.menu_opening) {
        put(r+0xac,0x1000);put(r+0xae,0x1000);s.menu_opening=1;
      }
      if(r[0x1ec9]==4 && r[0x1f10]==6 && (r[0xc3]&128)) {
        s.menu_ready=1;
        r[0x1ed2]=(uint8_t)MmxVsWeaponSlot(s.cursor[s.picker]);
      }
      MmxVsSetState(&s);return false;
    }
    unsigned seat=s.picker,p=press[seat];
    unsigned slot=MmxVsWeaponSlot(s.cursor[seat]);
    if(p&SNES_PAD_UP)slot=slot==1?4:slot==5?8:slot-1;
    else if(p&SNES_PAD_DOWN)slot=slot==4?1:slot==8?5:slot+1;
    else if(p&(SNES_PAD_LEFT|SNES_PAD_RIGHT))slot=slot<5?(slot==4?8:slot+5):(slot==5?1:slot-5);
    s.cursor[seat]=(uint8_t)MmxVsSlotBoss(slot);
    MmxVsSetState(&s);
    if(p&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y)) {
      MmxVsChoose(seat,s.cursor[seat]);s=MmxVsGetState();
      s.release[seat]=s.input[seat];MmxVsSetState(&s);
    }
    return false;
  }
  if(s.phase==MMX_VS_RESULT) {
    if(s.ticks<120)++s.ticks;
    if(s.ticks>=48 && ((press[0]|press[1])&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y))) {
      s.advance=1;
    }
    MmxVsSetState(&s);
    /* A deadline can suspend an actor inside a native attack. Let it return
     * before reusing its slots for the next roster, otherwise its old
     * continuation could spawn children owned by the new fighter. */
    if(s.ticks<48 || s.projected || MmxBossRushGetState().actor)return false;
    if(s.advance) {clear_round(r);MmxVsNextRound();}
    return true;
  }
  if(s.phase==MMX_VS_FIGHT && ((press[0]|press[1])&SNES_PAD_START)) {
    s.paused^=1;s.release[0]=s.input[0];s.release[1]=s.input[1];
  }
  if(s.paused) {
    if((press[0]|press[1])&(SNES_PAD_UP|SNES_PAD_DOWN))s.pause_choice^=1;
    if((press[0]|press[1])&(SNES_PAD_A|SNES_PAD_Y)) {
      if(s.pause_choice) {
        s.exit_requested=1;s.paused=0;MmxVsSetState(&s);return false;
      }
      s.paused=0;
    }
    MmxVsSetState(&s);return s.paused!=0;
  }
  MmxBossRushState rush=MmxBossRushGetState();
  if(s.phase==MMX_VS_LOADING && s.menu_opening) {
    if(r[0x1ec9]==4)r[0x1ec9]=6;
    if(r[0x1f10] || r[0x1ec9]) {MmxVsSetState(&s);return false;}
    s.menu_opening=s.menu_ready=0;
  }
  if(s.phase==MMX_VS_LOADING && rush.mode==MMX_RUSH_PREPARING) {
    /* Let the native door/checkpoint scripts finish before the hidden anchor
     * reaches the central encounter trigger. Teleporting through the closed
     * door lets its collision push the anchor back outside permanently. */
    put(r+0xac,word(r+0xbad)<rush.camera_x+MMX_RUSH_ARENA_CENTER_X?0x100:0);
  }
  if(s.phase==MMX_VS_LOADING && rush.mode==MMX_RUSH_PLAYING) {
    s.phase=MMX_VS_ENTRANCE;s.ticks=0;
  }
  if(s.shake_ticks)--s.shake_ticks;
  if(s.phase==MMX_VS_ENTRANCE && rush.bosses[0].phase==MMX_RUSH_FIGHTING &&
      rush.bosses[1].phase==MMX_RUSH_FIGHTING) {
    s.phase=MMX_VS_FIGHT;s.ticks=0;
    for(unsigned i=0;i<2;++i) {
      unsigned d=rush.bosses[i].object;
      s.fighters[i].grounded=1;s.fighters[i].facing=(uint8_t)!i;
      s.fighters[i].ground_y=(int16_t)word(r+d+8);
      /* Entrances such as Eagle's replace their assigned X with a stock-room
       * constant. Restore the two seats after both entrances have finished. */
      put(r+d+5,rush.camera_x+MMX_RUSH_ARENA_CENTER_X-64+i*128);
      if(s.fighters[i].boss==6) {
        /* Eagle finishes his entrance hovering. That is not the floor:
         * his standing arrangement extends 27 pixels below its origin. */
        s.fighters[i].ground_y=(int16_t)(rush.camera_y+192-27);
        put(r+d+8,s.fighters[i].ground_y);r[d+0x2f]=0;
      }
      r[d+0x11]=(r[d+0x11]&~64u)|(i?0:64);r[d+0x27]=0xa0;
    }
  }
  for(unsigned i=0;i<2;++i) {
    MmxVsFighter *f=&s.fighters[i];unsigned value=mapped(r,in[i]&~s.release[i]);
    f->pressed=(uint16_t)(value&~f->input);f->input=(uint16_t)value;
    if(f->invulnerable)--f->invulnerable;if(f->stun)--f->stun;if(f->lockout)--f->lockout;
    if(f->effect_ticks)--f->effect_ticks;
    if(f->captured && ++f->capture_ticks>=120) {f->captured=0;f->capture_ticks=0;f->stun=0;f->invulnerable=30;s.proxy_source=0;}
    if(s.phase!=MMX_VS_FIGHT)continue;
    unsigned d=rush.bosses[i].object;
    f->frame_x=(int16_t)word(r+d+5);f->frame_y=(int16_t)word(r+d+8);
    if(f->boss==6 && f->action!=MMX_VS_SPECIAL && (f->pressed&SNES_PAD_B) && !f->stun && !f->captured) {
      f->flying^=1;f->motion_ticks=f->flying?0:60;f->vy=f->vx=0;
      if(f->action==MMX_VS_IDLE)f->started=0;
    }
    if(f->boss==2 && f->action==MMX_VS_SPECIAL && (f->pressed&SNES_PAD_X)) {
      if(f->started)put(r+d+0x20,(uint16_t)f->target_x);
      f->action=MMX_VS_IDLE;f->started=0;f->native_sub=2;f->lockout=0;
      f->pressed&=~SNES_PAD_X;
      f->vx=f->vy=0;if(f->frame_y<f->ground_y)f->motion_ticks=60;
      r[d+2]=r[d+3]=0;r[d+0x37]&=~0x81u;put(r+d+0x1a,0);put(r+d+0x1c,0);
    }
    if(f->boss==5) {
      /* Cling is an independent held movement state; attacks remain usable.
       * Camouflage has no place in the two-player controller. */
      r[d+0x37]=0;f->effect_ticks=0;
      MmxBossRushVisualState v=MmxBossRushVisualGetState();v.initialized[i]=0;MmxBossRushVisualSetState(&v);
      if(!f->stun && !f->captured) {
        if((value&SNES_PAD_A) && !f->attached) {
          f->attached=1;f->flying=0;f->target_x=f->frame_x;f->target_y=f->frame_y;
          f->motion_ticks=0;f->vx=f->vy=0;f->action=MMX_VS_IDLE;f->started=0;
        } else if(!(value&SNES_PAD_A) && f->attached) {
          f->attached=0;f->flying=1;f->motion_ticks=60;f->vy=0;
          f->action=MMX_VS_JUMP;f->ticks=0;f->started=0;
        }
      }
    }
    if(!f->stun && !f->captured && f->frame_y<f->ground_y && !f->attached &&
        !(f->boss==2 && f->action==MMX_VS_SPECIAL)) {
      f->vx=(value&SNES_PAD_LEFT)?-512:(value&SNES_PAD_RIGHT)?512:0;
      if(f->vx)f->facing=f->vx>0;
      r[d+0x11]=(r[d+0x11]&~64u)|(f->facing?64:0);
    }
    if((f->action==MMX_VS_JUMP || f->action==MMX_VS_CEILING) &&
        !f->attached && !f->stun && !f->captured) {
      f->vx=(value&SNES_PAD_LEFT)?-512:(value&SNES_PAD_RIGHT)?512:0;
      if(f->vx)f->facing=f->vx>0;
      put(r+d+0x1a,f->vx);
      r[d+0x11]=(r[d+0x11]&~64u)|(f->facing?64:0);
    }
    if(f->action && f->ticks<240)++f->ticks;
    if(f->action && f->ticks>=240 && !f->attached &&
        !(f->boss==2 && (f->action==MMX_VS_ABILITY || f->action==MMX_VS_SPECIAL)) &&
        !(f->boss==6 && f->action==MMX_VS_SPECIAL)) {
      f->action=MMX_VS_IDLE;f->started=0;r[d+2]=r[d+3]=0;f->lockout=12;
    }
    if((f->action==MMX_VS_IDLE || (f->boss==5 && f->action==MMX_VS_JUMP)) && !f->stun && !f->lockout) {
      unsigned previous_action=f->action;
      if(value&SNES_PAD_LEFT)f->facing=0;else if(value&SNES_PAD_RIGHT)f->facing=1;
      r[d+0x11]=(r[d+0x11]&~64u)|(f->facing?64:0);
      if(f->pressed&SNES_PAD_Y)f->action=MMX_VS_PRIMARY;
      else if(f->pressed&SNES_PAD_X)f->action=MMX_VS_SPECIAL;
      else if((f->pressed&SNES_PAD_A) && f->boss!=7 && f->boss!=5)f->action=f->boss==4?MMX_VS_TELEPORT:MMX_VS_ABILITY;
      else if((f->pressed&SNES_PAD_B) && f->boss!=4) {
        if(f->boss!=6 && (f->boss!=5 || (!f->flying && !f->attached))) {
          f->action=MMX_VS_JUMP;
          if(f->boss==5) {f->flying=1;f->motion_ticks=60;f->vy=0x400;}
        }
      }
      if(f->action && f->action!=previous_action) {
        f->ticks=0;f->started=0;f->native_sub=0;
        if(f->action==MMX_VS_JUMP || f->action==MMX_VS_CEILING)
          f->vx=(value&SNES_PAD_LEFT)?-512:(value&SNES_PAD_RIGHT)?512:0;
        if(f->action==MMX_VS_TELEPORT) {
          f->target_x=f->frame_x;f->target_y=f->frame_y;
        }
      }
    }
    if(f->action==MMX_VS_TELEPORT && !f->native_sub && f->ticks<=16) {
      /* The native vanish windup is the charge window. Freeze the last held
       * direction/distance on release; no direction means blink in place.
       * substate 2 latches an early release, 1 is the arrival animation. */
      if(!(value&SNES_PAD_A))f->native_sub=2;
      else {
        int distance=16+5*f->ticks;
        int dx=(value&SNES_PAD_LEFT)?-distance:(value&SNES_PAD_RIGHT)?distance:0;
        int dy=(value&SNES_PAD_UP)?-distance:(value&SNES_PAD_DOWN)?distance:0;
        if(dx && dy) {int diagonal=(distance*181+128)/256;dx=dx<0?-diagonal:diagonal;dy=dy<0?-diagonal:diagonal;}
        int x=f->frame_x+dx,y=f->frame_y+dy;
        int left=rush.camera_x+32,right=rush.camera_x+336;
        if(x<left)x=left;if(x>right)x=right;
        if(y<rush.camera_y+32)y=rush.camera_y+32;if(y>f->ground_y)y=f->ground_y;
        f->target_x=(int16_t)x;f->target_y=(int16_t)y;
      }
    }
  }
  MmxVsSetState(&s);return false;
}
void MmxVsActorBegin(uint8_t *r,unsigned seat) {
  if(!MmxVsActive())return;MmxVsState s=MmxVsGetState();if(s.projected)return;
  MmxBossRushState rush=MmxBossRushGetState();unsigned target=rush.bosses[seat^1].object;
  memcpy(s.saved_player,r+0xba8,sizeof(s.saved_player));s.projected=(uint8_t)(seat+1);
  if(target) {put(r+0xbad,word(r+target+5));put(r+0xbb0,word(r+target+8));}
  r[0xbcf]=0xa0;r[0xbb6]=1;r[0xbd8]=0;r[0xc16]=0;
  if(s.fighters[seat^1].captured==seat+1)r[0xbaa]=0x16;
  MmxVsSetState(&s);
}
void MmxVsActorEnd(uint8_t *r,unsigned seat) {
  if(!MmxVsActive())return;MmxVsState s=MmxVsGetState();
  MmxBossRushState rush=MmxBossRushGetState();
  if(rush.actor_object==rush.bosses[seat].object && s.phase==MMX_VS_FIGHT) {
    unsigned other=seat^1,target=rush.bosses[other].object;
    MmxVsFighter *victim=&s.fighters[other];
    bool grab=(s.fighters[seat].boss==4 && s.fighters[seat].action==MMX_VS_SPECIAL) ||
        (s.fighters[seat].boss==3 && s.fighters[seat].action==MMX_VS_ABILITY);
    bool releasing=s.fighters[seat].boss==4?r[rush.bosses[seat].object+3]>=6:victim->capture_ticks>=90;
    if(grab && !releasing && target && (!victim->invulnerable || s.proxy_source==rush.bosses[seat].object) && r[0xbaa]==0x16 && victim->hp) {
      if(!victim->captured)victim->capture_ticks=0;
      victim->captured=(uint8_t)(seat+1);victim->stun=3;
      put(r+target+5,word(r+0xbad));put(r+target+8,word(r+0xbb0));
    } else if(victim->captured==seat+1 && (!grab || releasing || r[0xbaa]!=0x16)) {
      victim->captured=0;victim->capture_ticks=0;victim->motion_ticks=30;
      victim->vx=s.fighters[seat].facing?0x300:-0x300;victim->vy=0x400;victim->stun=30;
    }
  }
  if(s.proxy) {
    unsigned d=MmxBossRushGetState().bosses[s.proxy_seat].object;
    if((r[d+0x27]&127)<s.proxy_hp) {
      s.fighters[s.proxy_seat].last_damage=(uint8_t)(s.proxy_hp-(r[d+0x27]&127));
      s.fighters[s.proxy_seat].last_class=r[0x1232];
      s.fighters[s.proxy_seat].invulnerable=60;s.fighters[s.proxy_seat].stun=24;
      s.fighters[s.proxy_seat].attached=0;
    }
    r[d+0x28]=s.proxy_row;
    memcpy(r+0x1228,s.saved_proxy,64);memset(s.saved_proxy,0,64);s.proxy=0;
  }
  if(s.projected) {memcpy(r+0xba8,s.saved_player,sizeof(s.saved_player));memset(s.saved_player,0,sizeof(s.saved_player));s.projected=0;}
  (void)seat;MmxVsSetState(&s);
}
static bool overlaps(CpuState *cpu,unsigned a,unsigned b) {
  uint8_t *r=g_ram;unsigned ap=word(r+a+0x20),bp=word(r+b+0x20);
  if(ap<0x8000 || bp<0x8000)return false;
  int ax=(int16_t)word(r+a+5),ay=(int16_t)word(r+a+8),bx=(int16_t)word(r+b+5),by=(int16_t)word(r+b+8);
  int ao=(int8_t)cpu_read8(cpu,0x86,ap),bo=(int8_t)cpu_read8(cpu,0x86,bp);
  ax+=(r[a+0x11]&64)?-ao:ao;bx+=(r[b+0x11]&64)?-bo:bo;
  ay+=(int8_t)cpu_read8(cpu,0x86,ap+1);by+=(int8_t)cpu_read8(cpu,0x86,bp+1);
  unsigned w=cpu_read8(cpu,0x86,ap+2)+cpu_read8(cpu,0x86,bp+2);
  unsigned h=cpu_read8(cpu,0x86,ap+3)+cpu_read8(cpu,0x86,bp+3);
  int dx=ax-bx,dy=ay-by;return dx>=-(int)w && dx<=(int)w && dy>=-(int)h && dy<=(int)h;
}
static bool contact_attack(const MmxVsFighter *f) {
  if(f->boss==2)return f->action==MMX_VS_SPECIAL && f->started;
  if(f->boss==4)return (f->action==MMX_VS_SPECIAL && f->ticks>=8 && f->native_sub!=2) || (f->action==MMX_VS_TELEPORT && f->ticks>=16 && f->ticks<20);
  if(f->boss==3)return f->action==MMX_VS_ABILITY && f->ticks>=12;
  if(f->boss==7)return f->action==MMX_VS_JUMP && !f->grounded && f->ticks>=6;
  return (f->boss==0 || f->boss==1 || f->boss==2 || f->boss==6) && f->action==MMX_VS_SPECIAL && f->ticks>=8;
}
static bool receive(CpuState *cpu,unsigned seat,uint32_t pc) {
  MmxVsState s=MmxVsGetState();MmxVsFighter *victim=&s.fighters[seat];
  unsigned source=0,other=seat^1,klass=1;MmxBossRushState rush=MmxBossRushGetState();
  if(s.phase==MMX_VS_FIGHT && !victim->invulnerable && !s.proxy && victim->hp) {
    for(unsigned d=0xe68;d<0x1d08;) {
      if(d==0x1228){d=0x1428;continue;}
      bool boss=d==rush.bosses[other].object;
      /* Decoration, Penguin's handle, and passive noncolliding actors are
       * excluded. Body damage is admitted only during an explicit attack. */
      unsigned child_class=s.attack_class[(d-0xe68)/32];
      if(g_ram[d] && g_ram[d+0xe] && MmxBossRushOwner(d)==(int)other &&
          (boss?contact_attack(&s.fighters[other]):child_class!=0) &&
          ((boss && victim->captured==other+1) || overlaps(cpu,cpu->D,d))) {
        source=d;klass=boss?MmxVsAttackClass(s.fighters[other].boss,s.fighters[other].action,false):child_class;break;
      }
      d+=step(d);
    }
  }
  if(!source) {interp_bridge_pre_opcode_redirect(0x849b79);return true;}
  if(getenv("MMX_VS_TRACE"))fprintf(stderr,"vs hit victim=%u source=%04x kind=%02x boss=%u action=%u class=%u\n",
      seat,source,g_ram[source+10],s.fighters[other].boss,s.fighters[other].action,klass);
  memcpy(s.saved_proxy,g_ram+0x1228,64);memcpy(g_ram+0x1228,g_ram+source,64);
  g_ram[0x1228]=0x81;g_ram[0x1232]=(uint8_t)klass;g_ram[0x1258]=0;
  s.proxy=1;s.proxy_seat=(uint8_t)seat;s.proxy_source=(uint16_t)source;s.proxy_hp=g_ram[cpu->D+0x27]&127;
  static const uint8_t rows[8]={9,13,11,7,10,6,8,12};
  s.proxy_row=g_ram[cpu->D+0x28];
  bool rolling_armor=victim->boss==2 && !(victim->action==MMX_VS_ABILITY && (victim->input&SNES_PAD_A));
  if(s.proxy_row>=6 || rolling_armor)g_ram[cpu->D+0x28]=rows[victim->boss];
  MmxVsSetState(&s);cpu->X=0x1228;cpu->x_flag=0;cpu->P&=~0x10u;
  interp_bridge_pre_opcode_redirect(0x849e15);(void)pc;return true;
}
static void control_dispatch(CpuState *cpu,unsigned seat,uint32_t pc) {
  MmxVsState s=MmxVsGetState();MmxVsFighter *f=&s.fighters[seat];unsigned d=cpu->D;
  static uint8_t traced[8];
  if(getenv("MMX_VS_TRACE") && traced[f->boss]<2) {
    ++traced[f->boss];fprintf(stderr,"vs control boss=%u kind=%02x pc=%06x d=%04x action=%u native=%u:%u\n",
        f->boss,g_ram[d+10],pc,d,f->action,g_ram[d+2],g_ram[d+3]);
  }
  bool idle=s.phase!=MMX_VS_FIGHT || f->stun || f->action==MMX_VS_IDLE;
  unsigned reaction=f->boss==0?10:f->boss==1?10:f->boss==2?8:f->boss==3?8:f->boss==4?8:f->boss==5?10:255;
  if(g_ram[d+2]==reaction && f->invulnerable) {
    cpu->X=g_ram[d+2];return;
  }
  if(g_ram[d+2]==reaction && !f->invulnerable) {
    g_ram[d+2]=g_ram[d+3]=0;f->action=MMX_VS_IDLE;f->started=0;idle=true;
  }
  if(f->boss==4 && f->action==MMX_VS_SPECIAL && !f->stun && !f->captured) {
    if(f->ticks<8) {
      if(f->native_sub!=2) {
        /* Approach in his native dash pose before opening the horn grab's
         * short collision window. A distant target must not end it early. */
        f->native_sub=2;f->started=1;f->native_action=6;g_ram[d+2]=6;g_ram[d+3]=0;
        MmxVsSetState(&s);cpu->A=1;call_short(cpu,pc,0x8790b1);return;
      }
      MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
    }
    if(f->native_sub==2) {f->native_sub=1;f->started=0;}
  }
  if(f->boss==2 && f->native_sub==2 && f->action==MMX_VS_IDLE) {
    f->native_sub=0;MmxVsSetState(&s);cpu->A=0;call_short(cpu,pc,0x83b9c8);return;
  }
  if(f->boss==2 && f->action==MMX_VS_SPECIAL && !f->stun && !f->captured) {
    if(!f->started) {
      f->started=1;f->native_action=0;f->target_x=(int16_t)word(g_ram+d+0x20);
      f->vx=f->facing?0x800:-0x800;f->vy=0x400;
      g_ram[d+2]=0;g_ram[d+3]=4;g_ram[d+0x37]&=~0x81u;
      put(g_ram+d+0x20,0xc945);put(g_ram+d+0x1a,0);put(g_ram+d+0x1c,0);
      MmxVsSetState(&s);cpu->A=11;call_short(cpu,pc,0x83b9c8);return;
    }
    MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
  }
  if(f->boss==5 && !f->stun && !f->captured && (f->attached || f->flying) &&
      (f->action==MMX_VS_IDLE || f->action==MMX_VS_JUMP)) {
    if(!f->started) {
      f->started=1;g_ram[d+2]=g_ram[d+3]=0;
      MmxVsSetState(&s);cpu->A=f->attached?0:1;call_long(cpu,pc,0x848f07);return;
    }
    MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
  }
  if(f->attached && f->boss!=5) {
    unsigned release=f->boss==1?SNES_PAD_A:SNES_PAD_B;
    if(f->pressed&release) {
      f->attached=0;f->action=MMX_VS_JUMP;f->started=0;f->ticks=0;idle=false;
    } else {
      int x=word(g_ram+d+5),dx=(f->input&SNES_PAD_LEFT)?-2:(f->input&SNES_PAD_RIGHT)?2:0;
      x+=dx;if(dx)f->facing=dx>0;
      g_ram[d+0x11]=(g_ram[d+0x11]&~64u)|(f->facing?64:0);
      if(!dx) {
        /* Mandrill animation 7 ends on the still two-handed hanging pose.
         * Hold that native command; animation 8 is his swinging traversal. */
        put(g_ram+d+0x14,0xdf68);g_ram[d+0x13]=24;
        g_ram[d+0xf]=0;g_ram[d+0x17]=0x92;f->native_sub=0;
      } else if(!f->native_sub) {
        f->native_sub=1;put(g_ram+d+5,x);MmxVsSetState(&s);
        cpu->A=8;call_long(cpu,pc,0x848f07);return;
      }
      put(g_ram+d+5,x);MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
    }
  }
  if(f->boss==2 && f->action==MMX_VS_ABILITY) {
    if(!(f->input&SNES_PAD_A) || g_ram[d+0x33]) {
      g_ram[d+0x37]&=~1u;g_ram[d+2]=g_ram[d+3]=0;
      f->action=MMX_VS_IDLE;f->started=0;
      MmxVsSetState(&s);cpu->A=0;call_short(cpu,pc,0x83b9c8);return;
    } else if(g_ram[d+0x37]&1) {
      MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
    }
  }
  if(f->boss==7 && f->action==MMX_VS_JUMP && f->started && g_ram[d+3]>=8) {
    /* Native jump state zero can choose another jump without changing its
     * outer state. Finish at its landing boundary rather than repeating it. */
    f->action=MMX_VS_IDLE;f->started=0;f->lockout=24;g_ram[d+2]=g_ram[d+3]=0;
    MmxVsSetState(&s);cpu->A=4;call_long(cpu,pc,0x848f07);return;
  }
  if(f->boss==6 && f->action==MMX_VS_SPECIAL && f->started &&
      g_ram[d+3]==10 && g_ram[d+0x34]<=1) {
    /* One native dive per press, including its off-screen wait and return.
     * Stop before DE1F chooses another autonomous attack (possibly itself). */
    f->action=MMX_VS_IDLE;f->started=0;f->lockout=8;f->flying=1;
    g_ram[d+2]=g_ram[d+3]=0;
    idle=true;
  }
  if(!idle && f->started && g_ram[d+2]!=f->native_action) {
    /* Hurt states retain their own native recovery; a completed voluntary
     * action cannot fall through into autonomous selection on this frame. */
    unsigned hit=f->boss==0?10:f->boss==1?10:f->boss==2?8:f->boss==3?8:f->boss==4?8:f->boss==5?10:255;
    if(g_ram[d+2]!=hit) {
      if((f->action==MMX_VS_JUMP || f->action==MMX_VS_CEILING) &&
          word(g_ram+d+8)<f->ground_y && !f->flying && !f->attached) {
        f->motion_ticks=60;f->vy=(int16_t)word(g_ram+d+0x1c);
      }
      f->action=MMX_VS_IDLE;f->started=0;f->lockout=8;idle=true;
    }
  }
  if(!idle && !f->started) {
    unsigned index=f->action==MMX_VS_PRIMARY?0:f->action==MMX_VS_SPECIAL?1:
        (f->action==MMX_VS_ABILITY || f->action==MMX_VS_TELEPORT)?2:3;
    f->native_action=actions[f->boss][index];g_ram[d+2]=f->native_action;g_ram[d+3]=0;f->started=1;
    if(f->boss==0 && f->action==MMX_VS_ABILITY && (f->input&SNES_PAD_UP))
      g_ram[d+2]=f->native_action=2;
    if(f->boss==6 && f->action==MMX_VS_PRIMARY && (f->input&SNES_PAD_UP))
      g_ram[d+2]=f->native_action=0;
    if(f->boss==6 && f->action==MMX_VS_SPECIAL) {
      /* Retail repeats a random six to nine dives. A playable Special is
       * one full pass, with the native takeoff, dive and return intact. */
      g_ram[d+0x36]=1;f->flying=1;f->motion_ticks=0;
    }
    if(f->action==MMX_VS_JUMP || f->action==MMX_VS_CEILING) {
      f->target_y=(int16_t)word(g_ram+d+8);f->jump_peak=f->target_y;
    }
    if(f->boss==7 && f->action==MMX_VS_JUMP) {
      /* Begin the native airborne substate immediately. Holding Jump adds
       * lift during the first eleven ticks instead of charging on the floor. */
      f->native_sub=0;g_ram[d+3]=4;g_ram[d+0x2f]=255;g_ram[d+0x37]=7;
      put(g_ram+d+0x1a,f->vx);put(g_ram+d+0x1c,0x400);g_ram[d+0x1e]=0x30;
      MmxVsSetState(&s);cpu->A=1;call_long(cpu,pc,0x848f07);return;
    }
    if(f->boss==2 && f->action==MMX_VS_ABILITY) {
      /* Native guard normally waits for an X projectile. Enter its guard
       * substate directly, using its own animation/profile initializer. */
      g_ram[d+3]=4;MmxVsSetState(&s);call_short(cpu,pc,0x83b61b);return;
    }
    if(f->action==MMX_VS_TELEPORT) {
      put(g_ram+d+0x3a,f->target_x);g_ram[d+0x39]=0;
    }
  }
  if(f->boss==6 && f->flying && f->action==MMX_VS_PRIMARY) {
    /* Let the native tornado/flap cast complete at the chosen altitude,
     * rather than waiting forever for its autonomous landing approach. */
    g_ram[d+0x2f]=0;
  }
  if(f->action==MMX_VS_TELEPORT && f->ticks>=16) {
    if(f->native_sub!=1) {
      f->native_sub=1;MmxVsSetState(&s);cpu->A=0;call_long(cpu,pc,0x848f07);return;
    }
    MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
  }
  if(f->boss==6 && f->action==MMX_VS_IDLE && !f->started) {
    f->started=1;g_ram[d+0x38]=f->flying?5:0;
    MmxVsSetState(&s);cpu->A=g_ram[d+0x38];call_long(cpu,pc,0x848f07);return;
  }
  if(idle) {
    MmxVsSetState(&s);call_long(cpu,pc,0x848eea);return;
  }
  /* Native facing helpers target X; this facing is already seat-owned. */
  MmxVsSetState(&s);cpu->X=g_ram[d+2];
}
bool MmxVsNativeHook(CpuState *cpu,uint32_t pc) {
  if(!MmxVsActive())return false;uint32_t at=pc|0x800000;
  MmxVsState s=MmxVsGetState();int owner=MmxBossRushOwner(cpu->D);
  if(at==0x80c67f && s.phase==MMX_VS_SELECT && s.menu_ready) {
    unsigned slot=MmxVsWeaponSlot(s.cursor[s.picker]);
    bool changed=g_ram[cpu->D+10]!=slot;
    g_ram[cpu->D+10]=g_ram[cpu->D+11]=(uint8_t)slot;
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|(changed?0xc6bd:0xc770));return true;
  }
  if(setup_batch && (at==0x8088a2 || at==0x8088cd)) {
    /* Positional and nonpositional SFX entry points share this bare RTL. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x88eb);return true;
  }
  if((at&0x7fffff)==0x018136 && MmxBossRushGetState().mode==MMX_RUSH_PLAYING && s.phase!=MMX_VS_SELECT) {
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x819c);return true;
  }
  if(owner<0)return false;
  if(at==0x849b03) {interp_bridge_pre_opcode_redirect(0x849b79);return true;}
  if(at==0x849b43)return body((unsigned)owner,cpu->D)?receive(cpu,(unsigned)owner,pc):false;
  if(at==0x849e3a && s.proxy) {
    unsigned cls=cpu->A&255;
    /* Damage identity and the controller's private reaction encoding are
     * distinct. Resolve the requested X1 weakness cycle explicitly in VS. */
    if(s.fighters[owner].boss==1 && cls==7)cls=14;
    else if(s.fighters[owner].boss==4 && cls==14)cls=7;
    if(s.proxy_source==MmxBossRushGetState().bosses[owner^1].object &&
        s.fighters[owner^1].boss==4 && s.fighters[owner^1].action==MMX_VS_SPECIAL)cls=1;
    cpu->A=(cpu->A&0xff00)|cls;return false;
  }
  if((at==0x8790fb || at==0x879110) && s.fighters[owner].action==MMX_VS_TELEPORT) {
    cpu->A=(uint16_t)s.fighters[owner].target_x;return true;
  }
  if(at==0x84aca5 && body((unsigned)owner,cpu->D) && s.phase==MMX_VS_FIGHT) {
    interp_bridge_pre_opcode_redirect(0x84acbd);return true;
  }
  if(body((unsigned)owner,cpu->D) && at==dispatches[s.fighters[owner].boss]) {
    control_dispatch(cpu,(unsigned)owner,pc);return true;
  }
  return false;
}
static void hook(CpuState *cpu,uint32_t pc) {(void)MmxVsNativeHook(cpu,pc);}
void MmxVsHostFrame(void) {
#if !MMX_VARIANT_JP
  for(unsigned i=0;i<8;++i)interp_bridge_add_pre_opcode_hook(dispatches[i],hook);
  interp_bridge_add_pre_opcode_hook(0x818136,hook);
  interp_bridge_add_pre_opcode_hook(0x80c67f,hook);
  interp_bridge_add_pre_opcode_hook(0x8088a2,hook);
  interp_bridge_add_pre_opcode_hook(0x8088cd,hook);
  interp_bridge_add_pre_opcode_hook(0x84aca5,hook);
  interp_bridge_add_pre_opcode_hook(0x849e3a,hook);
#endif
}
void MmxVsAfterFrame(uint8_t *r) {
  if(!MmxVsActive())return;MmxVsState s=MmxVsGetState();MmxBossRushState rush=MmxBossRushGetState();
  if(s.phase!=MMX_VS_FIGHT)return;
  for(unsigned d=0x1428;d<0x1628;d+=64) {
    int owner=MmxBossRushOwner(d);
    if(owner>=0 && r[d] && s.fighters[owner].boss==7 && r[d+10]==0x21 && r[d+1]>=6)
      s.attack_class[(d-0xe68)/32]=10;
  }
  for(unsigned i=0;i<2;++i) {
    MmxVsFighter *f=&s.fighters[i];unsigned d=rush.bosses[i].object;if(!d)continue;
    f->hp=rush.bosses[i].health;
    int x=word(r+d+5),y=word(r+d+8),left=rush.camera_x+32,right=rush.camera_x+336;
    int dx=(f->input&SNES_PAD_LEFT)?-2:(f->input&SNES_PAD_RIGHT)?2:0;
    int dy=(f->input&SNES_PAD_UP)?-2:(f->input&SNES_PAD_DOWN)?2:0;
    bool free=!f->stun && !f->captured;
    bool diving=f->boss==6 && f->action==MMX_VS_SPECIAL && free;
    if(diving) {
      /* The retail attack ascends above the camera, dives below it and then
       * returns. Arena clipping prevents every one of those transitions. */
      r[d+0x2f]=255;
    } else if(f->boss==5 && f->attached && free) {
      x=f->target_x;y=f->target_y;put(r+d+0x1a,0);put(r+d+0x1c,0);
    } else if(f->boss==5 && f->flying && free) {
      x=f->frame_x+dx;y=f->frame_y-f->vy/256;
      if(f->motion_ticks)--f->motion_ticks;
      f->vy-=48;
      if(f->motion_ticks>49 && f->vy>0 && (f->input&SNES_PAD_B))f->vy+=40;
      if(y>=f->ground_y) {
        y=f->ground_y;f->flying=0;f->motion_ticks=0;f->vy=0;
        if(f->action==MMX_VS_JUMP) {f->action=MMX_VS_IDLE;f->started=0;}
      }
      put(r+d+0x1a,0);put(r+d+0x1c,0);r[d+0x2f]=f->flying?255:0;
    } else if(f->boss==6 && f->flying && free) {
      /* Read flight directions every simulation tick, even while attacking.
       * Only the explicitly requested dive retains native attack velocity. */
      x=(f->action==MMX_VS_SPECIAL?x:f->frame_x)+dx;
      y=(f->action==MMX_VS_SPECIAL?y:f->frame_y)+dy;
      r[d+0x2f]=255;
      if(f->action!=MMX_VS_SPECIAL) {put(r+d+0x1a,0);put(r+d+0x1c,0);}
    } else if(f->boss==2 && f->action==MMX_VS_SPECIAL && f->started && free) {
      x=f->frame_x+f->vx/256;y=f->frame_y-f->vy/256;
      if(x<left) {x=left*2-x;f->vx=(int16_t)abs(f->vx);}
      if(x>right) {x=right*2-x;f->vx=(int16_t)-abs(f->vx);}
      int top=rush.camera_y+32;
      if(y<top) {y=top*2-y;f->vy=(int16_t)-abs(f->vy);}
      if(y>f->ground_y) {y=f->ground_y*2-y;f->vy=(int16_t)abs(f->vy);}
      f->facing=f->vx>0;r[d+0x11]=(r[d+0x11]&~64u)|(f->facing?64:0);
      put(r+d+0x1a,0);put(r+d+0x1c,0);
    } else if(f->motion_ticks && !f->captured) {
      --f->motion_ticks;x+=f->vx/256;y-=f->vy/256;f->vy-=64;
      if(y>=f->ground_y) {y=f->ground_y;f->motion_ticks=0;f->stun=0;}
    }
    if(free && f->frame_y<f->ground_y && !f->attached && f->action!=MMX_VS_TELEPORT &&
        !(f->boss==2 && f->action==MMX_VS_SPECIAL) && !(f->boss==6 && f->flying))
      x=f->frame_x+dx;
    if(free && f->boss==4 && f->action==MMX_VS_SPECIAL && f->started &&
        f->ticks<8 && !s.fighters[i^1].captured) {
      int gap=((int)word(r+rush.bosses[i^1].object+5)-f->frame_x)*(f->facing?1:-1),stride=8;
      /* Do not dash through a nearby opponent before the horn opens. */
      if(gap>=0 && gap<32)stride=gap>24?gap-24:0;
      x=f->frame_x+(f->facing?stride:-stride);
    }
    if(free && f->action==MMX_VS_TELEPORT && f->ticks<16) {
      /* Keep the charge origin stationary until the native blink arrives. */
      x=f->frame_x;y=f->frame_y;put(r+d+0x1a,0);put(r+d+0x1c,0);
    }
    if(!diving) {
      if(x<left)x=left;if(x>right)x=right;
      if(y<rush.camera_y+32)y=rush.camera_y+32;if(y>f->ground_y)y=f->ground_y;
    }
    put(r+d+5,x);put(r+d+8,y);
    if(f->action==MMX_VS_TELEPORT && f->ticks>=16) {
      put(r+d+5,f->target_x);put(r+d+8,f->target_y);put(r+d+0x1a,0);put(r+d+0x1c,0);r[d+0xe]=1;
    }
    if(f->action==MMX_VS_TELEPORT && f->ticks>=30) {
      f->action=MMX_VS_IDLE;f->started=0;f->lockout=12;r[d+2]=r[d+3]=r[d+0x38]=r[d+0x39]=0;
      if(f->target_y<f->ground_y) {f->motion_ticks=60;f->vy=f->vx=0;}
    }
    if(f->action==MMX_VS_JUMP && f->ticks==1) {
      put(r+d+0x1a,f->vx);
      if(f->boss==7)put(r+d+0x1c,0x400);
    }
    if((f->action==MMX_VS_JUMP || f->action==MMX_VS_CEILING) && f->started && f->ticks<60 && !f->attached)
      put(r+d+0x1a,f->vx);
    if(f->boss==7 && f->action==MMX_VS_JUMP && f->ticks>1 && f->ticks<12 && (f->input&SNES_PAD_B))
      put(r+d+0x1c,word(r+d+0x1c)+40);
    if(f->boss==1 && f->action==MMX_VS_ABILITY && !f->attached) {
      if(f->ticks==1)put(r+d+0x1c,0x880);
      if(y<=rush.camera_y+56 || (r[d+0x2b]&8)) {
        f->attached=1;f->native_sub=0;put(r+d+0x1a,0);put(r+d+0x1c,0);put(r+d+8,rush.camera_y+56);
      }
    }
    bool grounded=(r[d+0x2b]&4)!=0 || y>=f->ground_y;
    if(f->action==MMX_VS_JUMP && y<f->jump_peak)f->jump_peak=(int16_t)y;
    if(f->boss==7 && grounded && !f->grounded && f->target_y-f->jump_peak>=48) {
      s.shake_ticks=18;
      if(s.fighters[i^1].grounded)s.fighters[i^1].stun=18;
    }
    f->grounded=(uint8_t)grounded;
    if(f->attached || diving)f->grounded=0;
    if(f->boss==5 && !f->effect_ticks && r[d+0x37]) {
      r[d+0x37]=0;MmxBossRushVisualState v=MmxBossRushVisualGetState();v.initialized[i]=0;MmxBossRushVisualSetState(&v);
    }
  }
  MmxVsSetState(&s);MmxVsResolveRound(s.fighters[0].hp,s.fighters[1].hp);
}
