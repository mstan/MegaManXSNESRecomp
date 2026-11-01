static void diagnostic_restore_current_frame(void) {
  size_t cap=RtlRollbackSnapshotBound();void *bytes=malloc(cap);
  size_t size=RtlRollbackSaveToMemory(bytes,cap);
  check(size && RtlRollbackLoadFromMemory(bytes,size),"same-frame rollback restores");free(bytes);
}
static void coop_p2_armor_checks(const char *assets,const char *fixture) {
  coop_replay_start(assets);MmxCoopDisable();
  check(fixture && RtlLoadSnapshot(fixture),"upgraded native stage checkpoint loads");
  check((g_ram[0x1f99]&15)==15,"checkpoint has all four armor upgrades");
  check(MmxCoopEnable(MMX_COOP_ZERO),"P1 Zero / P2 X roster enables");
  coop_frames(8,0,0);
  MmxCoopState s=MmxCoopGetState();
  check(coop_place_fixture_partner(coop_x(&s.players[0])+32,coop_y(&s.players[0])),"P2 X joins");
  coop_frames(40,0,0);s=MmxCoopGetState();
  fprintf(stderr,"P2 ARMOR group=%02x parts=%02x/%02x/%02x upgrades=%02x\n",
      s.players[1].body[0x16],s.players[1].auxiliaries[0],s.players[1].auxiliaries[0x20],
      s.players[1].auxiliaries[0x40],g_ram[0x1f99]);
  check(s.players[1].auxiliaries[0] && s.players[1].auxiliaries[0x20] && s.players[1].auxiliaries[0x40],
      "P2 X initializes acquired helmet, arm and boot objects on arrival");
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;
  }
  MmxCoopSelect(g_ram,0);
  unsigned lives=g_ram[0x1f80];bool returned=false;
  for(unsigned t=0;t<900 && !returned;++t) {
    coop_frames(1,0,0);s=MmxCoopGetState();
    returned=g_ram[0x1f80]==lives-1 && !s.stage_pending && !MmxCoopTransitionActive() &&
        s.players[1].status==MMX_COOP_ALIVE && (s.players[1].body[0x27]&127);
  }
  check(returned,"native team death restarts the checkpoint and returns P2");
  coop_frames(10,0,SNES_PAD_A|SNES_PAD_RIGHT);s=MmxCoopGetState();
  check(s.players[1].auxiliaries[0] && s.players[1].auxiliaries[0x20] && s.players[1].auxiliaries[0x40],
      "P2 X keeps acquired armor after restart while dashing");
  coop_x_graphics_checks(fixture);
  puts("MMX P2 ARMOR CHECKS PASSED");
}
static void diagnostic_replay_checks(const char *assets, const char *mode) {
  RtlEnableExtendedFrameTiming();
  if(!strcmp(mode,"armor")) {coop_p2_armor_checks(assets,getenv("MMX_REPLAY_FIXTURE"));return;}
  bool coop = strcmp(mode,"solo") != 0;
  if(coop) {
    coop_replay_start(assets);
    check(MmxCoopEnable(!strcmp(mode,"zero") ? MMX_COOP_ZERO : MMX_COOP_X),"recording roster selected");
    if(!strcmp(mode,"local")) MmxCoopViewsSetLocalSplit(true);
  }
  const char *input=getenv("MMX_REPLAY_INPUT");
  if(input) {
    if(!strcmp(mode,"local")) MmxCoopViewsSetLocalSplit(false);
    check(MmxReplayPlayback(input),"fresh-process diagnostic replay opens");
    unsigned frames=0;
    while(!MmxReplayFinished() && !MmxReplayFailed() && frames<10000) {frame(0);++frames;}
    check(!MmxReplayFailed() && MmxReplayFinished(),"fresh-process checkpoints match");
    if(!strcmp(mode,"local")) check(MmxCoopViewsLocalSplit(),"recorded local layout overrides playback preferences");
    printf("MMX DIAGNOSTIC REPLAY PASSED (%u executions)\n",frames);return;
  }
  /* Real boot and title flow, followed by an input sequence on both seats.
   * No fabricated checkpoint: capture the same machine we just executed. */
  const char *fixture=getenv("MMX_REPLAY_FIXTURE");
  if(fixture) {
    check(RtlLoadSnapshot(fixture),"replay fixture loads");
    fprintf(stderr,"FIXTURE %s stage=%u mode=%02x/%02x/%02x player=%02x/%02x xy=%u,%u armor=%02x/%02x upgrades=%02x\n",
        fixture,g_ram[0x1f7a],g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0xba8],g_ram[0xba9],
        g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,g_ram[0xe18],g_ram[0xe22],g_ram[0x1f99]);
    if(getenv("MMX_REPLAY_INSPECT")) return;
  } else {
    for(unsigned t=0;t<900;++t) frame(t==160 || t==360 ? SNES_PAD_START : 0);
  }
  size_t size=RtlRollbackSnapshotBound();
  void *rollback=malloc(size);
  size_t saved=RtlRollbackSaveToMemory(rollback,size);
  check(saved!=0,"replay test saves an actual rollback checkpoint");
  unsigned executions=getenv("MMX_REPLAY_ROTATE")?4201:90;
  MmxReplayEnable(true);
  for(unsigned t=0;t<executions;++t) {
    if(t==40) check(RtlRollbackLoadFromMemory(rollback,saved),"recording observes rollback correction");
    if(t==60) diagnostic_restore_current_frame();
    frame((t<30?SNES_PAD_RIGHT:t<60?SNES_PAD_B:SNES_PAD_Y) |
          ((t<40?SNES_PAD_LEFT:SNES_PAD_Y|SNES_PAD_B)<<12));
    /* Capture at the player's rate. Unlimited headless boot can outrun any
     * bounded writer; saturation is covered by the blocked-worker unit test. */
    SDL_Delay(16);
  }
  char captured[1400]; snprintf(captured,sizeof(captured),"%s",MmxReplayPath());
  check(!MmxReplayFailed() && captured[0],"diagnostic capture was written");
  MmxReplayEnable(false);
  size_t cap=RtlSaveSnapshotToMemory(NULL,0);
  void *observed=malloc(cap),*plain=malloc(cap);
  size_t observed_size=RtlSaveSnapshotToMemory(observed,cap);
  check(RtlRollbackLoadFromMemory(rollback,saved),"unrecorded control restores starting machine");
  for(unsigned t=0;t<executions;++t) {
    if(t==40) check(RtlRollbackLoadFromMemory(rollback,saved),"unrecorded control repeats rollback");
    if(t==60) diagnostic_restore_current_frame();
    frame((t<30?SNES_PAD_RIGHT:t<60?SNES_PAD_B:SNES_PAD_Y) |
          ((t<40?SNES_PAD_LEFT:SNES_PAD_Y|SNES_PAD_B)<<12));
  }
  same(observed,observed_size,plain,RtlSaveSnapshotToMemory(plain,cap),
      "recording leaves the complete machine identical to an unrecorded run");
  free(observed);free(plain);free(rollback);
  check(MmxReplayPlayback(captured),"diagnostic capture opens for playback");
  /* Retention can start a segment early at a rollback/checkpoint byte limit. */
  FILE *capture_file=fopen(captured,"r");check(capture_file!=NULL,"capture drained before local playback");
  char capture_line[512];unsigned expected_frames=0;
  while(fgets(capture_line,sizeof(capture_line),capture_file))if(capture_line[0]=='H')++expected_frames;
  fclose(capture_file);
  unsigned frames=0;
  while(!MmxReplayFinished() && !MmxReplayFailed() && frames<601) { frame(0); ++frames; }
  check(!MmxReplayFailed() && MmxReplayFinished() && expected_frames && frames==expected_frames,
      "recorded state, both inputs, and every WRAM/co-op/view checkpoint replay exactly");
  puts("MMX DIAGNOSTIC REPLAY CHECKS PASSED");
}
