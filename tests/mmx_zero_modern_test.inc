/* Private ROM fixtures exercise native movement, projection and rollback. */
static unsigned modern_x(void) { return g_ram[0xbad] | g_ram[0xbae] << 8; }
static unsigned modern_y(void) { return g_ram[0xbb0] | g_ram[0xbb1] << 8; }
static void modern_fixture(const char *fixture) {
  MmxCoopDisable();
  check(RtlLoadSnapshot(fixture), "legacy grounded fixture loads in Modern mode");
  MmxZeroSetModern(true); MmxZeroResetState();
  zero_replay(8);
}
static void modern_image(const char *suffix) {
  const char *base = getenv("MMX_ZERO_TEST_CAPTURE");
  if (!base) return;
  uint32_t pixels[256*224];
  check(MmxRendererDraw(pixels,(MmxRenderView){256,0,4.0/3.0},false),"Modern frame renders");
  char path[4096]; snprintf(path,sizeof(path),"%s.%s.bmp",base,suffix);
  FILE *f=fopen(path,"wb"); check(f!=NULL,"Modern image opens");
  uint8_t signature[2]={'B','M'};
  uint32_t header[]={54+sizeof(pixels),0,54,40,256,224,0x200001,0,sizeof(pixels),0,0,0,0};
  fwrite(signature,2,1,f); fwrite(header,sizeof(header),1,f);
  for(int y=223;y>=0;--y) fwrite(pixels+y*256,1024,1,f);
  fclose(f);
}
static void modern_checks(const char *assets,const char *fixture,uint8 *start,
                          uint8 *expected,uint8 *actual,size_t cap) {
  check(assets && fixture && MmxZeroLoad(assets),"Modern assets load");
  MmxZeroRegisterHooks(); MmxCoopRegisterHooks();
  g_config.widescreen=false; int w,h; MmxPrepareFrame(1280,720,&w,&h);
  if(getenv("MMX_ZERO_MOVEMENT_CANCEL_TEST")) {
    modern_fixture(fixture);unsigned ground=modern_y();
    frame(SNES_PAD_Y);zero_replay(4);
    check(MmxZeroGetState().slash,"ground saber is active before jump cancel");
    frame(SNES_PAD_B);frame(SNES_PAD_B);
    check(!MmxZeroGetState().slash && modern_y()<ground,
        "ground jump cancels saber and preserves the native jump input");
    modern_fixture(fixture);unsigned x=modern_x();
    frame(SNES_PAD_Y);zero_replay(4);frame(SNES_PAD_A|SNES_PAD_RIGHT);frame(SNES_PAD_A|SNES_PAD_RIGHT);
    check(!MmxZeroGetState().slash && modern_x()>x,
        "ground dash cancels saber and starts native movement");
    modern_fixture(fixture);x=modern_x();
    frame(SNES_PAD_Y|SNES_PAD_A|SNES_PAD_RIGHT);frame(SNES_PAD_A|SNES_PAD_RIGHT);
    check(!MmxZeroGetState().slash && modern_x()>x,
        "simultaneous ground dash and saber starts native dash");
    modern_fixture(fixture);frame(SNES_PAD_Y);
    check(MmxZeroGetState().slash,"holding saber starts one swing");
    bool recovered=false,repeated=false;
    for(unsigned i=0;i<90;++i) {
      frame(SNES_PAD_Y);
      bool slashing=MmxZeroGetState().slash!=0;
      repeated |= recovered && slashing;
      if(!slashing)recovered=true;
    }
    check(recovered && !repeated,"holding saber past recovery does not start another swing");
    frame(0);frame(SNES_PAD_Y);
    check(MmxZeroGetState().slash,"release and new press starts the next swing");
    modern_fixture(fixture);ground=modern_y();
    for(unsigned i=0;i<4;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
    check(g_ram[0xbaa]==0x14,"ground dash starts before the jump");
    for(unsigned i=0;i<3;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    unsigned rising=modern_y();
    check(rising<ground,"native ground dash-jump is already rising before slash");
    size_t dash_n=RtlSaveSnapshotToMemory(start,cap);
    for(unsigned i=0;i<8;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    unsigned dash_x=modern_x(),dash_y=modern_y();
    check(dash_y<rising && RtlLoadSnapshotFromMemory(start,dash_n),
        "restore exact current-build rising dash-jump");
    frame(SNES_PAD_Y|SNES_PAD_B|SNES_PAD_RIGHT);
    for(unsigned i=1;i<8;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    check(MmxZeroGetState().slash && modern_x()==dash_x && modern_y()==dash_y,
        "air slash preserves the rising native ground dash-jump trajectory");
    modern_fixture(fixture);
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_B);
    frame(SNES_PAD_Y);zero_replay(3);
    check(MmxZeroGetState().slash,"air saber is active before ladder input");
    frame(SNES_PAD_UP);
    check(!MmxZeroGetState().slash && (g_ram[0xbdf]&8),
        "ladder input cancels swing without losing native Up input");
    modern_fixture(fixture);
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_B);
    frame(SNES_PAD_Y);zero_replay(3);frame(SNES_PAD_A|SNES_PAD_RIGHT);
    check(!MmxZeroGetState().slash && MmxZeroGetState().modern.dash_ticks &&
        MmxZeroGetState().modern.dash_used,
        "air dash cancels saber without refreshing the aerial allowance");
    modern_fixture(fixture);ground=modern_y();
    g_ram[0xbb0]=(uint8_t)(ground-4);g_ram[0xbb1]=(uint8_t)((ground-4)>>8);
    g_ram[0xbaa]=8;g_ram[0xbab]=0;g_ram[0xbd3]=0;
    frame(SNES_PAD_Y);zero_replay(8);
    check((g_ram[0xbd3]&4) && MmxZeroGetState().slash && modern_y()==ground,
        "landing alone retains the active saber swing");
    puts("MMX MODERN SABER MOVEMENT CHECKS PASSED");return;
  }
  /* Compare against the same airborne trajectory without attacking. These
   * exercise the native input-to-velocity controller, not just adapter RAM. */
  for(unsigned dash=0;dash<2;++dash) {
    modern_fixture(fixture);
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    if(dash) for(unsigned i=0;i<4;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
    size_t before_n=RtlSaveSnapshotToMemory(start,cap);
    for(unsigned i=0;i<8;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    unsigned expected_x=modern_x(),expected_y=modern_y();
    check(RtlLoadSnapshotFromMemory(start,before_n),"restore moving jump before saber");
    frame(SNES_PAD_Y|SNES_PAD_B|SNES_PAD_RIGHT);
    for(unsigned i=1;i<8;++i) frame(SNES_PAD_B|SNES_PAD_RIGHT);
    printf("air slash dash=%u xy=%u,%u reference=%u,%u\n",dash,modern_x(),modern_y(),expected_x,expected_y);
    check(MmxZeroGetState().slash && modern_x()==expected_x && modern_y()==expected_y,
        "air saber preserves horizontal movement and native vertical arc");
    unsigned x=modern_x(); frame(SNES_PAD_B|SNES_PAD_LEFT);
    check(modern_x()<x && MmxZeroGetState().slash,"air saber permits steering back toward a platform");
    unsigned d=MmxZeroGetState().projectile;
    check(d && (g_ram[d+5]|g_ram[d+6]<<8)==modern_x() &&
        (g_ram[d+8]|g_ram[d+9]<<8)==modern_y() &&
        (g_ram[d+0x11]&64)==(g_ram[0xbb9]&64),
        "moving saber collision follows the rendered position and facing");
    modern_image(dash?"dash-saber-motion":"jump-saber-motion");
  }
  modern_fixture(fixture);
  frame(SNES_PAD_Y); zero_replay(10);
  check(MmxZeroGetState().slash && !MmxZeroGetState().charge,"tap immediately swings without charging");
  modern_image("saber");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".modern.cap");
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  zero_replay(12); size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"Modern single-player save loads");
  zero_replay(12); size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"saber contact windows replay deterministically");
  modern_fixture(fixture);
  unsigned ground=modern_y();
  for(unsigned i=0;i<12;++i) frame(SNES_PAD_B);
  frame(0); unsigned first=modern_y(); frame(SNES_PAD_B);
  check(MmxZeroGetState().modern.jump_used && modern_y()<first,"second press performs an aerial jump");
  for(unsigned i=0;i<10;++i) frame(SNES_PAD_B);
  check(modern_y()+60<ground,"double jump gains height");
  frame(0); frame(SNES_PAD_B);
  check((int16_t)(g_ram[0xbc4]|g_ram[0xbc5]<<8)<0x500,"third jump does not reset upward velocity");
  modern_image("double-jump");
  frame(0); unsigned x=modern_x(),y=modern_y();
  frame(SNES_PAD_A|SNES_PAD_RIGHT);
  for(unsigned i=0;i<8;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
  printf("dash: dx=%d dy=%d action=%x remaining=%u flags=%x/%x\n",(int)modern_x()-x,(int)modern_y()-y,g_ram[0xbaa],MmxZeroGetState().modern.dash_ticks,g_ram[0xbd3],g_ram[0xc06]);
  check(MmxZeroGetState().modern.dash_ticks && modern_y()==y && modern_x()>x+24,"air dash moves horizontally and holds height");
  modern_image("air-dash");
  n=RtlRollbackSaveToMemory(start,cap);
  for(unsigned i=0;i<7;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
  en=RtlRollbackSaveToMemory(expected,cap);
  check(RtlRollbackLoadFromMemory(start,n),"air dash rollback restores");
  for(unsigned i=0;i<7;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
  an=RtlRollbackSaveToMemory(actual,cap);
  same(expected,en,actual,an,"air dash rollback is deterministic");
  frame(0); frame(SNES_PAD_A);
  check(!MmxZeroGetState().modern.dash_ticks && MmxZeroGetState().modern.dash_used,"release ends dash and repress cannot dash twice");
  zero_replay(90);
  check((g_ram[0xbd3]&4) && !MmxZeroGetState().modern.jump_used && !MmxZeroGetState().modern.dash_used,"landing refreshes both aerial abilities");
  modern_fixture(fixture);
  g_ram[0x1f88]=28;g_ram[0xbdb]=2;
  frame(SNES_PAD_Y); zero_replay(3);
  check(!MmxZeroGetState().slash && zero_projectiles(7),"X1 special weapon keeps its native projectile");
  const char *sources[2]={getenv("MMX_COOP_X2_ASSETS"),getenv("MMX_COOP_X3_ASSETS")};
  for(unsigned page=1;page<=2;++page) if(sources[page-1]) {
    check(MmxWeaponsLoadPage(sources[page-1],page),"imported weapon assets load with Modern");
    modern_fixture(fixture);
    MmxWeaponsState weapon=MmxWeaponsGetState();weapon.page=page;weapon.weapon=page==1?8:1;
    MmxWeaponsSetState(weapon);frame(SNES_PAD_Y);zero_replay(2);
    MmxWeaponCombatState combat=MmxWeaponsGetCombatState();unsigned shots=0;
    for(unsigned i=0;i<8;++i) shots+=combat.shots[i].active!=0;
    check(shots && !MmxZeroGetState().slash,"imported special fires instead of saber");
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_B);
    frame(0);frame(SNES_PAD_B);
    check(MmxZeroGetState().modern.jump_used,"extra jump works with imported special selected");
    frame(0);unsigned y=modern_y();
    for(unsigned i=0;i<6;++i) frame(SNES_PAD_A|SNES_PAD_RIGHT);
    check(MmxZeroGetState().modern.dash_ticks && modern_y()==y,"air dash works with imported special selected");
  }
  MmxWeaponsDisable();
  modern_fixture(fixture);
  for(unsigned zero_seat=0;zero_seat<2;++zero_seat) {
    modern_fixture(fixture);
    check(MmxCoopEnable(zero_seat?MMX_COOP_X:MMX_COOP_ZERO),"Modern co-op roster starts");
    coop_frames(8,0,0); MmxCoopState s=MmxCoopGetState();
    check(coop_place_fixture_partner(coop_x(&s.players[0])+32,coop_y(&s.players[0])),"Modern partner placed");
    coop_frames(40,0,0);
    coop_frames(12,SNES_PAD_B,SNES_PAD_B); coop_frames(1,0,0);
    coop_frames(1,SNES_PAD_B,SNES_PAD_B); s=MmxCoopGetState();
    check(s.players[zero_seat].zero.modern.jump_used && !s.players[zero_seat^1].zero.modern.jump_used,"extra jump belongs only to Zero in either seat");
    coop_frames(1,0,0);coop_frames(1,SNES_PAD_Y,SNES_PAD_Y);coop_frames(10,0,0);
    s=MmxCoopGetState();
    check(s.players[zero_seat].zero.slash && !s.players[zero_seat^1].zero.slash,"saber and X buster remain independent");
    coop_image(zero_seat,"modern");
    n=RtlRollbackSaveToMemory(start,cap);coop_frames(12,0,0);en=RtlRollbackSaveToMemory(expected,cap);
    check(RtlRollbackLoadFromMemory(start,n),"Modern co-op rollback restores");
    coop_frames(12,0,0);an=RtlRollbackSaveToMemory(actual,cap);
    same(expected,en,actual,an,"both seat contexts replay deterministically");
  }
  const char *old_coop=getenv("MMX_MODERN_COOP_FIXTURE");
  if(old_coop) {
    check(RtlLoadSnapshot(old_coop),"v14 co-op save loads into the extended layout");
    MmxCoopState old=MmxCoopGetState();
    check(old.initialized && MmxCoopValidState(&old),"legacy roster and contexts remain valid");
  }
  MmxCoopDisable();MmxZeroSetModern(false);
  puts("MMX MODERN ZERO CHECKS PASSED");
}
