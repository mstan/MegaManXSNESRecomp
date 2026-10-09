/* Exercise the title entry and the original controllers in a real ROM.
 * Kept separate from the portable queue tests: no ROM fixtures are shipped. */
static void rush_keep_players_alive(void) {
  g_ram[0xbcf]=0xa0;
  if(MmxCoopEnabled()) {
    MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) c.players[i].body[0x27]=0xa0;
    MmxCoopSetState(&c);
  }
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
  frame(SNES_PAD_START);frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_LOADING,"Boss Rush starts directly without a mode submenu");
  check(MmxBossRushGetState().coop==(coop_assets!=NULL),"Boss Rush respects the configured co-op mod");
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) frame(0);
  fprintf(stderr,"rush load mode=%u stage=%u game=%u/%u/%u player=%u camera=%u/%u\n",
      MmxBossRushGetState().mode,g_ram[0x1f7a],g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],
      g_ram[0xba9],g_ram[0x1e4d]|g_ram[0x1e4e]<<8,g_ram[0x1e50]|g_ram[0x1e51]<<8);
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"title selection loads the universal arena");
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
  size_t baseline=RtlSaveSnapshotToMemory(start,cap);check(baseline!=0,"arena snapshot saves");
  unsigned before=g_ram[0xbad]|g_ram[0xbae]<<8;
  for(unsigned t=0;t<8;++t) frame(SNES_PAD_RIGHT|(coop_assets?SNES_PAD_LEFT<<12:0));
  check((g_ram[0xbad]|g_ram[0xbae]<<8)>before,"player moves while bosses enter");
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();
    check((c.players[1].body[5]|c.players[1].body[6]<<8)<96,"partner uses port 2 while bosses enter");
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
    for(unsigned t=0;t<600;++t) {frame(0);rush_keep_players_alive();}
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
    MmxCoopState c=MmxCoopGetState();c.players[0].body[0x27]=128;c.players[0].body[2]=12;
    if(c.current==0) {g_ram[0xbcf]=128;g_ram[0xbaa]=12;}
    MmxCoopSetState(&c);frame(0);
    check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"one co-op death leaves the run playing");
    c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) {c.players[i].body[0x27]=128;c.players[i].body[2]=12;}
    g_ram[0xbcf]=128;g_ram[0xbaa]=12;MmxCoopSetState(&c);frame(0);
  } else {g_ram[0xbcf]=0;frame(0);}
  check(MmxBossRushGetState().mode==MMX_RUSH_FINISHED,"last player death ends the run");
  check(MmxRendererSaveCapture("rush-results.cap"),"results capture saves");
  frame(0);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING && !MmxBossRushGetState().defeated,"Retry starts a new full run");
  MmxBossRushFinish();frame(0);frame(SNES_PAD_DOWN);frame(0);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t) frame(0);
  fprintf(stderr,"rush return mode=%u menu=%u pending=%u game=%u/%u/%u title=%u/%u xy=%u,%u\n",MmxBossRushGetState().mode,MmxBossRushGetState().menu,MmxBossRushGetState().return_title,g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0x38],g_ram[0x39],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8);
  check(MmxBossRushGetState().mode==MMX_RUSH_OFF && MmxBossRushGetState().menu==1,"results return to the native title menu");
  puts("MMX BOSS RUSH ROM CHECKS PASSED (28 distinct pairs)");
}
