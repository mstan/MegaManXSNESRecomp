/* Cold native setup and traversal cheats establish the encounter only.
 * Boarding, native dismount, both door scripts and the return stay native. */
static void coop_armor_transport_checks(const char *assets) {
  unsigned pilot=getenv("MMX_COOP_ARMOR_PILOT_P1")?0:1,entrant=pilot^1;
  bool empty_control=getenv("MMX_COOP_ARMOR_EMPTY_CONTROL")!=NULL;
  check(assets!=NULL,"armor transport requires owned X3 assets");
  coop_replay_start(assets);check(MmxCoopEnable(pilot?MMX_COOP_ZERO:MMX_COOP_X),"armor transport selects X pilot and Zero entrant");
  for(unsigned t=0;t<4000;++t) {
    g_ram[0x1f9a]=32;
    unsigned input=(t>=1400 && t<1430) || (t>=1611 && t<1641) ||
        (t>=1822 && t<1852) ? SNES_PAD_START : 0;
    coop_frames(1,input,0);
  }
  check(g_ram[0x1f7a]==0 && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4,
      "ordinary cold boot initializes the real Highway stage first");
  /* $80:997B dispatches phase0 through the complete retail level setup.
   * A stage-number write during New Game can be overwritten before that
   * same call loads its map, leaving only the caption changed next tick. */
  g_ram[0x1f7a]=2;g_ram[0x1f7b]=0;g_ram[0xd3]=g_ram[0xd4]=0;
  coop_frames(1200,0,0);
  check(g_ram[0x1f7a]==2 && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4,
      "cold stage access reaches native Sting Chameleon gameplay");
  /* The armor spawns in the forest and must be boarded there. Placing the
   * camera at the later doorway cannot produce an armor that was driven
   * there. The guest owns allocation, boarding and both door results. */
  check(coop_place_fixture_partner(4732,415),"forest fixture places its live partner");
  MmxCoopPlayer live_walker=MmxCoopGetState().players[entrant];
  for(unsigned seat=0;seat<2;++seat) {
    check(MmxCoopSelect(g_ram,seat),"late forest fixture teleports each seat");
    eagle_putword(g_ram+0xbad,4700+seat*32);eagle_putword(g_ram+0xbca,4700+seat*32);
    eagle_putword(g_ram+0xbb0,415);eagle_putword(g_ram+0xbcc,415);
    g_ram[0xbcf]=32;g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbaa]=g_ram[0xbab]=0;
  }
  MmxCoopSelect(g_ram,0);eagle_putword(g_ram+0x1e4d,4643);eagle_putword(g_ram+0x1e50,272);
  eagle_putword(g_ram+0x1e5a,256);eagle_putword(g_ram+0x1e5c,272);
  eagle_putword(g_ram+0x1e68,256);eagle_putword(g_ram+0x1e6e,272);
  /* Reuse DCDB's actual ROM-record producer, as the existing capsule pass
   * does after a camera jump: all allocation, live flags and initialization
   * stay native. No armor actor or boarding outcome is manufactured. */
  {
    uint8_t scratch[32];memcpy(scratch,g_ram,sizeof(scratch));
    CpuState base=g_cpu;base.D=0;base.PB=0;base.DB=0x7e;
    base.P=0x20;base.emulation=0;cpu_p_to_mirrors(&base);
    for(unsigned column=4480;column<=4896;column+=32) {
      CpuState spawn=base;
      eagle_putword(g_ram,column);eagle_putword(g_ram+2,240);eagle_putword(g_ram+4,0x120);
      (void)cpu_dispatch_call_pc(&spawn,0x00dcdbu,0x00dc8fu);
      check(spawn.S==base.S,"native record producer returns with its caller stack");
    }
    memcpy(g_ram,scratch,sizeof(scratch));
  }
  coop_frames(240,0,0);
  fprintf(stderr,"armor native spawn=%02x:%02x:%02x flags=%02x pos=%u/%u camera=%u/%u\n",
      g_ram[0xe18],g_ram[0xe19],g_ram[0xe1a],g_ram[0xe22],eagle_word(g_ram+0xe1d),eagle_word(g_ram+0xe20),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50));
  check(g_ram[0xe18] && !(g_ram[0xe22]&0x40),"native stage spawning supplies a real empty Ride Armor");
  unsigned ax=eagle_word(g_ram+0xe1d),ay=eagle_word(g_ram+0xe20);
  check(coop_place_fixture_partner(ax,ay-64),"boarding probe places X above the native armor");
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);unsigned x=seat==pilot?ax:ax-90,y=seat==pilot?ay-64:ay-16;
    eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbca,x);
    eagle_putword(g_ram+0xbb0,y);eagle_putword(g_ram+0xbcc,y);
    g_ram[0xbaa]=8;g_ram[0xbab]=2;g_ram[0xbd3]=g_ram[0xbd4]=0;
    g_ram[0xbd8]=g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbcf]=32;eagle_putword(g_ram+0xbc4,0xfd00);
  }
  MmxCoopSelect(g_ram,0);
  unsigned boarded=0;
  for(unsigned t=0;t<90 && !boarded;++t) {
    coop_frames(1,pilot==0 && t<30?SNES_PAD_RIGHT:0,pilot==1 && t<30?SNES_PAD_RIGHT:0);MmxCoopState s=MmxCoopGetState();
    if((g_ram[0xe22]&0x40) && s.players[pilot].body[2]==0x2c)boarded=t+1;
  }
  fprintf(stderr,"armor boarded=%u anchor=%u pilot=%02x\n",boarded,MmxCoopGetState().anchor,MmxCoopGetState().players[pilot].body[2]);
  check(boarded,"chosen X seat boards the real native Ride Armor before the door scenario");
  coop_frames(120,0,0);
  if(empty_control) {
    check(g_ram[0xe1a]==2,"ordinary armor boarding animation has finished");
    coop_frames(2,pilot==0?SNES_PAD_UP:0,pilot==1?SNES_PAD_UP:0);
    coop_frames(2,pilot==0?SNES_PAD_UP|SNES_PAD_B:0,pilot==1?SNES_PAD_UP|SNES_PAD_B:0);
    coop_frames(50,0,0);
    check(!(g_ram[0xe22]&0x40),"ordinary native Up plus Jump unboards before the door control");
  }
  MmxCoopState s=MmxCoopGetState();
  /* Rescuing the unmounted Zero is a fixture prerequisite, not a death/respawn result. */
  s.players[entrant]=live_walker;s.players[entrant].status=MMX_COOP_ALIVE;
  s.players[entrant].body[2]=0;s.players[entrant].body[3]=2;s.players[entrant].body[0x27]=32;
  s.players[entrant].zero.swap_phase=0;s.solo_death[entrant]=0;MmxCoopSetState(&s);
  int pilot_dx=(int)coop_x(&s.players[pilot])-eagle_word(g_ram+0xe1d);
  int pilot_dy=(int)coop_y(&s.players[pilot])-eagle_word(g_ram+0xe20);
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);unsigned x=seat==pilot?7552+pilot_dx:7636,y=seat==pilot?398+pilot_dy:415;
    eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbca,x);
    eagle_putword(g_ram+0xbb0,y);eagle_putword(g_ram+0xbcc,y);
    g_ram[0xbcf]=32;
    g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=240;
  }
  MmxCoopSelect(g_ram,pilot);eagle_putword(g_ram+0xe1d,7552);eagle_putword(g_ram+0xe20,398);
  eagle_putword(g_ram+0x1e4d,7424);eagle_putword(g_ram+0x1e50,272);
  {
    uint8_t scratch[32];memcpy(scratch,g_ram,sizeof(scratch));
    CpuState base=g_cpu;base.D=0;base.PB=0;base.DB=0x7e;base.P=0x20;
    base.emulation=0;cpu_p_to_mirrors(&base);
    for(unsigned column=7424;column<=7680;column+=32) {
      CpuState spawn=base;
      eagle_putword(g_ram,column);eagle_putword(g_ram+2,240);eagle_putword(g_ram+4,0x120);
      (void)cpu_dispatch_call_pc(&spawn,0x00dcdbu,0x00dc8fu);
      check(spawn.S==base.S,"first door record producer preserves its stack");
    }
    memcpy(g_ram,scratch,sizeof(scratch));
  }
  bool entered=false,probed=false;unsigned previous=0,departures=0,returns=0;
  for(unsigned t=0;t<1800;++t) {
    coop_frames(1,entrant==0?SNES_PAD_RIGHT:0,entrant==1?SNES_PAD_RIGHT:0);s=MmxCoopGetState();
    if(t%60==0)fprintf(stderr,"armor door t=%u owner=%u anchor=%u phase=%u body=%02x/%02x x=%u/%u camera=%u/%u armor=%02x:%02x boss=%02x:%02x HP=%02x tick=%02x\n",t,s.scene_owner,s.anchor,s.scene_phase,s.players[0].body[2],s.players[1].body[2],coop_x(&s.players[0]),coop_x(&s.players[1]),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),g_ram[0xe18],g_ram[0xe22],g_ram[0xe69],g_ram[0xe6a],g_ram[0xe8f],g_ram[0xe9c]);
    if(s.scene_phase==1 && !previous)++departures;
    if(!s.scene_phase && previous==3)++returns;
    if(s.scene_owner && !probed) {
      entered=true;
      fprintf(stderr,"native door entered owner=%u anchor=%u pilot=%02x armorflags=%02x\n",s.scene_owner,s.anchor,s.players[pilot].body[2],g_ram[0xe22]);
      const size_t cap=2u*1024u*1024u;uint8_t *snapshot=malloc(cap);
      check(snapshot!=NULL,"current native door snapshot allocates");size_t n=RtlSaveSnapshotToMemory(snapshot,cap);
      MmxCoopFrameTick(g_ram);s=MmxCoopGetState();
      check(s.scene_owner==entrant+1 && s.anchor==entrant,"scripted door retains its entrant while the partner pilots");
      check(RtlLoadSnapshotFromMemory(snapshot,n),"restore the native boundary after the next-tick ownership control");
      check(g_ram[0xe18] && !(g_ram[0xe22]&0x40) && g_ram[0xe3f]==16 &&
          (empty_control || (g_ram[0xe1a]==0x16 && g_ram[0xe1b]==1 && MmxCoopGetState().players[pilot].body[2]==6)),
          "transport completes native unboarding with phase, pilot, HP and armor lifetime intact");
      uint8_t *expected=malloc(cap),*actual=malloc(cap);
      check(expected && actual,"armor transport continuation buffers allocate");
      coop_frames(20,entrant==0?SNES_PAD_RIGHT:0,entrant==1?SNES_PAD_RIGHT:0);size_t en=RtlSaveSnapshotToMemory(expected,cap);
      check(RtlLoadSnapshotFromMemory(snapshot,n),"current-build pilot departure state restores");
      coop_frames(20,entrant==0?SNES_PAD_RIGHT:0,entrant==1?SNES_PAD_RIGHT:0);
      same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"native armor departure and door continue exactly after load");
      check(RtlLoadSnapshotFromMemory(snapshot,n),"restore original door boundary after its repeat control");
      free(expected);free(actual);free(snapshot);s=MmxCoopGetState();probed=true;
    }
    previous=s.scene_phase;
    if(departures==2 && returns==2 && !s.scene_owner && !g_ram[0xc16] && !g_ram[0x1f0c] && !g_ram[0x1f23])break;
  }
  check(entered,"the unmounted entrant opens the real native boss door");
  fprintf(stderr,"armor door complete departures=%u returns=%u owner=%u mode=%u/%u/%u hp=%u/%u armor=%02x:%02x\n",departures,returns,s.scene_owner,g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],s.players[0].body[0x27]&127,s.players[1].body[0x27]&127,g_ram[0xe18],g_ram[0xe22]);
  check(departures==2 && returns==2 && !s.scene_owner,"both native doors finish and return their partner");
}
