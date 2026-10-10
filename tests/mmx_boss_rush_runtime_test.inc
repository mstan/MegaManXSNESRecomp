/* Exercise the title entry and the original controllers in a real ROM.
 * Kept separate from the portable queue tests: no ROM fixtures are shipped. */
#include "mod_audio.h"

static void rush_keep_players_alive(void) {
  g_ram[0xbcf]=0xa0;
  if(MmxCoopEnabled()) {
    MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) c.players[i].body[0x27]=0xa0;
    MmxCoopSetState(&c);
  }
}
static unsigned rush_death_sounds;
static void rush_death_frame(void) {
  unsigned head=g_ram[0xba3]&30;frame(0);
  for(unsigned end=g_ram[0xba3]&30;head!=end;head=(head+2)&30)
    rush_death_sounds+=g_ram[0xb72+head]==0x0a;
}
static void rush_natural_death_checks(void) {
  check(!MmxCoopEnabled(),"native Boss Rush fatal-contact fixture uses solo play");
  MmxBossRushState s=MmxBossRushGetState();
  uint8_t queue[8];
  const char *boss=getenv("MMX_BOSS_RUSH_DEATH_BOSS");
  unsigned first=boss?(unsigned)strtoul(boss,NULL,10):0;
  check(first<8,"fatal-contact fixture selects a valid native boss");
  queue[0]=(uint8_t)first;queue[1]=(uint8_t)(first==0?4:0);unsigned pos=2;
  for(unsigned i=0;i<8;++i) if(i!=queue[0] && i!=queue[1]) queue[pos++]=(uint8_t)i;
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"fatal-contact fixture queues the selected boss and a survivor");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
      "fatal-contact fixture completes both native entrances");
  rush_death_sounds=0;
  g_ram[0xbcf]=129;g_ram[0xc30]=g_ram[0xc32]=g_ram[0xbd8]=0;
  for(unsigned t=0;t<120 && (g_ram[0xbcf]&127);++t) {
    memcpy(g_ram+0xbad,g_ram+s.bosses[0].object+5,2);
    memcpy(g_ram+0xbb0,g_ram+s.bosses[0].object+8,2);
    rush_death_frame();
  }
  check(!(g_ram[0xbcf]&127),"native boss contact delivers the fatal hit");
  for(unsigned t=0;t<360 && MmxBossRushGetState().mode==MMX_RUSH_PLAYING;++t) {
    rush_death_frame();
    if(t%30==0) fprintf(stderr,"native rush death t=%u mode=%u hp=%u action=%u:%u sounds=%u flags=%u/%u/%u\n",
        t,MmxBossRushGetState().mode,g_ram[0xbcf],g_ram[0xbaa],g_ram[0xbab],rush_death_sounds,
        g_ram[0x1f13],g_ram[0x1f19],g_ram[0xc32]);
  }
  check(MmxBossRushGetState().mode==MMX_RUSH_FINISHED,"native fatal contact reaches Game Over without looping");
  check(rush_death_sounds==1,"native fatal contact plays X's death sound once");
  puts("MMX BOSS RUSH NATIVE FATAL CONTACT CHECKS PASSED");
}

static void rush_audio_checks(uint8_t *snapshot,size_t cap) {
  Apu *a=apu_init();apu_reset(a);unsigned entry=0;
  const uint8_t *rom=g_snes->cart->rom;
  static const uint8_t banks[]={0,1,2,0x42,0x47};
  for(unsigned bank=0;bank<sizeof(banks);++bank) {
    const uint8_t *ptr=rom+0x48000+banks[bank]*3;
    unsigned p=(9+ptr[2])*0x8000+((ptr[0]|ptr[1]<<8)&0x7fff);
    for(;;) {
      unsigned n=rom[p]|rom[p+1]<<8,d=rom[p+2]|rom[p+3]<<8;p+=4;
      if(!n) {if(!bank) entry=d;break;}
      memcpy(a->ram+d,rom+p,n);p+=n;
    }
  }
  a->romReadable=false;a->spc->pc=entry;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==1,"ROM-derived private SPC driver initializes");
  a->dsp->sampleRead=a->dsp->sampleWrite=0;
  uint32_t count,boomerang_count;const int16_t *effect=MmxBossRushAudioClip(0x51,&count);
  const int16_t *boomerang=MmxBossRushAudioClip(0x5a,&boomerang_count);
  check(effect && count>8000 && boomerang && boomerang_count>64000,
      "native boss and sustained boomerang clips are available");
  check(!MmxBossRushAudioClip(0x0c,NULL),"meter-fill sound retains its native driver");
  a->inPorts[1]=0;a->inPorts[0]=0x51;int peak=0;
  for(unsigned sample=0;sample<count;++sample) {
    for(unsigned i=0;i<32;++i) apu_cycle(a);
    int16_t pcm[2];if(dsp_getSamples(a->dsp,pcm,1)!=1) check(false,"private native SPC sample arrives");
    if(pcm[0]!=effect[sample*2] || pcm[1]!=effect[sample*2+1])
      check(false,"cached effect matches independent native SPC output");
    for(unsigned ch=0;ch<2;++ch) {int v=abs(pcm[ch]);if(v>peak) peak=v;}
  }
  check(peak>0 && a->outPorts[2]==2,"native boss effect renders audibly");
  puts("ok: cached effect matches the complete independent native SPC output");
  const uint8_t *scripts=a->ram+0x1c02;
  unsigned current=scripts[0x51*2]<<8|scripts[0x51*2+1];
  unsigned incoming=scripts[0x5a*2]<<8|scripts[0x5a*2+1];
  check(a->ram[current]>a->ram[incoming],"native boss sound outranks the boomerang effect");
  a->inPorts[0]=0x51;
  for(unsigned i=0;i<8000*32;++i) apu_cycle(a);
  a->inPorts[0]=0x5a;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==4 && a->ram[0x2a2]==a->ram[current] && (a->ram[0xc3]&4),
      "lower-priority native request acknowledges without replacing the boss sound");
  /* The substituted command acknowledges without stopping that active voice. */
  a->inPorts[0]=0xb1;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==5 && a->ram[0x2a2]==a->ram[current] && (a->ram[0xc3]&4),
      "null native effect preserves acknowledgements and the existing voice");
  apu_free(a);
  snes_mod_audio_stop_all();
  SNESModAudioClip first=snes_mod_audio_register_pcm_s16(effect,count,32040,2);
  SNESModAudioClip second=snes_mod_audio_register_pcm_s16(boomerang,boomerang_count,32040,2);
  SNESModAudioVoice one=snes_mod_audio_play_voice(first,100),two=snes_mod_audio_play_voice(second,100);
  check(one && two,"native boss and boomerang effects can play concurrently");
  int16_t mixed[1024]={0};
  RtlApuLock();snes_mod_audio_mix(mixed,512,32040,2);RtlApuUnlock();
  for(unsigned i=0;i<1024;++i) {
    int v=(int)effect[i]+boomerang[i];if(v>32767) v=32767;if(v< -32768) v=-32768;
    if(mixed[i]!=v) check(false,"independent effects mix without channel preemption");
  }
  size_t rb_cap=RtlRollbackSnapshotBound();uint8_t *rollback=malloc(rb_cap);
  check(rollback!=NULL,"local rollback fixture allocates");
  size_t n=RtlRollbackSaveToMemory(rollback,rb_cap);check(n!=0,"local rollback audio snapshot saves");
  uint32_t generation=snes_mod_audio_reset_generation();
  check(RtlRollbackLoadFromMemory(rollback,n),"local rollback audio snapshot restores");free(rollback);
  check(snes_mod_audio_reset_generation()==generation && snes_mod_audio_voice_active(one) &&
      snes_mod_audio_voice_active(two),"local rollback preserves active mod-audio voices");
  memset(mixed,0,sizeof(mixed));
  RtlApuLock();snes_mod_audio_mix(mixed,512,32040,2);RtlApuUnlock();
  for(unsigned i=0;i<1024;++i) {
    int v=(int)effect[1024+i]+boomerang[1024+i];if(v>32767) v=32767;if(v< -32768) v=-32768;
    if(mixed[i]!=v) check(false,"rollback preserves the audio playback position");
  }
  puts("ok: independent native effects and playback positions survive local rollback");
  n=RtlSaveSnapshotToMemory(snapshot,cap);check(n!=0,"ordinary audio snapshot saves");
  check(RtlLoadSnapshotFromMemory(snapshot,n),"ordinary audio snapshot restores");
  check(!snes_mod_audio_voice_active(one) && !snes_mod_audio_voice_active(two) &&
      snes_mod_audio_reset_generation()!=generation,"ordinary save-state load stops previous audio voices");
  snes_mod_audio_unregister(first);snes_mod_audio_unregister(second);
  MmxBossRushAudioState bad=MmxBossRushAudioGetState();bad.cues[0].command=0xfa;
  check(!MmxBossRushAudioSetState(&bad),"invalid audio control requests are rejected");
  puts("MMX BOSS RUSH NATIVE AUDIO CHECKS PASSED");
}

static void rush_projectile_defeat(unsigned seat,unsigned weapon) {
  MmxBossRushState s=MmxBossRushGetState();unsigned object=s.bosses[seat].object;
  unsigned count=s.defeated;g_ram[object+0x27]=1;
  g_ram[0xbdb]=(uint8_t)(weapon*2);
  for(unsigned t=0;t<360 && MmxBossRushGetState().defeated==count;++t) {
    /* Give a real native weapon projectile a guaranteed collision. Reducing
     * the fixture to 1 HP saves time; the finishing hit and death are guest code. */
    for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
      memcpy(g_ram+d+5,g_ram+object+5,2);memcpy(g_ram+d+8,g_ram+object+8,2);
      g_ram[d+4]=g_ram[d+7]=0;
    }
    if(MmxCoopEnabled()) MmxCoopCapture(g_ram);
    frame(t%12==0?SNES_PAD_Y:0);rush_keep_players_alive();
  }
  s=MmxBossRushGetState();
  fprintf(stderr,"rush lethal boss=%u count=%u native=%u:%u hp=%u gates=%u/%u/%u\n",
      s.bosses[seat].id,s.defeated,g_ram[object+1],g_ram[object+2],g_ram[object+0x27],
      g_ram[0xd3],g_ram[0x1f19],MmxCoopGetState().menu_owner);
  if(s.defeated==count) {
    fprintf(stderr,"lethal fixture player=%u:%u at=%u,%u weapon=%u input=%u flags=%u/%u/%u\n",
      g_ram[0xbaa],g_ram[0xbab],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,
      g_ram[0xbdb],g_ram[0xac],g_ram[0xc0c],g_ram[0xc16],g_ram[0x1f17]);
    for(unsigned d=0x1228;d<0x1428;d+=64)
      fprintf(stderr,"shot %x: %u:%u kind=%u at=%u,%u\n",d,g_ram[d],g_ram[d+1],g_ram[d+10],
        g_ram[d+5]|g_ram[d+6]<<8,g_ram[d+8]|g_ram[d+9]<<8);
  }
  check(s.defeated==count+1,"native weapon finishing hit counts exactly one defeat");
}
static void rush_runtime_checks(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  const char *coop_assets=getenv("MMX_BOSS_RUSH_COOP");
  const char *zero_assets=getenv("MMX_BOSS_RUSH_ZERO");
  if(zero_assets) {
    check(MmxZeroLoad(zero_assets),"Add Zero source assets load");MmxZeroRegisterHooks();
    MmxZeroState zero=MmxZeroGetState();zero.active_x=1;MmxZeroSetState(zero);
  }
  if(coop_assets) {
    check(MmxZeroLoad(coop_assets),"co-op source assets load");MmxZeroRegisterHooks();
    check(MmxCoopEnable(MMX_COOP_X),"co-op configuration starts as X");
    MmxCoopRegisterHooks();
  }
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t) {
    frame(t==200 || t==350 || (t>500 && t%180==0)?SNES_PAD_START:0);
  }
  check(MmxBossRushGetState().menu==1,"Boss Rush appears on the native title menu");
  zero_replay(180);
  for(unsigned n=0;n<3;++n) {frame(SNES_PAD_DOWN);zero_replay(12);}
  check(MmxBossRushGetState().selection==3,"fourth title option is navigable");
  check(MmxRendererSaveCapture("rush-title.cap"),"fourth title entry capture saves");
  frame(SNES_PAD_Y);frame(SNES_PAD_Y);
  check(MmxRendererSaveCapture("rush-confirm.cap"),"confirmation keeps the selected fourth row");
  fprintf(stderr,"rush confirm state=%u/%u/%u/%u script=%u xy=%u,%u action=%u:%u\n",g_ram[0x38],g_ram[0x39],g_ram[0x3b],g_ram[0x3c],g_ram[0xc01],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,g_ram[0xbaa],g_ram[0xbab]);
  check(g_ram[0xbab]==2 && (g_ram[0xbb0]|g_ram[0xbb1]<<8)==214,
      "native title confirmation shot runs on the Boss Rush row");
  check(MmxBossRushGetState().mode==MMX_RUSH_LOADING,"Boss Rush starts directly without a mode submenu");
  check(MmxBossRushGetState().coop==(coop_assets!=NULL),"Boss Rush respects the configured co-op mod");
  bool title_shot=false;
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) {
    frame(SNES_PAD_Y);
    if(!MmxBossRushGetState().stage_started) {
      if(t==12) check(MmxRendererSaveCapture("rush-title-shot.cap"),"native title shot capture saves");
      for(unsigned d=0x1228;d<0x1428;d+=64) title_shot|=g_ram[d]!=0;
    }
  }
  check(title_shot,"native title confirmation produces a projectile before the fade");
  fprintf(stderr,"rush load mode=%u stage=%u checkpoint=%u/%u game=%u/%u/%u player=%u action=%u:%u xy=%u,%u camera=%u/%u\n",
      MmxBossRushGetState().mode,g_ram[0x1f7a],g_ram[0x1f81],g_ram[0x1f82],g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],
      g_ram[0xba9],g_ram[0xbaa],g_ram[0xbab],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,
      g_ram[0x1e4d]|g_ram[0x1e4e]<<8,g_ram[0x1e50]|g_ram[0x1e51]<<8);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING,"title selection starts between Penguin's doors");
  check((g_ram[0xbad]|g_ram[0xbae]<<8)>=0x1d00 &&
      (g_ram[0xbad]|g_ram[0xbae]<<8)<0x1e00,"player arrives in the native preparation corridor");
  check(MmxRendererSaveCapture("rush-preparation.cap"),"preparation corridor capture saves");
  for(unsigned t=0;t<30;++t) frame(SNES_PAD_Y);
  for(unsigned d=0x1228;d<0x1428;d+=64) check(!g_ram[d],"held confirmation does not fire a weapon");
  zero_replay(180);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING &&
      !MmxBossRushGetState().bosses[0].phase && !MmxBossRushGetState().bosses[1].phase,
      "preparation waits for the second door before spawning bosses");
  check(g_ram[0x1f9a]==32 && g_ram[0x1f99]==255,"run starts fully upgraded");
  for(unsigned i=0;i<4;++i) check((g_ram[0x1f83+i]&127)==28,"shared subtank starts full");
  for(unsigned i=0;i<8;++i) check(g_ram[0x1f88+i*2]==0xdc,"X1 weapon starts owned and full");
  check(!(g_ram[0x1f7e]&128),"Hadouken is not acquired");
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();
    check(c.initialized && c.players[0].status==MMX_COOP_ALIVE && c.players[1].status==MMX_COOP_ALIVE,
        "both co-op players start alive");
    check((c.players[0].body[0x27]&127)==32 && (c.players[1].body[0x27]&127)==32,"both co-op players start at maximum health");
  }
  unsigned stage_bank=g_ram[0xfffd];bool native_door=false,music_replayed=false;
  for(unsigned t=0;t<1200 && MmxBossRushGetState().mode==MMX_RUSH_PREPARING;++t) {
    if(!music_replayed && MmxBossRushGetState().loaded==MMX_RUSH_MUSIC_PENDING) {
      size_t pending=RtlSaveSnapshotToMemory(expected,cap);
      check(pending!=0,"pending native music upload snapshot saves");
      frame(SNES_PAD_RIGHT);size_t en=RtlSaveSnapshotToMemory(actual,cap);
      check(RtlLoadSnapshotFromMemory(expected,pending),"pending native music upload restores");
      frame(SNES_PAD_RIGHT);size_t an=RtlSaveSnapshotToMemory(start,cap);
      same(actual,en,start,an,"pending native music upload replays deterministically");
      music_replayed=true;
    }
    frame(SNES_PAD_RIGHT);native_door|=g_ram[0xbaa]==0x18 || g_ram[0xbaa]==0x1a;
  }
  fprintf(stderr,"rush music bank before=%02x after=%02x door=%u\n",
      stage_bank,g_ram[0xfffd],native_door);
  fprintf(stderr,"rush door mode=%u camera=%u player=%u action=%u:%u lock=%u flags=%u/%u/%u section=%u\n",
      MmxBossRushGetState().mode,g_ram[0x1e4d]|g_ram[0x1e4e]<<8,
      g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbaa],g_ram[0xbab],g_ram[0xc16],
      g_ram[0x1f23],g_ram[0x1f26],g_ram[0x1f13],g_ram[0x1f08]);
  check(native_door,"second door runs the native player door controller");
  /* Derive the expected track from the retail Penguin health-fill controller,
   * independently of Rush's chosen command. A valid SPC upload alone would
   * also pass for the victory song, which is the wrong encounter music. */
  const uint8_t *rom=g_snes->cart->rom;
  static const uint8_t music_call[]={0x22,0xa2,0x87,0x80};
  check(rom[0xb617]==0xa9 && !memcmp(rom+0xb619,music_call,sizeof(music_call)),
      "retail Penguin entrance supplies a native music command");
  unsigned boss_song=rom[0xb618];
  unsigned boss_bank=rom[0x3007b+rom[0x3006b+boss_song]];
  fprintf(stderr,"retail Penguin battle command=%02x bank=%02x\n",boss_song,boss_bank);
  check(g_ram[0xfffd]==boss_bank && g_ram[0xfffd]!=stage_bank,
      "native boss music bank replaces the stage bank");
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"crossing the second door starts the encounter");
  if(getenv("MMX_BOSS_RUSH_AUDIO_TEST")) {rush_audio_checks(start,cap);return;}
  if(getenv("MMX_BOSS_RUSH_DEATH_TEST")) {rush_natural_death_checks();return;}
  size_t baseline=RtlSaveSnapshotToMemory(start,cap);check(baseline!=0,"arena snapshot saves");
  unsigned before=g_ram[0xbad]|g_ram[0xbae]<<8;
  MmxCoopState before_coop=MmxCoopGetState();
  unsigned partner_before=before_coop.players[1].body[5]|before_coop.players[1].body[6]<<8;
  for(unsigned t=0;t<8;++t) frame(SNES_PAD_RIGHT|(coop_assets?SNES_PAD_LEFT<<12:0));
  check((g_ram[0xbad]|g_ram[0xbae]<<8)>before,"player moves while bosses enter");
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();
    check((c.players[1].body[5]|c.players[1].body[6]<<8)<partner_before,"partner uses port 2 while bosses enter");
  }
  check(RtlLoadSnapshotFromMemory(start,baseline),"restore arena after input check");
  for(unsigned a=0;a<8;++a) for(unsigned b=a+1;b<8;++b) {
    check(RtlLoadSnapshotFromMemory(start,baseline),"restore empty arena for boss pair");
    MmxBossRushState s=MmxBossRushGetState();
    memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
    s.queued=8;for(unsigned i=0;i<8;++i) s.queue[i]=(uint8_t)i;
    unsigned pos=2;s.queue[0]=(uint8_t)a;s.queue[1]=(uint8_t)b;
    for(unsigned i=0;i<8;++i) if(i!=a && i!=b) s.queue[pos++]=(uint8_t)i;
    check(MmxBossRushSetState(&s),"test pair queue installs");
    memset(g_ram+0xe68,0,0x3c0);
    for(unsigned t=0;t<1500;++t) {
      frame(0);s=MmxBossRushGetState();
      rush_keep_players_alive(); /* Isolate controller compatibility from survival. */
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
    }
    fprintf(stderr,"rush pair %u/%u phases=%u/%u hp=%u/%u native=%u:%u/%u:%u xy=%u,%u/%u,%u\n",
      a,b,s.bosses[0].phase,s.bosses[1].phase,s.bosses[0].health,s.bosses[1].health,
      g_ram[s.bosses[0].object+1],g_ram[s.bosses[0].object+2],g_ram[s.bosses[1].object+1],g_ram[s.bosses[1].object+2],
      g_ram[s.bosses[0].object+5]|g_ram[s.bosses[0].object+6]<<8,g_ram[s.bosses[0].object+8]|g_ram[s.bosses[0].object+9]<<8,
      g_ram[s.bosses[1].object+5]|g_ram[s.bosses[1].object+6]<<8,g_ram[s.bosses[1].object+8]|g_ram[s.bosses[1].object+9]<<8);
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,"both native bosses complete their own entrances");
    check(s.bosses[0].health==32 && s.bosses[1].health==32,"both boss meters fill independently");
    char capture[40];snprintf(capture,sizeof(capture),"rush-pair-%u-%u.cap",a,b);
    check(MmxRendererSaveCapture(capture),"native boss pair capture saves");
    if(a==0 && b==4) {
      size_t pair_snapshot=RtlSaveSnapshotToMemory(expected,cap);
      frame(SNES_PAD_START);zero_replay(90);
      check(MmxRendererSaveCapture("rush-weapon-pause.cap"),"native weapon pause capture saves");
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore combat after pause capture");
      for(unsigned seat=0;seat<2;++seat) {
        check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore weakness finishing-hit fixture");
        uint32_t generation=MmxBossRushGetState().bosses[seat].generation;
        rush_projectile_defeat(seat,seat?1:4);
        for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
        check(MmxBossRushGetState().bosses[seat].phase && MmxBossRushGetState().defeated==1 &&
            MmxBossRushGetState().bosses[seat].generation>generation,
            "native weakness finishing hit replaces its boss");
      }
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore boss pair after native finishing hits");
      s=MmxBossRushGetState();s.bosses[0].phase=MMX_RUSH_ENTERING;MmxBossRushSetState(&s);
      g_ram[s.bosses[0].object+1]=8;MmxBossRushAfterFrame(g_ram);
      check(MmxBossRushGetState().bosses[0].phase==MMX_RUSH_FIGHTING,
          "entrance completion followed by a hit reaction still enters combat");
      s=MmxBossRushGetState();s.bosses[0].phase=MMX_RUSH_ENTERING;MmxBossRushSetState(&s);
      g_ram[s.bosses[0].object+1]=6;g_ram[s.bosses[0].object+0x27]=0;MmxBossRushAfterFrame(g_ram);
      check(MmxBossRushGetState().defeated==1,"death after an unobserved combat state still counts once");
      for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
      fprintf(stderr,"rush transition cleanup phase=%u ticks=%u actor=%u object=%x S=%x gates=%u/%u/%u\n",
          MmxBossRushGetState().bosses[0].phase,MmxBossRushGetState().bosses[0].ticks,
          MmxBossRushGetState().actor,MmxBossRushGetState().actor_object,MmxBossRushGetState().actor_s,
          g_ram[0xd3],g_ram[0x1f19],MmxCoopGetState().menu_owner);
      check(MmxBossRushGetState().bosses[0].phase!=MMX_RUSH_DYING,
          "death after an unobserved combat state advances the queue");
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore boss pair after entrance regression");
    }
    if(!a && b==1) {
      for(unsigned t=0;t<180;++t) {frame(t%12==0?SNES_PAD_Y:0);rush_keep_players_alive();}
      s=MmxBossRushGetState();
      check(s.bosses[0].health<32 || s.bosses[1].health<32,"native buster damages a fighting boss");
    }
    int audio_peak=0;
    for(unsigned t=0;t<600;++t) {
      frame(0);rush_keep_players_alive();
      if(a==0 && b==4) {
        /* Consume an audio callback's worth each frame. The last gameplay
         * frame need not contain an active effect, especially after a
         * boomerang's owning object has retired. */
        int16_t pcm[1068]={0};
        RtlApuLock();snes_mod_audio_mix(pcm,534,32040,2);RtlApuUnlock();
        for(unsigned i=0;i<1068;++i) if(abs(pcm[i])>audio_peak) audio_peak=abs(pcm[i]);
      }
    }
    if(a==0 && b==4) {
      check(MmxBossRushAudioGetState().sequence>0,"native boss updates emit snapshot-owned sound cues");
      check(audio_peak>0,"committed native boss cues reach the independent audio mixer");
    }
    size_t n=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(expected,n),"two-boss combat restores");
    frame(0);size_t en=RtlSaveSnapshotToMemory(actual,cap);
    check(RtlLoadSnapshotFromMemory(expected,n),"two-boss combat restores again");
    frame(0);size_t an=RtlSaveSnapshotToMemory(expected,cap);
    same(actual,en,expected,an,"boss pair replays deterministically");
    s=MmxBossRushGetState();unsigned survivor=s.bosses[1].object,id=s.bosses[1].id;
    uint32_t generation=s.bosses[0].generation;
    g_ram[s.bosses[0].object+0x27]=0;frame(0);
    check(MmxBossRushGetState().defeated==1,"one defeat increments the counter once");
    for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
    s=MmxBossRushGetState();
    check(s.bosses[1].object==survivor && s.bosses[1].id==id && g_ram[survivor],"replacement preserves the surviving boss");
    check(s.bosses[0].phase && s.bosses[0].phase!=MMX_RUSH_DYING &&
        s.bosses[0].generation>generation && s.bosses[0].id!=id && s.defeated==1,
        "replacement is a new distinct boss and death counts once");
  }
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();c.players[0].body[0x27]=128;c.players[0].body[2]=12;c.players[0].body[3]=0;
    if(c.current==0) {g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;}
    MmxCoopSetState(&c);frame(0);
    check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"one co-op death leaves the run playing");
    c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) {c.players[i].body[0x27]=128;c.players[i].body[2]=12;c.players[i].body[3]=0;}
    g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;MmxCoopSetState(&c);frame(0);
  } else {g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;frame(0);}
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"fatal damage lets the native death controller run");
  zero_replay(40);
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"results wait while native death orbs animate");
  check(MmxRendererSaveCapture("rush-player-death.cap"),"native player death capture saves");
  for(unsigned t=0;t<300 && MmxBossRushGetState().mode==MMX_RUSH_PLAYING;++t) frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_FINISHED,"last player death ends the run");
  check(MmxRendererSaveCapture("rush-results.cap"),"results capture saves");
  frame(0);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING && !MmxBossRushGetState().defeated,"Retry returns to preparation with a new full run");
  MmxBossRushFinish();frame(0);frame(SNES_PAD_DOWN);frame(0);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t) frame(0);
  fprintf(stderr,"rush return mode=%u menu=%u pending=%u game=%u/%u/%u title=%u/%u xy=%u,%u\n",MmxBossRushGetState().mode,MmxBossRushGetState().menu,MmxBossRushGetState().return_title,g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0x38],g_ram[0x39],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8);
  check(MmxBossRushGetState().mode==MMX_RUSH_OFF && MmxBossRushGetState().menu==1,"results return to the native title menu");
  puts("MMX BOSS RUSH ROM CHECKS PASSED (28 distinct pairs)");
}
