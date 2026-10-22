/* Real-ROM integration, separate from the portable round-state checks. */
#include "mmx_vs.h"
#include "mmx_vs_movement_test.inc"
static void vs_runtime_checks(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  MmxVsSetEnabled(true);MmxBossRushSetEnabled(true);
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t)
    frame(t==200 || t==350 || (t>500 && t%180==0)?SNES_PAD_START:0);
  check(MmxBossRushGetState().menu==1,"VS title menu appears");zero_replay(60);
  for(unsigned i=0;i<4;++i) {frame(SNES_PAD_DOWN);zero_replay(4);}
  fprintf(stderr,"VS title selection=%u y=%u\n",MmxBossRushGetState().selection,eagle_word(g_ram+0xbb0));
  check(MmxBossRushGetState().selection==4,"fifth title option selects VS");
  rush_visual_bmp("vs-title-entry.bmp");
  frame(SNES_PAD_Y);frame(0);
  check(MmxVsGetState().phase==MMX_VS_SELECT,"VS opens character selection");
  check(MmxVsChoose(0,0) && MmxVsChoose(1,4),"Penguin and Kuwanger lock in");
  for(unsigned t=0;t<2400 && MmxVsGetState().phase!=MMX_VS_FIGHT;++t) {
    frame(0);
    if(t%120==0) {
      MmxBossRushState r=MmxBossRushGetState();
      fprintf(stderr,"VS load t=%u phase=%u rush=%u game=%u:%u:%u xy=%04x,%04x camera=%04x bosses=%04x:%u/%04x:%u\n",
          t,MmxVsGetState().phase,r.mode,g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],eagle_word(g_ram+0xbad),eagle_word(g_ram+0xbb0),eagle_word(g_ram+0x1e4d),
          r.bosses[0].object,r.bosses[0].phase,r.bosses[1].object,r.bosses[1].phase);
    }
  }
  check(MmxVsGetState().phase==MMX_VS_FIGHT,"both native entrances complete");
  zero_replay(10);rush_visual_bmp("vs-first-fight.bmp");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"VS snapshot saves");
  check(RtlSaveSnapshot("vs-ready.sav"),"private VS netplay fixture saves");
  for(unsigned i=0;i<120;++i)frame((i%30==0?SNES_PAD_Y:0)|((i%40==0?SNES_PAD_A|SNES_PAD_LEFT:0)<<12));
  size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"VS snapshot loads");
  for(unsigned i=0;i<120;++i)frame((i%30==0?SNES_PAD_Y:0)|((i%40==0?SNES_PAD_A|SNES_PAD_LEFT:0)<<12));
  size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"VS combat snapshot replay is byte exact");
  rush_visual_bmp("vs-after-attacks.bmp");
  snes_rewind_configure();
  for(unsigned i=0;i<12;++i) {frame(0);snes_rewind_note_frame();}
  en=RtlSaveSnapshotToMemory(expected,cap);
  for(unsigned i=0;i<6;++i) {frame(SNES_PAD_X);snes_rewind_note_frame();}
  check(snes_rewind_open(),"VS rewind opens");rewind_six_frames();snes_rewind_commit();
  an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"VS rewind restores the selected combat frame");
  snes_rewind_shutdown();
  check(RtlLoadSnapshotFromMemory(start,n),"ready fight restores for round transition");
  MmxBossRushState knockout=MmxBossRushGetState();
  g_ram[knockout.bosses[0].object+0x27]=0x81;
  for(unsigned t=0;t<240 && MmxVsGetState().phase==MMX_VS_FIGHT;++t) {
    for(unsigned d=0x1428;d<0x1628;d+=64)if(g_ram[d] && MmxBossRushOwner(d)==1) {
      memcpy(g_ram+d+5,g_ram+knockout.bosses[0].object+5,2);
      memcpy(g_ram+d+8,g_ram+knockout.bosses[0].object+8,2);
    }
    frame((t<16?SNES_PAD_Y:0)<<12);
  }
  check(MmxVsGetState().phase==MMX_VS_RESULT && MmxVsGetState().score[1]==1 && !MmxVsGetState().fighters[0].hp,
      "real attack KO awards exactly one point");
  rush_visual_bmp("vs-round-result.bmp");zero_replay(50);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<120 && MmxVsGetState().phase==MMX_VS_RESULT;++t)frame(0);
  check(MmxVsGetState().phase==MMX_VS_SELECT && MmxVsGetState().picker==1,
      "actual next-round path clears the arena and gives the winner first pick");
  rush_visual_bmp("vs-character-select.bmp");
  MmxVsState selection=MmxVsGetState(),alternate=selection;
  MmxCoopViewsSetOnline(true);MmxRendererSetPeerView(0);CaptureSimulationFrame(1);
  check(MmxRendererDraw((uint32_t *)expected,(MmxRenderView){256,0,4.0/3.0},true),"online selection renders");
  alternate.cursor[1]^=4;check(MmxVsSetState(&alternate),"alternate private cursor installs");CaptureSimulationFrame(1);
  check(MmxRendererDraw((uint32_t *)actual,(MmxRenderView){256,0,4.0/3.0},true) &&
      !memcmp(expected,actual,256*224*4),"opponent cursor cannot change the local selection image");
  MmxRendererSetPeerView(1);
  check(MmxRendererDraw((uint32_t *)actual,(MmxRenderView){256,0,4.0/3.0},true) &&
      memcmp(expected,actual,256*224*4),"active picker sees their own cursor");
  MmxCoopViewsSetOnline(false);MmxRendererSetPeerView(-1);check(MmxVsSetState(&selection),"local selection restores");
  frame(SNES_PAD_RIGHT<<12);frame(0);
  check(MmxVsGetState().cursor[1]==(selection.cursor[1]^4),"selection moves between the two weapon-menu columns");
  check(MmxVsSetState(&selection),"selection navigation fixture restores");CaptureSimulationFrame(1);
  size_t select_n=RtlSaveSnapshotToMemory(expected,cap);check(select_n,"between-round selection snapshot saves");
  check(RtlSaveSnapshot("vs-selection.sav"),"private VS selection netplay fixture saves");
  unsigned pairs=0;
  for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b) {
    if(getenv("MMX_VS_SOURCE_ONLY") && a!=(unsigned)atoi(getenv("MMX_VS_SOURCE_ONLY")))continue;
    if(getenv("MMX_VS_BOSS_ONLY") && (a!=(unsigned)atoi(getenv("MMX_VS_BOSS_ONLY")) || a!=b))continue;
    check(RtlLoadSnapshotFromMemory(expected,select_n),"selection fixture restores");
    check(MmxVsChoose(1,b) && MmxVsChoose(0,a),"both round choices lock");
    for(unsigned t=0;t<900 && MmxVsGetState().phase!=MMX_VS_FIGHT;++t)frame(0);
    MmxBossRushState r=MmxBossRushGetState();
    check(r.bosses[0].id==a && r.bosses[1].id==b &&
        g_ram[r.bosses[0].object+10]==kMmxBossRushBosses[a].kind &&
        g_ram[r.bosses[1].object+10]==kMmxBossRushBosses[b].kind,
        "both native body identities match the locked roster");
    fprintf(stderr,"VS pair %u/%u phase=%u native=%u:%u:%u,%u:%u:%u hp=%u,%u\n",a,b,MmxVsGetState().phase,
        g_ram[r.bosses[0].object+1],g_ram[r.bosses[0].object+2],g_ram[r.bosses[0].object+3],
        g_ram[r.bosses[1].object+1],g_ram[r.bosses[1].object+2],g_ram[r.bosses[1].object+3],r.bosses[0].health,r.bosses[1].health);
    check(MmxVsGetState().phase==MMX_VS_FIGHT,"ordered pair completes both entrances");
    zero_replay(30);check(MmxVsGetState().phase==MMX_VS_FIGHT,"idle pair remains alive");
    check(MmxVsGetState().fighters[0].hp==32 && MmxVsGetState().fighters[1].hp==32,
        "idle pair takes no autonomous attack or contact damage");
    static const unsigned weak_target[8]={1,2,3,4,5,6,7,0};
    if(b==weak_target[a]) {
      size_t ready_n=RtlSaveSnapshotToMemory(start,cap);check(ready_n,"weakness fight snapshot saves");
      eagle_putword(g_ram+r.bosses[0].object+5,r.camera_x+128);
      eagle_putword(g_ram+r.bosses[1].object+5,r.camera_x+184);
      unsigned button=a==2?SNES_PAD_X:SNES_PAD_Y,min_hp=32,max_hit=0;
      for(unsigned t=0;t<240 && MmxVsGetState().phase==MMX_VS_FIGHT;++t) {
        /* Isolate damage classification from traversal: position real
         * emitted attacks on the target without setting damage/success flags. */
        MmxVsState vs=MmxVsGetState();
        for(unsigned d=0x1428;d<0x1628;d+=64)if(g_ram[d] && MmxBossRushOwner(d)==0 && vs.attack_class[(d-0xe68)/32]) {
          memcpy(g_ram+d+5,g_ram+r.bosses[1].object+5,2);memcpy(g_ram+d+8,g_ram+r.bosses[1].object+8,2);
        }
        if(a==2 && vs.fighters[0].action==MMX_VS_SPECIAL && vs.fighters[0].ticks>=8) {
          memcpy(g_ram+r.bosses[1].object+5,g_ram+r.bosses[0].object+5,2);
          memcpy(g_ram+r.bosses[1].object+8,g_ram+r.bosses[0].object+8,2);
        }
        frame(t<16?button:0);unsigned hp=MmxVsGetState().fighters[1].hp;if(hp<min_hp)min_hp=hp;
        unsigned hit=MmxVsGetState().fighters[1].last_damage;if(hit>max_hit)max_hit=hit;
      }
      fprintf(stderr,"VS weakness source=%u target=%u minimum_hp=%u largest_hit=%u\n",a,b,min_hp,max_hit);
      check(max_hit>=3,"actual signature ability delivers weakness damage per accepted hit");
      if(a==0 || a==1) {
        zero_replay(180);MmxVsFighter target=MmxVsGetState().fighters[1];
        check(!target.stun && !target.invulnerable && !target.captured,"native weakness reaction has a finite recovery");
        frame(SNES_PAD_Y<<12);bool accepted=false;
        for(unsigned t=0;t<24;++t) {frame(0);accepted|=MmxVsGetState().fighters[1].started!=0;}
        check(accepted,"boss accepts attacks after its weakness recovery");
      }
      check(RtlLoadSnapshotFromMemory(start,ready_n),"fight restores after native weakness check");
      if(a==4) {
        eagle_putword(g_ram+r.bosses[0].object+5,r.camera_x+128);
        eagle_putword(g_ram+r.bosses[1].object+5,r.camera_x+156);
        bool captured=false,thrown=false;unsigned horn_hit=0;
        for(unsigned t=0;t<180;++t) {
          frame(t<16?SNES_PAD_X:0);MmxVsFighter target=MmxVsGetState().fighters[1];
          captured|=target.captured!=0;thrown|=target.motion_ticks!=0;
          if(target.last_damage>horn_hit)horn_hit=target.last_damage;
        }
        fprintf(stderr,"VS horn grab damage=%u captured=%u thrown=%u\n",horn_hit,captured,thrown);
        check(horn_hit>=3 && captured && thrown,"horn grab applies Cutter weakness and native capture/throw");
        check(RtlLoadSnapshotFromMemory(start,ready_n),"fight restores after horn grab check");
      }
    }
    if(a==b) {
      char path[80];snprintf(path,sizeof(path),"vs-mirror-%u.bmp",a);rush_visual_bmp(path);
      size_t ready_n=RtlSaveSnapshotToMemory(actual,cap);check(ready_n,"mirror combat snapshot saves");
      static const unsigned buttons[]={SNES_PAD_Y,SNES_PAD_X,SNES_PAD_A,SNES_PAD_B};
      for(unsigned seat=0;seat<2;++seat)for(unsigned action=0;action<4;++action) {
        check(RtlLoadSnapshotFromMemory(actual,ready_n),"mirror action fixture restores");
        unsigned children=0,acted=0,accepted=0;
        for(unsigned t=0;t<150;++t) {
          frame((t<16?buttons[action]:0)<<(seat*12));
          MmxVsFighter f=MmxVsGetState().fighters[seat];acted|=f.action!=MMX_VS_IDLE;
          accepted|=f.started || (a==6 && f.flying);
          for(unsigned d=0xe68;d<0x1d08;d+=32)if(g_ram[d] && MmxBossRushOwner(d)==(int)seat && d!=r.bosses[seat].object)children++;
          if(!seat && t==30) {char image[96];snprintf(image,sizeof(image),"vs-action-%u-%u.bmp",a,action);rush_visual_bmp(image);}
        }
        fprintf(stderr,"VS action boss=%u seat=%u action=%u acted=%u children=%u hp=%u,%u\n",a,seat,action,acted,children,
            MmxVsGetState().fighters[0].hp,MmxVsGetState().fighters[1].hp);
        if(!(a==4 && action==3) && !(a==7 && action==2))check((acted || (a==6 && action==3)) && accepted,"native boss controller accepts the mapped seat action");
      }
      vs_movement_checks(a,actual,ready_n);
    }
    ++pairs;
    if(getenv("MMX_VS_QUICK"))goto done;
  }
done:
  fprintf(stderr,"VS ordered pair entrances verified=%u\n",pairs);
  frame(SNES_PAD_START);frame(0);check(MmxVsGetState().paused,"VS pauses");
  frame(SNES_PAD_DOWN);frame(0);frame(SNES_PAD_A);frame(0);
  for(unsigned t=0;t<1800 && (MmxVsActive() || !MmxBossRushGetState().menu);++t) {
    frame(0);
    if(t%240==0)fprintf(stderr,"VS exit t=%u phase=%u pending=%u projected=%u rush=%u actor=%u title=%u game=%u:%u:%u opening=%u:%u\n",
        t,MmxVsGetState().phase,MmxVsGetState().exit_requested,MmxVsGetState().projected,
        MmxBossRushGetState().mode,MmxBossRushGetState().actor,MmxBossRushGetState().return_title,
        g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0x38],g_ram[0x39]);
  }
  check(!MmxVsActive() && MmxBossRushGetState().mode==MMX_RUSH_OFF && MmxBossRushGetState().menu,
      "pause exit drains native actors and returns to the actual title menu");
  puts("MMX VS NATIVE BOOT AND REPLAY PASSED");
}
