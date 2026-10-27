/* ROM-backed independent-view checks; local seat selection never owns WRAM. */
#include "mmx_coop_view.h"
/* The reporter's Armadillo replay leaves Zero on solid lower-path ground
 * while X backtracks up the wall. Native shared limits then killed Zero. */
static void coop_region_replay_checks(const char *assets,const char *fixture,
    uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  coop_replay_start(assets);RtlEnableExtendedFrameTiming();
  g_config.no_sprite_limits=true;MmxCoopViewsSetIndependent(true);
  bool wrap=getenv("MMX_COOP_WRAP_REPLAY_FIXTURE")!=NULL;
  unsigned p1[600],p2[600],count=0,a,b;
  const char *inputs=getenv("MMX_COOP_REGION_INPUTS");
  FILE *f=inputs?fopen(inputs,"r"):NULL;check(f!=NULL,"private region input trace opens");
  while(count<600 && fscanf(f,"%x %x",&a,&b)==2) {p1[count]=a;p2[count++]=b;}
  fclose(f);check(count>=300,"region trace covers the recorded death");
  for(unsigned driver=0;driver<2;++driver) {
    check(RtlLoadSnapshot(fixture),"captured lower-path checkpoint loads");
    MmxCoopState s=MmxCoopGetState();
    check(g_ram[0x1f7a]==3 && s.players[1].status==MMX_COOP_ALIVE,
        "Armadillo checkpoint precedes the false pit death");
    if(driver) {
      MmxCoopPlayer p=s.players[0];s.players[0]=s.players[1];s.players[1]=p;
      s.current^=1;s.anchor^=1;MmxCoopSetState(&s);
    }
    bool wrapped=false;
    for(unsigned t=0;t<count;++t) {
      unsigned previous=coop_x(&s.players[driver]);
      coop_frames(1,driver?p2[t]:p1[t],driver?p1[t]:p2[t]);s=MmxCoopGetState();
      if(wrap && previous>7000 && coop_x(&s.players[driver])<1024) {
        wrapped=true;
        check(coop_x(&s.players[driver^1])<1024 && coop_y(&s.players[driver^1])>=2048 &&
            abs((int)coop_y(&s.players[0])-(int)coop_y(&s.players[1]))<160,
            "map wrap relocates the stored partner in the same frame as its driver");
      }
    }
    s=MmxCoopGetState();unsigned lower=driver^1;
    MmxCoopView v=MmxCoopViewForPlayer(g_ram,&s,lower);
    fprintf(stderr,"issue110 region driver=%u lower=%u hp=%u y=%u camera=%d native=%u bottom=%u\n",
        driver,lower,s.players[lower].body[0x27]&127,coop_y(&s.players[lower]),v.y,
        eagle_word(g_ram+0x1e50),MmxCoopViewsGetBoundsState().limits[lower][3]);
    check(s.players[lower].status==MMX_COOP_ALIVE && (s.players[lower].body[0x27]&127),
        "either lower-path player survives the partner's uphill backtrack");
    if(wrap) check(wrapped && v.y>=2048,
        "either player keeps its view through Armadillo's map wrap");
    else check(v.y>=1248 && eagle_word(g_ram+0x1e50)<=1056,
        "lower path keeps its camera while the native view follows the upper path");
    for(unsigned seat=0;seat<2;++seat) {
      char name[64];snprintf(name,sizeof(name),"issue110-%s-driver%u-seat%u",wrap?"wrap":"region",driver,seat);
      MmxRendererSetPeerView((int)seat);modern_image(name);
    }
    size_t n=RtlSaveSnapshotToMemory(start,cap);coop_frames(24,0,0);
    size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"independent camera history restores");
    coop_frames(24,0,0);
    same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"independent bounds replay byte-exactly");
  }
  puts(wrap?"MMX ISSUE110 WRAP REPLAY CHECKS PASSED":"MMX ISSUE110 REGION REPLAY CHECKS PASSED");
}
static void coop_view_boot_checks(const char *assets,uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  check(assets && MmxZeroLoad(assets),"online world test loads owned X3 source");
  MmxZeroRegisterHooks();MmxCoopRegisterHooks();check(MmxCoopEnable(0),"online world roster enables");
  zero_replay(1400);
  for(unsigned n=0;n<3;++n) {
    coop_frames(30,SNES_PAD_START,0);if(n<2) zero_replay(181);
  }
  unsigned wait;
  for(wait=0;wait<3600;++wait) {
    MmxCoopState s=MmxCoopGetState();
    if(g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4 &&
        s.initialized && s.players[1].status==MMX_COOP_ALIVE && !s.stage_pending && !MmxCoopTransitionActive()) break;
    coop_frames(1,0,0);
  }
  check(wait<3600,"retail boot reaches Highway with both actors");
  MmxCoopState s=MmxCoopGetState();unsigned initial=coop_x(&s.players[0]);
  unsigned remote=0;
  for(unsigned t=0;t<400;++t) {
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopSelect(g_ram,seat);g_ram[0xc30]=g_ram[0xc32]=255;MmxCoopCapture(g_ram);
    }
    MmxCoopSelect(g_ram,0);coop_frames(1,0,SNES_PAD_RIGHT|SNES_PAD_Y);
    s=MmxCoopGetState();
    MmxCoopView a=MmxCoopViewForPlayer(g_ram,&s,0),b=MmxCoopViewForPlayer(g_ram,&s,1);
    for(unsigned d=0xe68;d<0x1228;d+=64) {
      unsigned flag=eagle_word(g_ram+d+12),x=eagle_word(g_ram+d+5),y=eagle_word(g_ram+d+8);
      if(g_ram[d] && (g_ram[d+0x27]&127) && flag>=0xfa00 && flag<=0xfffb &&
          g_ram[d+14] && (int)x>=a.x+320 && (int)x<b.x+320 && (int)y>=b.y-64 && (int)y<b.y+288) remote=d;
    }
  }
  fprintf(stderr,"boot views x=%u,%u y=%u,%u hp=%u,%u remote=%x\n",coop_x(&s.players[0]),coop_x(&s.players[1]),
      coop_y(&s.players[0]),coop_y(&s.players[1]),s.players[0].body[0x27]&127,s.players[1].body[0x27]&127,remote);
  check(coop_x(&s.players[0])==initial && coop_x(&s.players[1])>initial+400,
      "native early Highway physics permit independent progress");
  check(remote,"native enemies update visibly outside the idle player's activation rectangle");
  MmxRendererSetPeerView(1);modern_image("splitless-highway");
  size_t n=RtlSaveSnapshotToMemory(start,cap);coop_frames(24,0,SNES_PAD_RIGHT|SNES_PAD_Y);
  size_t en=RtlSaveSnapshotToMemory(expected,cap);check(RtlLoadSnapshotFromMemory(start,n),"remote enemy world snapshot restores");
  coop_frames(24,0,SNES_PAD_RIGHT|SNES_PAD_Y);
  same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"remote native AI, spawns and shots replay together");
  check(RtlLoadSnapshotFromMemory(start,n),"restore remote encounter before retirement");
  unsigned flag=eagle_word(g_ram+remote+12);
  fprintf(stderr,"remote source=%x id=%x seen=%u live=%u\n",flag,g_ram[remote+10],MmxCoopViewsSeen(flag),g_ram[remote]);
  check(MmxCoopViewsSeen(flag),"native remote allocation enters rollback-owned spawn history");
  g_ram[flag]=0;memset(g_ram+remote,0,64);coop_frames(2,0,0);
  unsigned duplicates=0;
  for(unsigned d=0xe68;d<0x1228;d+=64) duplicates+=g_ram[d] && eagle_word(g_ram+d+12)==flag;
  check(!duplicates && !g_ram[flag],"stationary dual-view rescan does not respawn a retired native enemy");
  puts("MMX CO-OP INDEPENDENT WORLD BOOT CHECKS PASSED");
}
static void coop_view_checks(const char *fixture,uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  coop_followup_fixture(fixture);g_mmx_custom_renderer=true;g_mmx_expanded_sprites=true;
  MmxCoopState s=MmxCoopGetState();
  fprintf(stderr,"views stage=%u bounds=%u,%u / %u,%u actors=%u,%u y=%u,%u\n",g_ram[0x1f7a],
      eagle_word(g_ram+0x1e58),eagle_word(g_ram+0x1e56),eagle_word(g_ram+0x1e5a),eagle_word(g_ram+0x1e5c),
      coop_x(&s.players[0]),coop_x(&s.players[1]),coop_y(&s.players[0]),coop_y(&s.players[1]));
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);g_ram[0xc30]=255;g_ram[0xc32]=255;
    if(g_ram[0xbaa]==14) {g_ram[0xbaa]=g_ram[0xbab]=0;}
    MmxCoopCapture(g_ram);
  }
  MmxCoopSelect(g_ram,0);
  unsigned initial=coop_x(&s.players[0]);
  for(unsigned t=0;t<240;++t) {
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopSelect(g_ram,seat);g_ram[0xc30]=g_ram[0xc32]=255;MmxCoopCapture(g_ram);
    }
    MmxCoopSelect(g_ram,0);
    coop_frames(1,0,SNES_PAD_RIGHT|SNES_PAD_Y);s=MmxCoopGetState();
    if(t%60==59) fprintf(stderr,"view walk %u x=%u,%u y=%u,%u hp=%u,%u camera=%u,%u seen=%u\n",t,
        coop_x(&s.players[0]),coop_x(&s.players[1]),coop_y(&s.players[0]),coop_y(&s.players[1]),
        s.players[0].body[0x27]&127,s.players[1].body[0x27]&127,eagle_word(g_ram+0x1e4d),
        eagle_word(g_ram+0x1e50),MmxCoopViewsGetWorldState().initialized);
  }
  check(coop_x(&s.players[0])==initial,"splitless companion movement does not drag P1");
  check(coop_x(&s.players[1])>initial+300,"netplay player crosses former 224px separation limit");
  check(s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE,
      "separated actors retain native physics and health");
  MmxCoopView a=MmxCoopViewForPlayer(g_ram,&s,0),b=MmxCoopViewForPlayer(g_ram,&s,1);
  check(b.x>a.x+250,"two peers receive independent camera origins");
  check(MmxCoopViewContains(g_ram,&s,(int)coop_x(&s.players[1]),(int)coop_y(&s.players[1]),64,320,64,288,0),
      "remote player remains within shared world activation");
  static uint8_t shaft[0x20000];memcpy(shaft,g_ram,sizeof(shaft));
  MmxCoopState separated=s;separated.current=separated.anchor=0;
  eagle_putword(shaft+0x1e4d,0);eagle_putword(shaft+0x1e50,0);
  eagle_putword(shaft+0x1e56,0);eagle_putword(shaft+0x1e58,2048);
  eagle_putword(shaft+0x1e5a,0);eagle_putword(shaft+0x1e5c,2048);
  eagle_putword(separated.players[1].body+5,1000);eagle_putword(separated.players[1].body+8,1300);
  /* This artificial shaft is not Highway. The real per-player limit history
   * still describes Highway's fixed Y=256 and correctly overrides WRAM when
   * valid. Supply this fixture's own history, then restore the live world's.
   * Cover the WRAM fallback and valid history without relaxing either check. */
  MmxCoopViewBoundsState live_bounds=MmxCoopViewsGetBoundsState();
  MmxCoopViewBoundsState shaft_bounds={0};shaft_bounds.stage=shaft[0x1f7a];
  for(unsigned seat=0;seat<2;++seat) {
    shaft_bounds.limits[seat][1]=shaft_bounds.limits[seat][3]=2048;
  }
  for(unsigned history=0;history<2;++history) {
    shaft_bounds.valid=history?3:0;MmxCoopViewsSetBoundsState(&shaft_bounds);
    b=MmxCoopViewForPlayer(shaft,&separated,1);
    check(b.y>1000 && MmxCoopViewContains(shaft,&separated,1000,1300,64,320,64,288,0),
        "vertical separation activates the remote player's shaft");
    check(!MmxCoopViewContains(shaft,&separated,128,1300,64,320,64,288,0),
        "activation does not combine one player's X with the other player's Y");
  }
  MmxCoopViewsSetBoundsState(&live_bounds);
  static uint32_t views[2][342*224];
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"independent-view snapshot saves");
  for(unsigned seat=0;seat<2;++seat) {
    MmxRendererSetPeerView((int)seat);
    check(MmxRendererDraw(views[seat],(MmxRenderView){342,43,16.0/9.0},true),"peer frame renders");
    same(start,n,actual,RtlSaveSnapshotToMemory(actual,cap),"view selection leaves simulation snapshot untouched");
    const char *base=getenv("MMX_ZERO_TEST_CAPTURE");
    if(base) {char path[1024];snprintf(path,sizeof(path),"%s.peer-%u.bmp",base,seat);
      SDL_Surface *surface=SDL_CreateSurfaceFrom(342,224,SDL_PIXELFORMAT_ARGB8888,views[seat],342*4);
      check(surface && SDL_SaveBMP(surface,path),"peer frame capture writes");SDL_DestroySurface(surface);}
  }
  check(memcmp(views[0],views[1],sizeof(views[0]))!=0,"different peer origins show different world pixels");
  MmxRendererSetPeerView(0);coop_frames(12,0,SNES_PAD_RIGHT|SNES_PAD_Y);
  size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"independent world/placement history restores");
  MmxRendererSetPeerView(1);coop_frames(12,0,SNES_PAD_RIGHT|SNES_PAD_Y);
  same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"different presentation seats replay the same world");
  s=MmxCoopGetState();
  for(unsigned owner=0;owner<2;++owner) {
    MmxCoopState scene=s;scene.anchor=(uint8_t)owner;scene.scene_owner=(uint8_t)(owner+1);scene.scene_phase=1;
    scene.players[owner^1].zero.swap_phase=1;
    a=MmxCoopViewForPlayer(g_ram,&scene,owner);b=MmxCoopViewForPlayer(g_ram,&scene,owner^1);
    check(a.x==b.x && a.y==b.y && a.player==owner && b.player==owner,
        "transported partner immediately snaps to either scene owner's camera");
    scene=s;scene.anchor=(uint8_t)owner;scene.players[owner^1].status=MMX_COOP_FALLEN;
    b=MmxCoopViewForPlayer(g_ram,&scene,owner^1);
    check(b.player==owner && b.x==eagle_word(g_ram+0x1e4d),"dead seat follows the living survivor");
  }
  for(unsigned owner=0;owner<2;++owner) {
    check(RtlLoadSnapshotFromMemory(start,n),"restore separated scene approach");
    MmxCoopSelect(g_ram,owner);s=MmxCoopGetState();s.anchor=(uint8_t)owner;MmxCoopSetState(&s);
    unsigned hp=s.players[owner^1].body[0x27]&127;
    g_ram[0x1f0c]=1;coop_frames(1,0,0);s=MmxCoopGetState();
    check(s.scene_owner==owner+1,"either separated player begins scene transport");
    a=MmxCoopViewForPlayer(g_ram,&s,owner);b=MmxCoopViewForPlayer(g_ram,&s,owner^1);
    check(a.x==b.x && a.y==b.y && b.player==owner,"camera snaps on the first departure frame");
    for(unsigned t=0;t<90 && MmxCoopGetState().scene_phase==1;++t) coop_frames(1,0,0);
    check(MmxCoopGetState().scene_phase==2,"separated partner completes its departure");
    g_ram[0x1f0c]=0;
    for(unsigned t=0;t<120 && MmxCoopGetState().scene_owner;++t) coop_frames(1,0,0);
    s=MmxCoopGetState();
    check(!s.scene_owner && (s.players[owner^1].body[0x27]&127)==hp &&
        abs((int)coop_x(&s.players[0])-(int)coop_x(&s.players[1]))<=64,
        "scene exit returns distant partner beside owner with its HP intact");
  }
  MmxCoopViewsSetOnline(false);a=MmxCoopViewForPlayer(g_ram,&s,1);
  check(a.x==eagle_word(g_ram+0x1e4d) && a.y==eagle_word(g_ram+0x1e50),"couch co-op retains its shared camera");
  MmxRendererSetPeerView(-1);
  puts("MMX CO-OP INDEPENDENT VIEW CHECKS PASSED");
}
