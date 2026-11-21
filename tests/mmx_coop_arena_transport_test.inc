/* Cold native stage entry and traversal placement are prerequisites only. */
static void coop_arena_transport_checks(const char *assets) {
  coop_replay_start(assets);MmxZeroSetModern(false);
  check(MmxCoopEnable(MMX_COOP_ZERO),"arena transport selects original X3 Zero P1 and X P2");
  for(unsigned t=0;t<4000;++t) {
    g_ram[0x1f9a]=32;
    unsigned input=(t>=1400 && t<1430) || (t>=1611 && t<1641) ||
        (t>=1822 && t<1852) ? SNES_PAD_START : 0;
    coop_frames(1,input,0);
  }
  check(g_ram[0x1f7a]==0 && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4,
      "ordinary cold boot initializes the native Highway stage");
  g_config.widescreen=false;g_mmx_custom_renderer=true;
  g_mmx_custom_view=(MmxRenderView){256,0,4.0/3.0};
  g_ram[0x1f7a]=6;g_ram[0x1f7b]=0;g_ram[0xd3]=g_ram[0xd4]=0;
  coop_frames(1200,0,0);
  check(g_ram[0x1f7a]==6 && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4,
      "normal phase0 initializes the native Spark Mandrill stage");
  MmxCoopState s=MmxCoopGetState();
  fprintf(stderr,"arena native start player=%u/%u camera=%u/%u HP=%u\n",coop_x(&s.players[0]),coop_y(&s.players[0]),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),s.players[0].body[0x27]&127);
  check(coop_place_fixture_partner(3004,863),"native upper-ledge partner context is placed");
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);unsigned x=seat?3004:3030,y=seat?863:943;
    eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbca,x);
    eagle_putword(g_ram+0xbb0,y);eagle_putword(g_ram+0xbcc,y);
    g_ram[0xbaa]=g_ram[0xbab]=0;memset(g_ram+0xbc2,0,4);
    g_ram[0xbcf]=32;g_ram[0xc13]=240;g_ram[0xc32]=0;
  }
  MmxCoopSelect(g_ram,0);
  /* Let native scrolling/room producers catch up to the traversal warp.
   * Observe the genuine spawn before any explicit record rescan. */
  unsigned boss=0;
  for(unsigned t=0;t<400;++t) {
    coop_frames(1,0,0);
    for(unsigned d=0xe68;d<0x1228;d+=64)if(g_ram[d] && g_ram[d+10]==3)boss=d;
  }
  fprintf(stderr,"approach prerequisite camera=%u/%u player=%u/%u partner-status=%u\n",eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),eagle_word(g_ram+0xbad),eagle_word(g_ram+0xbb0),MmxCoopGetState().players[1].status);
  check(eagle_word(g_ram+0x1e4d)>=2700,"native camera finishes traversal-warp catch-up before the region approach");
  /* The floor warp skipped region6: its actual native rectangle is
   * x[2848,2895), y[880,912). Produce the preceding authored records, then
   * enter that falling-player rectangle and let the normal event execute.
   * Never write the camera limits or an event-success flag directly. */
  {
    uint8_t scratch[32];memcpy(scratch,g_ram,sizeof(scratch));
    CpuState base=g_cpu;base.D=0;base.PB=0;base.DB=0x7e;base.P=0x20;base.emulation=0;cpu_p_to_mirrors(&base);
    for(unsigned column=2816;column<=2912;column+=32) {
      CpuState spawn=base;eagle_putword(g_ram,column);eagle_putword(g_ram+2,752);eagle_putword(g_ram+4,0x120);
      check(cpu_dispatch_call_pc(&spawn,0x00dcdb,0x00dc8f)==RECOMP_RETURN_NORMAL && spawn.S==base.S,
          "normal native producer loads the actual preceding region records");
    }
    memcpy(g_ram,scratch,sizeof(scratch));
    MmxCoopSelect(g_ram,0);
    eagle_putword(g_ram+0xbad,2872);eagle_putword(g_ram+0xbca,2872);
    eagle_putword(g_ram+0xbb0,890);eagle_putword(g_ram+0xbcc,890);
    g_ram[0xbaa]=g_ram[0xbab]=0;memset(g_ram+0xbc2,0,4);
    coop_frames(10,0,0);
    fprintf(stderr,"native falling-region result right-bound=%u\n",eagle_word(g_ram+0x1e60));
    check(eagle_word(g_ram+0x1e60)==5376,"actual native falling-region event releases the authored camera limit");
    MmxCoopSelect(g_ram,0);
    eagle_putword(g_ram+0xbb0,943);eagle_putword(g_ram+0xbcc,943);
  }
  fprintf(stderr,"pre-approach Slimer slot=%04x (zero means not yet produced)\n",boss);
  bool gathered=false,returned=false,controls=false;
  for(unsigned t=0;t<900;++t) {
    for(unsigned character=0;character<2;++character) {
      uint8_t *body=MmxCoopCharacterBody(g_ram,character);
      if(body){body[0x27]=32;body[0x6b]=240;body[0x8a]=0;}
    }
    if(!controls && g_ram[0xc16] && eagle_word(g_ram+0x1e5e)==3072 && eagle_word(g_ram+0x1e60)==3072) {
      const size_t cap=2u*1024u*1024u;uint8_t *snapshot=malloc(cap);check(snapshot!=NULL,"arena control snapshot allocates");
      size_t n=RtlSaveSnapshotToMemory(snapshot,cap);check(n>0,"actual native arena lock snapshots");
      eagle_putword(g_ram+0x1e60,3328);MmxCoopFrameTick(g_ram);
      check(!MmxCoopGetState().scene_owner,"ordinary non-arena body lock does not gather a partner");
      check(RtlLoadSnapshotFromMemory(snapshot,n),"restore native arena entry after open-room control");
      s=MmxCoopGetState();unsigned other=s.anchor^1;eagle_putword(s.players[other].body+5,3200);MmxCoopSetState(&s);MmxCoopFrameTick(g_ram);
      check(!MmxCoopGetState().scene_owner,"a living partner already inside the sealed arena stays independent");
      check(RtlLoadSnapshotFromMemory(snapshot,n),"restore native arena entry after inside control");
      s=MmxCoopGetState();s.players[other].status=MMX_COOP_FALLEN;s.players[other].body[0x27]=0;MmxCoopSetState(&s);MmxCoopFrameTick(g_ram);
      check(!MmxCoopGetState().scene_owner,"sealed arena does not gather a dead partner");
      check(RtlLoadSnapshotFromMemory(snapshot,n),"restore native arena entry after dead-seat control");free(snapshot);controls=true;
    }
    coop_frames(1,SNES_PAD_RIGHT,SNES_PAD_RIGHT);s=MmxCoopGetState();
    if(s.scene_owner)gathered=true;
    if(gathered && !s.scene_owner && s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE && coop_x(&s.players[1])>=3072) {returned=true;break;}
    for(unsigned d=0xe68;d<0x1228;d+=64)if(g_ram[d] && g_ram[d+10]==3)boss=d;
    if(t%30==0) {
      fprintf(stderr,"arena t=%u xy=%u/%u,%u/%u action=%02x/%02x hp=%u/%u scene=%u:%u camera=%u/%u boss=%02x:%02x:%02x hp=%u C16=%02x\n",t,coop_x(&s.players[0]),coop_y(&s.players[0]),coop_x(&s.players[1]),coop_y(&s.players[1]),s.players[0].body[2],s.players[1].body[2],s.players[0].body[0x27]&127,s.players[1].body[0x27]&127,s.scene_owner,s.scene_phase,eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),g_ram[boss],g_ram[boss+1],g_ram[boss+2],g_ram[boss+0x27],g_ram[0xc16]);
      fprintf(stderr,"native room words:");for(unsigned p=0x1e54;p<=0x1e6e;p+=2)fprintf(stderr," %04x=%u",p,eagle_word(g_ram+p));fprintf(stderr,"\n");
    }
  }
  check(boss!=0,"native approach produces the actual Thunder Slimer encounter");
  check(controls,"actual native intro exercises the selective gather controls");
  check(gathered && returned,"outside partner completes transport into the native sealed arena");
  s=MmxCoopGetState();unsigned partner_x=coop_x(&s.players[1]),owner_x=coop_x(&s.players[0]);
  coop_frames(20,0,SNES_PAD_RIGHT);s=MmxCoopGetState();
  check(coop_x(&s.players[1])>partner_x+10 && abs((int)coop_x(&s.players[0])-(int)owner_x)<=2,
      "returned partner resumes independent movement inside the native arena");
  const size_t cap=2u*1024u*1024u;uint8_t *start=malloc(cap),*expected=malloc(cap),*actual=malloc(cap);
  check(start && expected && actual,"arena continuation buffers allocate");
  size_t n=RtlSaveSnapshotToMemory(start,cap);coop_frames(20,SNES_PAD_LEFT,0);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"current-build native arena state loads");
  coop_frames(20,SNES_PAD_LEFT,0);
  same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"arena continuation repeats exactly after load");
  g_config.widescreen=true;g_mmx_custom_aspect=MMX_ASPECT_ADAPTIVE;
  g_mmx_custom_view=MmxRendererViewport(MMX_ASPECT_ADAPTIVE,1280,720,kSnesDisplayAspect_Crt4x3);frame(0);
  check(g_mmx_custom_view.width==342,"native arena field uses adaptive Fit geometry");
  uint32_t *picture=malloc(342u*224u*4u);check(picture!=NULL,"adaptive arena picture allocates");memcpy(actual,g_ram,0x20000);
  check(MmxRendererDraw(picture,g_mmx_custom_view,false) && !memcmp(actual,g_ram,0x20000),"adaptive arena drawing preserves native gameplay memory");free(picture);
  free(start);free(expected);free(actual);
  owner_wide_image("sealed-arena-return");
  puts("NATIVE SEALED ARENA TRANSFER CHECKS PASSED");
}
