/* Private ROM-backed Learner Mode checks (P2). The learner loses no health
 * and never dies; pits, fatal states and falling behind beam him back beside
 * P1 while the world keeps running. P1 alone moves the camera, pauses and
 * owns the stage. Uses the ordinary grounded Highway fixture. */
static void learner_setup(unsigned character,const char *fixture) {
  MmxCoopDisable();check(RtlLoadSnapshot(fixture),"learner fixture loads");
  check(MmxCoopEnable(character),"Learner Mode accepts either roster");
  MmxCoopSetLearner(true);check(MmxCoopLearner(),"Learner Mode is active");
  coop_frames(8,0,0);
  MmxCoopState s=MmxCoopGetState();
  check(coop_place_fixture_partner(coop_x(&s.players[0])+40,coop_y(&s.players[0])),"learner placed beside P1");
  coop_frames(2,0,0);
}
/* One frame that must not freeze the world. P1 can be kept topped up so a
 * stray enemy on the walk cannot end the check through P1's own death. */
static void learner_frame(unsigned p1,unsigned p2,bool keep_p1) {
  unsigned tick=g_ram[0xb9c];
  if(keep_p1 && !MmxCoopGetState().current && (g_ram[0xbcf]&127))
    g_ram[0xbcf]=(uint8_t)((g_ram[0xbcf]&128)|g_ram[0x1f9a]);
  coop_frames(1,p1,p2);
  check((uint8_t)(g_ram[0xb9c]-tick)==1 && !MmxCoopTransitionActive(),
      "the learner never freezes the world or holds a rendered frame");
  MmxCoopState s=MmxCoopGetState();
  check(s.players[1].status!=MMX_COOP_FALLEN && s.anchor==0,
      "the learner never falls and never drives the stage");
}
static bool learner_back(const MmxCoopState *s) {
  return !s->scene_owner && s->players[1].status==MMX_COOP_ALIVE && !s->players[1].zero.swap_phase;
}
static void learner_check_beside(const char *what) {
  MmxCoopState s=MmxCoopGetState();
  int dx=(int)coop_x(&s.players[1])-(int)coop_x(&s.players[0]);
  int dy=(int)coop_y(&s.players[1])-(int)coop_y(&s.players[0]);
  fprintf(stderr,"learner %s: dx=%d dy=%d hp=%u/%u\n",what,dx,dy,s.players[1].body[0x27]&127,g_ram[0x1f9a]);
  /* Landing gaps reach 64 px; P1 may have kept walking during the arrival. */
  check(dx>=-112 && dx<=112 && dy>=-48 && dy<=48,"the learner returns beside P1");
  check((s.players[1].body[0x27]&127)==g_ram[0x1f9a],"the learner returns at full health");
}
/* Follow a rescue: it must start as a live transport and end beside P1. */
static void learner_rescue(const char *what,unsigned p1_frames,uint8_t *start,
                           uint8_t *expected,uint8_t *actual,size_t cap) {
  bool began=false,returned=false;
  for(unsigned f=0;f<600 && !returned;++f) {
    learner_frame(f<p1_frames ? SNES_PAD_RIGHT : 0,0,true);
    MmxCoopState s=MmxCoopGetState();
    if(s.scene_owner) {
      check((s.scene_flags&MMX_COOP_SCENE_LIVE)!=0,"a learner rescue is a live transport");
      if(!began && start) {
        /* Mid-departure replay, while the world still runs around it. */
        coop_frames(2,0,0);
        size_t n=RtlSaveSnapshotToMemory(start,cap);coop_frames(30,SNES_PAD_RIGHT,0);
        size_t en=RtlSaveSnapshotToMemory(expected,cap);
        check(RtlLoadSnapshotFromMemory(start,n),"live rescue snapshot restores");
        coop_frames(30,SNES_PAD_RIGHT,0);
        size_t an=RtlSaveSnapshotToMemory(actual,cap);
        same(expected,en,actual,an,"a live rescue replays exactly");
      }
      began=true;
    }
    returned=began && learner_back(&s);
  }
  if(!returned) {
    MmxCoopState s=MmxCoopGetState();
    fprintf(stderr,"learner %s stuck: began=%u scene=%u/%u/%u status=%u swap=%u away=%u stage=%u/%u/%u\n",what,began,
        s.scene_owner,s.scene_phase,s.scene_flags,s.players[1].status,s.players[1].zero.swap_phase,s.learner_away,
        g_ram[0xd1],g_ram[0xd2],g_ram[0xd3]);
  }
  check(began && returned,what);
  coop_frames(1,0,0);learner_check_beside(what);
}
/* The mod hides the learner's health meter unless the option shows it; P1's
 * HUD and P2's weapon column are untouched in either HUD layout. */
static void learner_hud_check(unsigned character) {
  static uint32_t hidden[256*224],shown[256*224];
  const MmxRenderView view={256,0,4.0/3.0};
  for(unsigned compact=0;compact<2;++compact) {
    MmxRendererSetCompactCoopHud(compact!=0);
    MmxRendererSetHideLearnerHealth(true);
    check(MmxRendererDraw(hidden,view,false),"learner HUD renders");
    MmxRendererSetHideLearnerHealth(false);
    check(MmxRendererDraw(shown,view,false),"learner HUD renders with health shown");
    unsigned changed=0,outside=0;
    for(unsigned y=0;y<224;++y) for(unsigned x=0;x<256;++x)
      if(hidden[y*256+x]!=shown[y*256+x]) {++changed;outside+=x>=72 || y>=160;}
    fprintf(stderr,"learner HUD roster=%u compact=%u hidden pixels=%u outside=%u\n",character,compact,changed,outside);
    check(changed>=64 && !outside,"only the learner's health meter is hidden");
  }
  MmxRendererSetCompactCoopHud(true);
}
static void coop_learner_checks(const char *fixture,uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  interp_bridge_set_pre_opcode_hook(0x818aa4,coop_death_sound_check);
  interp_bridge_set_pre_opcode_hook(0x818b09,coop_death_sound_check);
  for(unsigned character=0;character<2;++character) {
    learner_setup(character,fixture);
    learner_hud_check(character);
    /* A pit: start below the screen with health intact, as the natural
     * death check does. P1 keeps walking throughout. */
    learner_setup(character,fixture);
    unsigned lives=g_ram[0x1f80],p1x=coop_x(&MmxCoopGetState().players[0]);
    MmxCoopSelect(g_ram,1);
    unsigned y=(g_ram[0x1e50]|g_ram[0x1e51]<<8)+240;
    g_ram[0xbb0]=(uint8_t)y;g_ram[0xbb1]=(uint8_t)(y>>8);g_ram[0xbaa]=8;g_ram[0xbab]=2;
    MmxCoopSelect(g_ram,0);
    coop_death_sounds=coop_death_orbs=0;
    learner_rescue("pit fall rescues the learner",40,character ? NULL : start,expected,actual,cap);
    check(coop_x(&MmxCoopGetState().players[0])>p1x+30,"P1 keeps moving while the learner is rescued");
    check(g_ram[0x1f80]==lives && !coop_death_sounds && !coop_death_orbs,
        "a learner's fall costs no life and plays no death");
    if(!character) coop_image(character,"learner-pit-return");

    /* A would-be fatal enemy hit: native contact, learner at 1 HP. */
    learner_setup(character,fixture);
    unsigned enemy=0;
    for(unsigned i=0;i<400 && !enemy;++i) {
      coop_frames(1,SNES_PAD_RIGHT,SNES_PAD_RIGHT);
      for(unsigned d=0xe68;d<0x1228;d+=64)
        if(g_ram[d] && g_ram[d+14] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {enemy=d;break;}
    }
    check(enemy!=0,"native enemy found for learner contact");
    unsigned contact_damage=g_ram[enemy+0x26];
    MmxCoopSelect(g_ram,1);
    memcpy(g_ram+0xbad,g_ram+enemy+5,2);memcpy(g_ram+0xbb0,g_ram+enemy+8,2);
    g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbcf]=129;
    MmxCoopSelect(g_ram,0);
    lives=g_ram[0x1f80];coop_death_sounds=coop_death_orbs=0;
    bool hurt=false;
    for(unsigned f=0;f<90;++f) {
      coop_frames(1,0,0);MmxCoopState s=MmxCoopGetState();
      hurt|=s.players[1].body[2]==0x0e;
      check(s.players[1].status!=MMX_COOP_FALLEN && (s.players[1].body[0x27]&127),
          "an enemy hit never empties the learner's health");
    }
    MmxCoopState s=MmxCoopGetState();
    fprintf(stderr,"learner contact roster=%u enemy=%04x class=%02x damage=%u hurt=%u hp=%u\n",
        character,enemy,g_ram[enemy+10],contact_damage,hurt,s.players[1].body[0x27]&127);
    check(hurt,"the learner keeps the native hurt reaction");
    check((s.players[1].body[0x27]&127)==g_ram[0x1f9a] && s.players[1].status==MMX_COOP_ALIVE,
        "the learner stays at full health");
    check(g_ram[0x1f80]==lives && !coop_death_sounds && !coop_death_orbs,"enemy contact plays no learner death");

    /* Any other path into the death controller becomes a rescue. */
    learner_setup(character,fixture);
    lives=g_ram[0x1f80];coop_death_sounds=coop_death_orbs=0;
    MmxCoopSelect(g_ram,1);g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;MmxCoopSelect(g_ram,0);
    learner_rescue("a fatal state rescues the learner",0,NULL,NULL,NULL,0);
    check(g_ram[0x1f80]==lives && !coop_death_sounds && !coop_death_orbs,
        "the death controller never runs for the learner");

    /* P1 walks away: no shared-screen tether, a P1-only camera, and a
     * rescue once the learner has been off P1's screen for two seconds. */
    learner_setup(character,fixture);
    unsigned learner_x=coop_x(&MmxCoopGetState().players[1]);
    bool separated=false,began=false,returned=false;
    unsigned away_frames=0;
    for(unsigned f=0;f<600 && !returned;++f) {
      learner_frame(f<260 ? SNES_PAD_RIGHT : 0,0,true);
      s=MmxCoopGetState();
      int p1_screen=(int)coop_x(&s.players[0])-(int)(g_ram[0x1e4d]|g_ram[0x1e4e]<<8);
      check(p1_screen>=0 && p1_screen<256,"the camera keeps P1 on screen");
      if(!s.scene_owner && coop_x(&s.players[0])>coop_x(&s.players[1])+260) separated=true;
      if(!began && !s.scene_owner) away_frames=s.learner_away;
      if(s.scene_owner) began=true;
      returned=began && learner_back(&s);
    }
    fprintf(stderr,"learner left behind roster=%u start=%u separated=%u away=%u began=%u returned=%u\n",
        character,learner_x,separated,away_frames,began,returned);
    check(separated,"P1 is never held back by the learner");
    check(began && away_frames>=MMX_COOP_LEARNER_AWAY_FRAMES-1,"falling behind off screen rescues the learner after two seconds");
    check(returned,"the learner left behind returns");
    coop_frames(1,0,0);learner_check_beside("left behind");

    /* Start: the learner cannot pause; P1 still can. P1 cannot withdraw. */
    coop_frames(1,0,SNES_PAD_START);coop_frames(10,0,0);
    check(!MmxCoopGetState().menu_owner && g_ram[0x1f10]<6,"the learner's Start never pauses");
    for(unsigned f=0;f<100;++f) learner_frame(0,0,true);
    learner_frame(SNES_PAD_SELECT,0,true);
    for(unsigned f=0;f<100;++f) learner_frame(SNES_PAD_SELECT,0,true);
    check(MmxCoopGetState().players[0].status==MMX_COOP_ALIVE && !MmxCoopGetState().scene_owner,
        "P1 cannot withdraw and leave the learner driving");
    coop_frames(1,SNES_PAD_START,0);coop_frames(75,0,0);
    check(MmxCoopGetState().menu_owner==1,"P1 still opens the pause menu");
    coop_frames(1,SNES_PAD_START,0);coop_frames(75,0,0);
    check(!MmxCoopGetState().menu_owner,"P1's pause menu closes");

    /* The learner's own Select withdrawal and return never freeze P1. */
    coop_frames(2,0,0);
    for(unsigned f=0;f<95 && MmxCoopGetState().players[1].status==MMX_COOP_ALIVE &&
        !MmxCoopGetState().scene_owner;++f) learner_frame(0,SNES_PAD_SELECT,true);
    check(MmxCoopGetState().scene_owner && (MmxCoopGetState().scene_flags&MMX_COOP_SCENE_WITHDRAW),
        "holding the learner's Select begins a live departure");
    for(unsigned f=0;f<120 && MmxCoopGetState().scene_owner;++f) learner_frame(SNES_PAD_RIGHT,0,true);
    s=MmxCoopGetState();
    check(s.players[1].status==MMX_COOP_ABSENT && s.enrolled,"the learner's departure ends withdrawn");
    learner_frame(0,SNES_PAD_SELECT,true);learner_frame(0,0,true);
    check(MmxCoopGetState().scene_owner!=0,"the learner's Select begins a live return");
    returned=false;
    for(unsigned f=0;f<240 && !returned;++f) {learner_frame(0,0,true);s=MmxCoopGetState();returned=learner_back(&s);}
    check(returned,"the learner's live return completes");
    learner_check_beside("rejoin");

    /* P1's death restarts the whole team; the learner never takes over. */
    learner_setup(character,fixture);
    lives=g_ram[0x1f80];
    g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;
    returned=false;
    for(unsigned i=0;i<1500 && !returned;++i) {
      coop_frames(1,0,0);s=MmxCoopGetState();
      check(s.anchor==0 && s.players[0].status!=MMX_COOP_FALLEN,"P1's death never hands the stage to the learner");
      returned=g_ram[0x1f80]+1==lives && !s.stage_pending && learner_back(&s) && g_ram[0xd3]==4 &&
          s.players[0].status==MMX_COOP_ALIVE && (s.players[0].body[0x27]&127)==g_ram[0x1f9a];
    }
    if(!returned) fprintf(stderr,"learner team restart stuck stage=%u/%u/%u lives=%u/%u pending=%u scene=%u status=%u/%u\n",
        g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],lives,g_ram[0x1f80],s.stage_pending,s.scene_owner,
        s.players[0].status,s.players[1].status);
    check(returned,"P1's death spends one life and the learner returns after the restart");
  }
  /* Off again: the ordinary co-op pit death is restored. */
  learner_setup(0,fixture);MmxCoopSetLearner(false);check(!MmxCoopLearner(),"Learner Mode turns off");
  MmxCoopSelect(g_ram,1);
  unsigned y=(g_ram[0x1e50]|g_ram[0x1e51]<<8)+240;
  g_ram[0xbb0]=(uint8_t)y;g_ram[0xbb1]=(uint8_t)(y>>8);g_ram[0xbaa]=8;g_ram[0xbab]=2;
  MmxCoopSelect(g_ram,0);
  coop_frames(150,0,0);
  check(MmxCoopGetState().players[1].status==MMX_COOP_FALLEN,"without Learner Mode a pit is fatal for P2");
  interp_bridge_set_pre_opcode_hook(0x818aa4,NULL);
  interp_bridge_set_pre_opcode_hook(0x818b09,NULL);
  MmxCoopDisable();
  puts("MMX CO-OP LEARNER MODE CHECKS PASSED");
}
