#if defined(SNES_HAS_LOBBY_CLIENT)
static void vs_netplay_checks(const char *root,const char *rom,const char *fixture,int guest) {
  check(fixture && snes_mod_runtime_initialize_c(root,"megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),"VS pair catalog initializes");
  const RecompLauncherCModProvider *base=snes_mod_runtime_launcher_provider_c();
  const char *disable[][2]={{"megaman-x.coop","coop"},{"megaman-x.character.zero","zero"},
    {"megaman-x.weapons.x2","weapons"},{"megaman-x.weapons.x3","weapons"},
    {"megaman-x.enhancement.widescreen","widescreen"}};
  for(unsigned i=0;i<sizeof(disable)/sizeof(*disable);++i)
    check(base->feature_enable(base->ctx,disable[i][0],disable[i][1],0),"VS does not require character or weapon packs");
  check(base->feature_enable(base->ctx,"megaman-x.mode.vs","vs",1) && base->commit(base->ctx,rom),"VS-only offline plan commits with X1 ROM");
  host_lobby_ensure_init();
  RecompLauncherCGameInfo gi={.mods=base,.netplay=snes_host_lobby_callbacks(),.netplay_supported=1};
  MmxNetplayConfigureLauncher(&gi);gi.netplay_mode_changed(1);
  const RecompLauncherCNetplayCallbacks *np=gi.netplay;
  const RecompLauncherCModProvider *mods=gi.mods;
  np->set_player_name(np->ctx,guest?"VS Guest Test":"VS Host Test");
  int port=getenv("MMX_VS_NET_PORT")?atoi(getenv("MMX_VS_NET_PORT")):18240;
  check(port>1024 && port<65534,"VS private LAN port valid");
  char endpoint[64],bind[64];snprintf(endpoint,sizeof(endpoint),"127.0.0.1:%d",port);
  snprintf(bind,sizeof(bind),"0.0.0.0:%d",port+1);
  RecompLauncherCSettings settings={0};
  if(!guest)check(np->create(np->ctx,"VS private test",endpoint,"",&settings,1,2)==0,"VS host creates two-seat room");
  else {
    char room[80];snprintf(room,sizeof(room),"lan:%s",endpoint);SDL_Delay(250);
    check(np->join(np->ctx,room,"",bind)==0,"VS guest joins room");
  }
  RecompLauncherCNetplayLaunch launch={0};uint32_t started=SDL_GetTicks();int requested=0;
  while(SDL_GetTicks()-started<15000) {
    np->pump(np->ctx);RecompLauncherCNetplayMember member={0};
    int ready=np->member_get(np->ctx,1,&member) && member.ready && member.display_name[0];
    if(!guest && !requested && ready && SDL_GetTicks()-started>1000) {
      check(np->request_start(np->ctx,&settings)==0,"VS host starts agreed room");requested=1;
    }
    if(np->launch_pending(np->ctx) && np->fill_launch(np->ctx,&launch))break;
    SDL_Delay(2);
  }
  check(launch.enabled && launch.local_slot==guest && launch.player_count==2,"VS lobby maps the two seats");
  check(mods->commit_netplay(mods->ctx,rom),"VS room commits without X3");
  snes_mod_runtime_activate_plugins_c();
  check(MmxVsEnabled() && !MmxCoopEnabled(),"VS activation needs no X or Zero co-op");
  check(RtlLoadSnapshot(fixture),"VS ready fixture loads");
  SnesHostLaunchResult result;snes_host_app_apply_launch(&launch,&result);
  char plan[2048];snes_mod_runtime_effective_set_c(plan,sizeof(plan));
  snes_netplay_rb_set_modset(plan,snes_mod_runtime_check_set_c,snes_mod_runtime_adopt_set_c);
  snes_netplay_rb_set_replay_frame(NetplayReplayFrame);
  check(snes_netplay_start(&result.net_cfg)==0,"VS rollback session starts");
  unsigned frames=0,fight_frames=0;int delayed=0;started=SDL_GetTicks();
  bool selecting=MmxVsGetState().phase==MMX_VS_SELECT;
  unsigned target_frames=selecting?1800:720;
  while(frames<target_frames && SDL_GetTicks()-started<60000) {
    if(guest && frames==120 && !delayed) {SDL_Delay(600);delayed=1;}
    if(snes_netplay_needs_local_sample()) {
      unsigned t=snes_netplay_sim_tick(),pad=0;MmxVsState vs=MmxVsGetState();
      if(vs.phase==MMX_VS_SELECT)pad=t%20==0?SNES_PAD_A:0;
      else if(vs.phase==MMX_VS_RESULT)pad=t%60==0?SNES_PAD_START:0;
      else if(t%90<12)pad=SNES_PAD_Y;
      else if(t%90>=30 && t%90<42)pad=SNES_PAD_X;
      else if(t%90>=60 && t%90<72)pad=SNES_PAD_A|(guest?SNES_PAD_LEFT:SNES_PAD_RIGHT);
      snes_netplay_stage_local((uint16_t)pad);
    }
    if(snes_netplay_poll_admit()) {
      NetplayReplayFrame(snes_netplay_published_inputs()|snes_netplay_active_mask(),frames);
      fight_frames+=MmxVsGetState().phase==MMX_VS_FIGHT;
      snes_netplay_finish_frame();++frames;
    } else SDL_Delay(1);
    if(snes_netplay_refusal())fprintf(stderr,"VS refused: %s\n",snes_netplay_refusal());
    check(!snes_netplay_refusal() && !snes_netplay_rb_desync_count(),"VS peers retain matching simulation digests");
  }
  check(frames==target_frames && fight_frames>=100,"VS peers finish selection/entrances and advance combat together");
  check(MmxCoopViewsOnline(),"VS runtime uses independent peer views");
  printf("VS_PAIR_RESULT seat=%d frames=%u confirmed=%u episodes=%u replay=%llu\n",guest,frames,
      snes_netplay_rb_confirmed_through(),snes_netplay_rb_episode_count(),(unsigned long long)snes_netplay_rb_resim_ticks());
  rush_visual_bmp("vs-local-view.bmp");
  snes_netplay_shutdown();snes_host_lobby_shutdown();gi.netplay_mode_changed(0);
  puts("MMX VS NETPLAY PAIR PASSED");
}
#endif
