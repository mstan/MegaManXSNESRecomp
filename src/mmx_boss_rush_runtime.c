#include "mmx_boss_rush.h"
#include "mmx_vs.h"
#include "mmx_boss_rush_audio.h"
#include "mmx_boss_rush_score.h"
#include "host_paths.h"
#include "mmx_coop.h"
#include "mmx_renderer.h"
#include "mmx_render_assets.h"
#include "mmx_wide_policy.h"
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
#include <stdlib.h>

extern uint8_t g_ram[0x20000];
static unsigned word(const uint8_t *p) {return p[0]|p[1]<<8;}
static void put(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static unsigned entry_x(const uint8_t *r,unsigned seat) {return word(r+0x1e4d)+64+seat*128;}
static void stack_push(CpuState *cpu,unsigned v,unsigned bytes) {
  while(bytes) {cpu_write8(cpu,0,cpu->S,(uint8_t)(v>>(--bytes*8)));--cpu->S;}
}
static unsigned stack_pop(CpuState *cpu,unsigned bytes) {
  unsigned v=0;for(unsigned i=0;i<bytes;++i) v|=(unsigned)cpu_read8(cpu,0,++cpu->S)<<(i*8);
  return v;
}
static bool arena_ready(const uint8_t *r) {
  /* Start beneath the room's central hook, with the native follow camera
   * framing the player near the middle and an entrance on either side. */
  unsigned right=r[0x1f7a]==4?0x1e00+MMX_RUSH_ARENA_CENTER_X:0x1e20;
  return word(r+0x1e4d)>=0x1e00 && word(r+0xbad)>=right &&
      r[0xbaa]!=0x1a && r[0xbaa]!=0x18 && !r[0xc16];
}
/* These are scene/camera flags, not inventory or player combat state. Native
 * entrances may borrow them during their own update, never across actors. */
static const uint16_t globals[]={0x1f0c,0x1f0e,0x1f0f,0x1f13,0x1f14,0x1f15,
  0x1f16,0x1f17,0x1f18,0x1f1a,0x1f23,0x1f26,0x1f31,0x1f3b,0xc16,
  0x1e5c,0x1e5d,0x1e5e,0x1e5f,0x1e60,0x1e61,0x1e62,0x1e63,
  0xc0,0xc1,0xc9,0xca,0xcb,0xcc,0xcd,
  0xc0c,0xbd8,0x1e54,0x1e55,0x1e74,0x1e75,0x1e76,0x1e77};
/* Retain the array slots for existing snapshots. These bytes belong to the
 * projected player: swapping them clears hurt protection or an ice/grab lock
 * when the boss's update returns. Entrance locks are suppressed separately. */
static bool private_global(unsigned i) {return globals[i]!=0xbd8 && globals[i]!=0xc16;}
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
    if(s.versus)MmxVsActorEnd(g_ram,owner);
    for(unsigned d=0xe68;d<0x1d08;) {
      if(d==0x1228) {d=0x1428;continue;}
      unsigned n=(d-0xe68)/32;
      if(!g_ram[d]) s.owner[n]=0;
      else if(!s.occupied[n]) {
        s.owner[n]=(uint8_t)(owner+1);
        if(s.versus)MmxVsChildAllocated(g_ram,d,s.actor_object,owner);
      }
      d+=object_step(d);
    }
    for(unsigned i=0;i<sizeof(globals)/sizeof(*globals);++i) {
      if(!private_global(i)) continue;
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
    if(!private_global(i)) continue;
    s.globals[i]=g_ram[globals[i]];g_ram[globals[i]]=s.bosses[owner].globals[i];
  }
  remember_objects(&s);
  put(g_ram+0x1f0e,s.bosses[owner].object);g_ram[0x1f26]=0;
  MmxBossRushSetState(&s);
  if(s.versus)MmxVsActorBegin(g_ram,(unsigned)owner);
}
static void boss_hook(CpuState *cpu,uint32_t pc) {
  if(MmxVsNativeHook(cpu,pc))return;
  MmxBossRushState s=MmxBossRushGetState();int owner=MmxBossRushOwner(cpu->D);
  unsigned at=pc&0x7fffff;
  if(at==0x00c6dd && (s.mode==MMX_RUSH_PREPARING || s.mode==MMX_RUSH_PLAYING)) {
    /* An old save may already have its cursor on an invalid $90 empty tank.
     * Recheck the reserve at confirmation as well as during menu navigation. */
    unsigned cursor=g_ram[cpu->D+10];
    if(cursor<10 || cursor>13 || !(g_ram[0x1f83+cursor-10]&128) ||
        !(g_ram[0x1f83+cursor-10]&15)) {
      interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xc6f8);return;
    }
  }
  if(at==0x00c93c && (s.mode==MMX_RUSH_PREPARING || s.mode==MMX_RUSH_PLAYING)) {
    /* Full weapon inventory normally enables the Escape Unit. An endless
     * run ends only on death: use the retail unavailable cue/zero return,
     * before the menu can set its escape flag or start the exit sequence. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xc94f);return;
  }
  if(at==0x00d1ed && s.mode==MMX_RUSH_PREPARING) {
    /* Run the native music call inside the guest scheduler, so a multi-frame
     * SPC upload can yield and resume normally. Saved registers live on the
     * guest stack; the pending return is part of the existing snapshot state.
     * Penguin's health-fill controller requests battle song $1E at $81:B617.
     * Rush isolates that controller's global intro flags, bypassing its call. */
    if(s.loaded==MMX_RUSH_MUSIC_PENDING) {
      cpu->A=(uint16_t)stack_pop(cpu,2);cpu->X=(uint16_t)stack_pop(cpu,2);
      cpu->Y=(uint16_t)stack_pop(cpu,2);cpu->D=(uint16_t)stack_pop(cpu,2);
      cpu->DB=(uint8_t)stack_pop(cpu,1);cpu->P=(uint8_t)stack_pop(cpu,1);
      cpu_p_to_mirrors(cpu);
      s.loaded=MMX_RUSH_ARENA_LOADED;s.mode=MMX_RUSH_PLAYING;MmxBossRushSetState(&s);
    } else if(arena_ready(g_ram)) {
      cpu_mirrors_to_p(cpu);
      stack_push(cpu,cpu->P,1);stack_push(cpu,cpu->DB,1);stack_push(cpu,cpu->D,2);
      stack_push(cpu,cpu->Y,2);stack_push(cpu,cpu->X,2);stack_push(cpu,cpu->A,2);
      stack_push(cpu,pc>>16,1);stack_push(cpu,(pc-1)&65535,2);
      cpu->D=0;cpu->DB=0x86;cpu->A=0x1e;cpu->P|=0x30;cpu_p_to_mirrors(cpu);
      cpu->X&=255;cpu->Y&=255;
      s.loaded=MMX_RUSH_MUSIC_PENDING;MmxBossRushSetState(&s);
      interp_bridge_pre_opcode_redirect(0x8087a2);return;
    }
  }
  if(at==0x00dd47 && (s.mode==MMX_RUSH_PREPARING ||
      (s.mode==MMX_RUSH_LOADING && s.stage_started && !s.return_title))) {
    /* Keep native door/camera/checkpoint records; suppress stage enemies,
     * including the stock Penguin, before their object allocation. */
    unsigned record=word(g_ram+cpu->D+0x18);
    if((cpu_read8(cpu,cpu->DB,(uint16_t)record)&15)==3) {
      interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xdd94);return;
    }
  }
  if(at==0x00e68e && s.mode==MMX_RUSH_LOADING && s.stage_started && !s.return_title) {
    /* Stage initialization clears the checkpoint after the title callback.
     * Select it when the native checkpoint table is actually read. */
    g_ram[0x1f81]=2;g_ram[0x1f82]=0;
  }
  if(at==0x0094d9 && s.return_title) {
    /* Retire the stage's auxiliary tasks before the opening installs its
     * graphics/palette tasks. Otherwise a suspended stage upload resumes
     * against the opening's reused scratch buffers. Keep our active task. */
    for(unsigned slot=0;slot<0x70;slot+=0x10)
      if(slot!=g_ram[0xa0]) g_ram[0x30+slot]=0;
    MmxBossRushReset();MmxCoopReset();
    s=MmxBossRushGetState();s.return_title=1;MmxBossRushSetState(&s);
    memset(g_ram+0xd1,0,4);
    memset(g_ram+0xac,0,4); /* Consume the results confirmation before intro skip. */
    memset(g_ram+0x38,0,5);g_ram[0x39]=4;
    /* The native logo opening ($8CE1) resets the scene, loads its art and
     * requests title music before the logo flies in. Select that opening
     * through its normal loop, without replaying the startup gameplay demo. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x8c56);return;
  }
  if(at==0x008c56 && s.return_title && (g_ram[0x38]==2 || g_ram[0x39]==6)) {
    /* After the complete logo sequence, show the requested menu rather
     * than starting the native attract demo. Start can still skip the intro. */
    if(g_ram[0x39]==6) {g_ram[0x38]=2;memset(g_ram+0x39,0,4);}
    s.return_title=0;MmxBossRushSetState(&s);return;
  }
  if(at==0x0094d9 && s.mode==MMX_RUSH_LOADING && !s.stage_started) {
    g_ram[0xd1]=2;g_ram[0xd2]=2;g_ram[0xd3]=g_ram[0xd4]=0;
    /* Mammoth's third checkpoint starts in the final door corridor. */
    g_ram[0x1f7a]=4;g_ram[0x1f81]=2;g_ram[0x1f82]=0;
    equipment(g_ram);s.stage_started=1;MmxBossRushSetState(&s);
  }
  if(at==0x0088d6 && playing() && getenv("MMX_BOSS_RUSH_AUDIO_TRACE")) {
    fprintf(stderr,"rush-sfx frame=%d actor=%u object=%04x command=%02x pan=%02x\n",snes_frame_counter,s.actor,cpu->D,cpu->A&255,cpu->Y&255);
  }
  unsigned emitter=owner==(int)s.actor-1 ? cpu->D : s.actor_object;
  if(at==0x0088d6 && playing() && s.actor &&
      MmxBossRushAudioQueue(s.actor-1,emitter,cpu->A&255,cpu->Y&255,s.bosses[s.actor-1].generation))
    cpu->A=(cpu->A&0xff00)|0xb1; /* Acknowledged null effect; preserve the native ring protocol. */
  if(at==0x00dc36 && playing()) {
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0xdcda);return;
  }
  if((at==0x0498ff || at==0x04990d) && g_ram[0x1f7a]==4 &&
      (s.mode==MMX_RUSH_PREPARING || s.mode==MMX_RUSH_PLAYING) && !MmxBossRushConveyorRunning(&s)) {
    /* The native solid-floor correction has already run. Skip only the
     * half-pixel belt carry, preserving floor contact for either player. */
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9929);return;
  }
  if(!playing()) return;
  if(at==0x009ac7) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9ad9);return;}
  /* Native grab, throw damage and release helpers also write player poses
   * directly. A grab after a fatal contact must not replace the death pose. */
  if(s.actor && !(g_ram[0xbcf]&127)) {
    unsigned end=at==0x049f19?0x9f29:
        (at==0x049f2a || at==0x049f2f)?0x9f7d:at==0x049f7e?0x9f89:0;
    if(end) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|end);return;}
  }
  if(owner<0) return;
  if(s.bosses[owner].object==cpu->D) {
    /* Retail victory prologues grant the player indefinite protection before
     * reaching the common death controller. Rush replaces that victory scene,
     * so leave the player's existing hurt timer/latch intact. Each boundary
     * skips only LDA #1 and the two victory flag writes, before the next LDA. */
    unsigned end=at==0x01b6b2?0xb6ba:at==0x03b3ae?0xb3b6:
        at==0x01c5ca?0xc5d2:at==0x078cb8?0x8cc0:
        at==0x079326?0x932e:at==0x07d9bf?0xd9c7:0;
    if(end) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|end);return;}
    if(s.bosses[owner].id==4 && (at==0x0790fb || at==0x079110)) {
      /* Kuwanger's retail destinations assume a single-screen tower room.
       * Keep his original target selection inside this arena's closed doors. */
      unsigned left=s.camera_x+32;
      unsigned right=s.camera_x+256+(g_ram[0x1f7a]==4?MMX_RUSH_MAMMOTH_SCROLL:0)-32;
      unsigned x=cpu->A;if(x<left)x=left;if(x>right)x=right;
      cpu->A=(uint16_t)x;cpu->_flag_Z=!x;cpu->_flag_N=x>>15;
      cpu->P=(cpu->P&~0x82u)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
      return;
    }
  }
  if(g_ram[0x1f7a]==4 && s.bosses[owner].id==0) {
    /* Move the native handle constructor and both body approach/landing
     * calculations together. Each hook follows the original 16-bit load or
     * add; subsequent native comparisons/motion still use the same target.
     * Never change the shared camera or Penguin's other attack boundaries. */
    bool body=s.bosses[owner].object==cpu->D;
    if((body && (at==0x01b7bf || at==0x01b81b)) ||
        (!body && at==0x07efd2 && g_ram[cpu->D+10]==0x12)) {
      unsigned origin=s.camera_x+MMX_RUSH_ARENA_CENTER_X-128;
      unsigned value=at==0x01b81b?cpu->A-word(g_ram+0x1e60)+origin:origin;
      cpu->A=(uint16_t)value;cpu->_flag_Z=!cpu->A;cpu->_flag_N=cpu->A>>15;
      cpu->P=(cpu->P&~0x82u)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
      return;
    }
  }
  if(at==0x028297 || at==0x02829d || at==0x03b192) {
    /* $82:827D initializes OBJ metadata from the current stage's $7F
     * allocation tables. Cross-stage bodies otherwise save palette zero
     * as their normal palette ($33), so their native hit flash cannot toggle.
     * Supply their own ROM allocation before the original STA. Both native
     * loads still execute, preserving their bus reads and cycle timing.
     * Armadillo then overwrites $11 directly from $7F:835D at $83:B18C.
     * Supply that allocation after his native AND #$FE as well; he saves
     * its palette in $35 rather than the generic damage helper's $33. */
    const MmxSpriteAsset *art=MmxRenderAssetsRushSprite(
        kMmxBossRushBosses[s.bosses[owner].id].stage,at==0x03b192?0x62:g_ram[cpu->D+0x16]);
    if(art) {
      unsigned value=at==0x028297?art->tile_base:art->attributes&(at==0x03b192?0xfe:0xff);
      cpu->A=(cpu->A&0xff00)|value;cpu->_flag_Z=!value;cpu->_flag_N=(value>>7)&1;
      cpu->P=(cpu->P&~0x82u)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
      return;
    }
  }
  if(s.bosses[owner].id==5 && s.bosses[owner].object==cpu->D) {
    /* Native $88:8DAB/$8E0F increment/decrement each RGB5 channel. Their
     * shared $0480/$04A0 buffers belong to the arena; update private colors
     * at these same calls instead. The initial clear is $88:85B5. */
    unsigned end=at==0x0885b5?0x85c0:at==0x088dab?0x8e0e:at==0x088e0f?0x8e4e:0;
    if(end) {
      const MmxSpriteAsset *art=MmxRenderAssetsRushSprite(2,0x23);
      if(art) MmxBossRushVisualFade((unsigned)owner,s.bosses[owner].generation,
          art->colors,at==0x0885b5?0:at==0x088dab?1:-1);
      interp_bridge_pre_opcode_redirect((pc&0xff0000)|end);return;
    }
    /* His native HDMA bends the entire stage's BG2. Keep the phase update,
     * but do not install or remove a shared arena task for one boss. */
    end=at==0x088d79?0x8daa:at==0x088d2f?0x8d49:at==0x088d5e?0x8d76:at==0x08876a?0x876d:0;
    if(end) {interp_bridge_pre_opcode_redirect((pc&0xff0000)|end);return;}
  }
  if(at==0x048fca) {
    /* Boss art is decoded privately per pose. Its native DMA would read the
     * arena's staging buffer and overwrite X/weapon/other boss CHR. */
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
      /* The room has already been opened for every pair. His stock intro
       * must not add $70 again to its private movement boundary. */
      if(g_ram[0x1f7a]==4) put(g_ram+0x1e60,s.camera_x+MMX_RUSH_MAMMOTH_SCROLL);
      put(g_ram+cpu->D+8,s.camera_y+160);
    }
    if(at==0x07925e) {
      /* Keep his assigned side after the stock arrival adds $40 to X. */
      cpu->A=(uint16_t)entry_x(g_ram,(unsigned)owner);
      cpu->_flag_Z=!cpu->A;cpu->_flag_N=cpu->A>>15;
      cpu->P=(cpu->P&~0x82u)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
    }
    /* The factory waits for a player to walk under the entrance platform.
     * Rush starts his arrival on the visible screen after the run trigger. */
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
  /* Another boss can remain in contact during the native death countdown.
   * Never deliver another hit to an actor whose health is already empty:
   * the damage routine otherwise resets the death substate every frame. */
  if((at==0x049b03 || at==0x049b43) &&
      (s.bosses[owner].phase!=MMX_RUSH_FIGHTING || !(g_ram[0xbcf]&127)))
    interp_bridge_pre_opcode_redirect((pc&0xff0000)|0x9b79);
}
void MmxBossRushHostFrame(void) {
#if !MMX_VARIANT_JP
  static bool score_opened,score_warned;
  if(!score_opened) {
    char path[4096];score_opened=true;
    if(!snesrecomp_exe_dir_path("mmx-boss-rush-score.dat",path,sizeof(path)) || !MmxBossRushScoreOpen(path))
      fprintf(stderr,"[mmx-rush] high score could not be loaded; existing record left unchanged\n");
  }
  MmxBossRushState result=MmxBossRushGetState();
  if(result.mode==MMX_RUSH_FINISHED && !MmxBossRushScoreRecord(result.defeated) && !score_warned) {
    score_warned=true;fprintf(stderr,"[mmx-rush] high score retained for this session but could not be saved\n");
  }
  if(result.mode==MMX_RUSH_LOADING || result.mode==MMX_RUSH_PREPARING || result.mode==MMX_RUSH_PLAYING)
    MmxBossRushAudioPrepare(g_snes->cart->rom,g_snes->cart->romSize);
  bool registered=true;
  /* Idempotent registration: mod activation can replace shared hooks. Add
   * ours after its setup so co-op contact replay stays authoritative. */
  const unsigned actors[]={0xd4f6,0xd4f9,0xd515,0xd522,0xd499,0xd49c,0xd4b8,0xd4c5,0xd359,0xd35c};
  for(unsigned i=0;i<sizeof(actors)/sizeof(*actors);++i)
    registered&=interp_bridge_add_pre_opcode_hook(actors[i],actor_hook);
  const unsigned bosses[]={0x849feb,0x84a003,0x84aadd,0x84a677,0x849b03,0x849b43,
    0x848fca,0x848fad,0x9ac7,0xdc36,0xdd47,0xd1ed,0x94d9,0x8c56,0xe68e,0x879258,0x879276,0x8088d6,0x849f19,0x849f2a,0x849f2f,0x849f7e,
    0x8885b5,0x888dab,0x888e0f,0x888d79,0x888d2f,0x888d5e,0x88876a,0x828297,0x82829d,0x83b192,
    0x8498ff,0x84990d,0x81b7bf,0x81b81b,0x87efd2,0x87925e,0xc93c,
    0x81b6b2,0x83b3ae,0x81c5ca,0x878cb8,0x879326,0x87d9bf,0x8790fb,0x879110,0xc6dd};
  for(unsigned i=0;i<sizeof(bosses)/sizeof(*bosses);++i)
    registered&=interp_bridge_add_pre_opcode_hook(bosses[i],boss_hook);
  static bool warned;
  if(!registered && !warned) {warned=true;fprintf(stderr,"[mmx-rush] native hook registration failed: interpreter hook table is full\n");}
#endif
}
static bool title(const uint8_t *r) {
  unsigned y=word(r+0xbb0);
  return r[0xd1]==0 && r[0xba9]==2 && word(r+0xbad)==32 && y>=166 && y<=230;
}
static void equipment(uint8_t *r) {
  r[0x1f99]=0xff;r[0x1f9a]=32;r[0x1f98]=0x40;r[0xbcf]=0xa0;
  r[0x1f9c]=0xff;r[0x1f7e]&=0x7f; /* All hearts; Hadouken remains unacquired. */
  /* Retail refill caps tanks at $8E. Bit 7 is ownership; bits 4..5
   * must stay clear or the menu still considers a depleted tank nonempty. */
  for(unsigned i=0;i<4;++i) r[0x1f83+i]=0x8e;
  for(unsigned i=0;i<8;++i) {r[0x1f87+i*2]=0;r[0x1f88+i*2]=0xdc;}
  MmxZeroHealthRespawn(r);
  r[0x1f80]=0; /* No spare-life re-entry. */
}
static void load(uint8_t *r,bool coop) {
  MmxBossRushState previous=MmxBossRushGetState();uint16_t held=previous.input;
  /* Native RNG and NMI counters vary with the player's confirmation timing.
   * Mixing the previous queue RNG also gives Retry a fresh opening. These
   * inputs belong to guest/snapshot state, so replay uses the same shuffle. */
  uint32_t seed=((uint32_t)word(r+0xba6)<<16)^word(r+0xb9b)^previous.random^0x9e3779b9u;
  MmxBossRushStart(coop,seed);MmxBossRushAudioReset();MmxCoopReset();
  MmxBossRushState s=MmxBossRushGetState();s.input=held;
  s.menu_input=held&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y);
  MmxBossRushSetState(&s);
  if(title(r)) {
    /* Use the same mini charged shot and delay as the other native choices,
     * keeping the cursor and lettering on the fourth row until the fade. */
    MmxBossRushState s=MmxBossRushGetState();s.menu=1;s.selection=3;
    MmxBossRushSetState(&s);
    r[0x3c]=0;r[0x39]=4;r[0x3b]=60;r[0xc01]=2;
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
  /* Checkpoint 2 already spawns at $1D80,$0200 in the final corridor.
   * Preserve its native arrival, room map, collision and camera. */
  MmxBossRushState s=MmxBossRushGetState();
  s.camera_x=0x1e00;s.camera_y=r[0x1f7a]==4?0x200:0x100;
  s.loaded=1;s.mode=MMX_RUSH_PREPARING;s.menu=0; /* Retain old Penguin loading snapshots. */
  r[0x1fa0]=0;
  equipment(r);
  MmxBossRushSetState(&s);
  if(s.versus)MmxVsPrepareArena(r);
  if(!s.versus && MmxCoopEnabled()) {
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
  /* $80:C466 clears the menu camera before installing its HUD/HDMA flags.
   * The coroutine can yield between those steps. This arena's X is $1E00;
   * preserve the native zero origin from the first clear through its fades. */
  if(!word(r+0x1e4d) || !MmxWidePolicy_IsStageScene(r)) return;
  MmxBossRushState s=MmxBossRushGetState();
  if(r[0x1f7a]==4) {
    /* The door first locks one screen; Mammoth's stock intro normally opens
     * the extra $70. Do that for every pair after the door releases, retaining
     * the native gradual boundary update, player follow and menu restoration. */
    if(word(r+0x1e4d)<s.camera_x || r[0xbaa]==0x18 || r[0xbaa]==0x1a || r[0xc16]) return;
    put(r+0x1e5e,s.camera_x);put(r+0x1e60,s.camera_x+MMX_RUSH_MAMMOTH_SCROLL);
    return;
  }
  put(r+0x1e4d,s.camera_x);put(r+0x1e50,s.camera_y);
  put(r+0x1e56,s.camera_x);put(r+0x1e58,s.camera_x);
  put(r+0x1e5a,s.camera_y);put(r+0x1e5c,s.camera_y);
  put(r+0x1e5e,s.camera_x);put(r+0x1e60,s.camera_x);put(r+0x1e62,s.camera_y);
}
static bool drop_health(uint8_t *r,MmxBossRushState *s,const MmxBossRushBoss *b) {
  for(unsigned d=0x1628;d<0x1928;d+=48) if(!r[d]) {
    /* Retail item kind 2, parameter 0: large health with native drop physics,
     * lifetime, collection and refill. Keep aerial/off-screen kills reachable. */
    int x=b->death_x,y=b->death_y;
    int right=256+(r[0x1f7a]==4?MMX_RUSH_MAMMOTH_SCROLL:0)-16;
    if(x<16)x=16;if(x>right)x=right;
    if(y<16)y=16;if(y>184)y=184;
    memset(r+d,0,48);r[d]=1;r[d+10]=2;
    s->owner[(d-0xe68)/32]=0;
    put(r+d+5,s->camera_x+x);put(r+d+8,s->camera_y+y);
    return true;
  }
  return false;
}
void MmxBossRushFrame(uint8_t *r,uint16_t input) {
#if MMX_VARIANT_JP
  (void)r;(void)input;return;
#else
  MmxBossRushState s=MmxBossRushGetState();unsigned pressed=input&~s.input;s.input=input;
  if(s.mode==MMX_RUSH_PREPARING || s.mode==MMX_RUSH_PLAYING)
    for(unsigned i=0;i<4;++i) {
      /* Older Rush snapshots used $9C and could retain $90 after draining.
       * Preserve their remaining native units while removing invalid bits. */
      unsigned tank=r[0x1f83+i];
      if(tank&128)r[0x1f83+i]=(uint8_t)(0x80|((tank&15)>14?14:tank&15));
    }
  if(s.mode!=MMX_RUSH_OFF) MmxBossRushAudioTick();
  if(s.mode==MMX_RUSH_OFF) {
    if(!MmxBossRushEnabled() && !MmxVsEnabled()) {
      /* A launcher edit can disable the fourth row while it is selected.
       * Return the native cursor to Option Mode before its three-row table
       * runs again. Keep input history consistent with ordinary frames. */
      if(s.menu && title(r) && s.selection==3) {
        put(r+0xbb0,198);r[0x3c]=2;memset(r+0xac,0,4);
      }
      s.menu=0;s.selection=0;s.menu_input=input;MmxBossRushSetState(&s);return;
    }
    if(!title(r)) {s.menu=0;s.menu_input=input;MmxBossRushSetState(&s);return;}
    /* Old playtest snapshots may contain the removed mode submenu. */
    if(s.menu==2) s.selection=3;
    s.menu=1;
    if(s.selection<3) s.selection=(uint8_t)((word(r+0xbb0)-166)/16);
    unsigned previous=s.selection;
    unsigned last=MmxVsEnabled()?(MmxBossRushEnabled()?4:3):3;
    if(s.selection>last)s.selection=(uint8_t)last;
    if(pressed&SNES_PAD_UP) {
      if(previous==0) s.selection=(uint8_t)last;
      else if(previous>=3) s.selection=(uint8_t)(previous-1);
    } else if(pressed&(SNES_PAD_DOWN|SNES_PAD_SELECT)) {
      if(previous>=2)s.selection=(uint8_t)(previous==last?0:previous+1);
    }
    if(previous>=3 || s.selection>=3) {
      /* The native cursor table has three rows. Handle transitions touching
       * our fourth row here, and synchronize its index when leaving it. */
      memset(r+0xac,0,4);r[0x3c]=s.selection>=3?2:s.selection;
      put(r+0xbb0,s.selection==4?222:166+16*s.selection);
      if(s.selection>=3 && (pressed&(SNES_PAD_START|SNES_PAD_A|SNES_PAD_Y))) {
        bool versus=MmxVsEnabled() && (s.selection==4 || !MmxBossRushEnabled());
        MmxBossRushSetState(&s);load(r,!versus && MmxCoopEnabled());
        if(versus) {
          MmxVsStart();s=MmxBossRushGetState();s.versus=1;s.selection=(uint8_t)last;
          MmxBossRushSetState(&s);put(r+0xbb0,last==4?222:166+16*last);
          MmxVsState vs=MmxVsGetState();vs.input[0]=vs.release[0]=input;MmxVsSetState(&vs);
        }
        return;
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
  if(s.mode==MMX_RUSH_PREPARING) {camera(r);return;}
  camera(r);
  unsigned vs_phase=MmxVsGetState().phase;
  if(r[0xd3]==4 && !r[0x1f19] && !MmxWeaponsMenuVisible(r) && !MmxCoopGetState().menu_owner &&
      (!s.versus || (!r[0x1f10] && !r[0x1ec9] && vs_phase>=MMX_VS_LOADING && vs_phase<=MMX_VS_FIGHT))) {
    for(unsigned i=0;i<2;++i) {
      s=MmxBossRushGetState();MmxBossRushBoss *b=&s.bosses[i];
      if(b->phase==MMX_RUSH_DYING) {
        if(s.versus)continue;
        if(++b->ticks>=48) {
          for(unsigned d=0xe68;d<0x1d08;) {
            if(d==0x1228) {d=0x1428;continue;}
            unsigned n=(d-0xe68)/32;
            if(s.owner[n]==i+1) {memset(r+d,0,object_step(d));s.owner[n]=0;}
            d+=object_step(d);
          }
          /* Cleanup releases that boss's item slots first. If the shared
           * native pool is full, retry without losing or duplicating its drop. */
          if(!drop_health(r,&s,b)) {
            b->ticks=48;MmxBossRushSetState(&s);continue;
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
        int id=s.versus?MmxVsGetState().fighters[i].boss:MmxBossRushNextBoss();if(id<0) continue;
        if(!MmxBossRushAssign(i,(unsigned)id,(uint16_t)object)) continue;
        s=MmxBossRushGetState();
        for(unsigned n=0;n<sizeof(globals)/sizeof(*globals);++n)
          s.bosses[i].globals[n]=r[globals[n]];
        MmxBossRushSetState(&s);
        memset(r+object,0,64);r[object]=0x81;r[object+10]=kMmxBossRushBosses[id].kind;
        unsigned x=entry_x(r,i),y=s.camera_y+(id==4?43:96);
        put(r+object+5,x);put(r+object+8,y);put(r+object+0x22,x);put(r+object+0x24,y);
        if(s.versus)MmxVsSpawned(r,i);
      }
    }
  }
#endif
}
void MmxBossRushAfterFrame(uint8_t *r) {
  if(MmxBossRushGetState().mode==MMX_RUSH_PREPARING) {
    /* Door completion and native music upload release the encounter at the
     * next guest player-update boundary, including any upload yields. */
    return;
  }
  if(!playing()) return;
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned i=0;i<2;++i) {
    MmxBossRushBoss *b=&s.bosses[i];if(!b->phase) continue;
    if(b->id==4 && b->phase==MMX_RUSH_FIGHTING && r[b->object]) {
      /* Closed door tiles do not provide the tower's full boss-height walls.
       * Bound the body as well as its destinations, including old saves that
       * already put him behind a door. Native AI/animation continue unchanged. */
      unsigned left=s.camera_x+32;
      unsigned right=s.camera_x+256+(r[0x1f7a]==4?MMX_RUSH_MAMMOTH_SCROLL:0)-32;
      unsigned x=word(r+b->object+5);
      if(x<left)x=left;if(x>right)x=right;
      put(r+b->object+5,x);
      if(r[b->object+2]==4 && r[b->object+3]>=2 && r[b->object+3]<=4) {
        unsigned target=word(r+b->object+0x3a);
        if(target<left)target=left;if(target>right)target=right;
        put(r+b->object+0x3a,target);
      }
    }
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
  if(!s.versus && !alive && death_complete) MmxBossRushFinish();
  camera(r);put(r+0x1f0e,0);
}
