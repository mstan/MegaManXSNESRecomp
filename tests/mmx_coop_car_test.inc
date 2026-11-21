/* Highway's road attacker owns one moving car and a separate solid roof.
 * Exercise the real cartridge roof contact/carry helper after a fresh boot;
 * actor placement only establishes the encounter, never its rider result. */
static void highway_car_call(unsigned pc,unsigned d,bool long_call) {
  CpuState cpu=g_cpu;cpu.S=0x1ff;cpu.D=(uint16_t)d;cpu.DB=0x86;cpu.PB=0x81;
  cpu.P=0x30;cpu.emulation=0;cpu_p_to_mirrors(&cpu);
  if(long_call)cpu_push_jsl_return_frame(&cpu);else cpu_push_jsr_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  check(interp_bridge_run(&cpu,pc) && cpu.S==0x1ff,"native road attacker helper returns with balanced stack");
}
static void coop_highway_car_checks(const char *assets) {
  static CoopReplay route;
  const char *inputs=getenv("MMX_COOP_CAR_INPUTS");
  check(assets && inputs,"Highway car check requires owned assets and a cold input route");
  coop_replay_load(inputs,&route);coop_replay_start(assets);
  check(MmxCoopEnable(MMX_COOP_ZERO),"Highway car uses P1 Zero and P2 X");
  coop_replay_run(&route,0,route.ticks);
  check(g_ram[0x1f7a]==0 && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4,
      "cold native input route reaches real Highway gameplay");
  unsigned roof=0;
  for(unsigned d=0xe68;d<0x1228;d+=64)
    if(g_ram[d] && g_ram[d+10]==0x12 && g_ram[d+1]>=4 && g_ram[d+1]!=10)roof=d;
  check(roof!=0,"cold native route creates the real rideable car roof");
  unsigned car=eagle_word(g_ram+roof+0x33);
  check(car>=0xe68 && car<0x1228 && g_ram[car] && g_ram[car+10]==0x11,
      "native roof retains its real moving car owner");
  unsigned x=eagle_word(g_ram+roof+5),y=eagle_word(g_ram+roof+8);
  fprintf(stderr,"car=%04x roof=%04x pos=%u/%u hitbox=%04x state=%02x%02x%02x\n",car,roof,x,y,eagle_word(g_ram+roof+0x20),g_ram[roof],g_ram[roof+1],g_ram[roof+2]);
  /* Route progress is a prerequisite only. Place/rescue P2 explicitly, then
   * let the real car and support helper decide every rider/carry result. */
  check(coop_place_fixture_partner(x+8,y-18),"current native car fixture places a live partner");
  for(unsigned seat=0;seat<2;++seat) {
    check(MmxCoopSelect(g_ram,seat),"car fixture projects each live body");
    eagle_putword(g_ram+0xbad,x+(seat?8:0));eagle_putword(g_ram+0xbca,x+(seat?8:0));
    eagle_putword(g_ram+0xbb0,y-18);eagle_putword(g_ram+0xbcc,y-20);
    eagle_putword(g_ram+0xbc4,0xfd00);g_ram[0xbaa]=8;g_ram[0xbab]=2;g_ram[0xbd3]=g_ram[0xbd4]=0;
    g_ram[0xbcf]=16;MmxZeroCancel(g_ram);MmxWeaponsCancelShots(g_ram);
  }
  check(MmxCoopSelect(g_ram,0),"car fixture restores the world body");
  g_ram[roof+0x2c]=0;eagle_putword(g_ram+roof+0x22,x);eagle_putword(g_ram+roof+0x24,y);
  highway_car_call(0x81de1c,roof,false);
  MmxCoopState s=MmxCoopGetState();
  fprintf(stderr,"car riders=%u p1=%u/%u p2=%u/%u ground=%02x/%02x\n",g_ram[roof+0x2c],coop_x(&s.players[0]),coop_y(&s.players[0]),coop_x(&s.players[1]),coop_y(&s.players[1]),s.players[0].body[0x2c],s.players[1].body[0x2c]);
  check(g_ram[roof+0x2c]==1,"car landing retains native scalar suspension");
  unsigned standing=eagle_word(g_ram+0xbb0);
  const size_t cap=2u*1024u*1024u;
  uint8_t *start=malloc(cap),*expected=malloc(cap),*actual=malloc(cap);
  check(start && expected && actual,"moving car snapshots allocate");
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  /* The old .2C byte is tested as a whole by the ROM. Only the seat that
   * rode previously may inherit the car's displacement, in either order. */
  for(unsigned first=0;first<2;++first)for(unsigned selection=0;selection<3;++selection) {
    unsigned mask=selection==0?2:selection==1?1:3;
    check(RtlLoadSnapshotFromMemory(start,n),"fresh current car encounter restores");
    for(unsigned seat=0;seat<2;++seat) {
      check(MmxCoopSelect(g_ram,seat),"rider control projects the intended seat");
      eagle_putword(g_ram+0xbad,x+((mask&(1u<<seat))?seat*8:80));
      eagle_putword(g_ram+0xbb0,(mask&(1u<<seat))?standing:y-80);
      g_ram[0xbd3]=g_ram[0xbd4]=0;
    }
    check(MmxCoopSelect(g_ram,first),"rider control restores its native owner");
    s=MmxCoopGetState();s.anchor=(uint8_t)first;MmxCoopSetState(&s);
    g_ram[roof+0x2c]=1;g_ram[roof+0x3f]=(uint8_t)mask;
    eagle_putword(g_ram+roof+0x22,x);eagle_putword(g_ram+roof+0x24,y);
    eagle_putword(g_ram+roof+5,x+4);eagle_putword(g_ram+roof+8,y+2);
    highway_car_call(0x81de1c,roof,false);
    check(MmxCoopSelect(g_ram,first^1) && MmxCoopSelect(g_ram,first),"native car results synchronize without changing ownership");
    s=MmxCoopGetState();
    for(unsigned seat=0;seat<2;++seat) {
      bool riding=(mask&(1u<<seat))!=0;
      check(coop_x(&s.players[seat])==x+(riding?seat*8+4:80),
          "car carries only an actual previous rider horizontally");
      check(coop_y(&s.players[seat])==(riding?standing+2:y-80),
          "car carries only an actual previous rider vertically");
    }
    check(g_ram[roof+0x2c]==1 && g_ram[roof+0x3f]==mask && !s.contact_pass && s.current==first,
        "car combines rider bits and restores its native owner");
  }
  check(RtlLoadSnapshotFromMemory(start,n),"normal moving car starts from its fresh encounter");
  /* Keep the car's enemy pilot from turning a support check into combat.
   * The native support/carry and vehicle state still run normally. */
  for(unsigned seat=0;seat<2;++seat) {
    check(MmxCoopSelect(g_ram,seat),"car route protects each test-owned player");
    g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=240;
  }
  check(MmxCoopSelect(g_ram,0),"car route restores its world owner");
  /* The real report has X riding while Zero remains off the car. With both
   * directly above it, the enemy AI deliberately stops at its target. */
  eagle_putword(g_ram+0xbad,x-90);eagle_putword(g_ram+0xbca,x-90);
  eagle_putword(g_ram+0xbb0,y-20);eagle_putword(g_ram+0xbcc,y-20);
  g_ram[0xbaa]=g_ram[0xbab]=0;g_ram[0xbd3]=4;g_ram[0xbd4]=0;
  unsigned moved=0,both=0;
  for(unsigned t=0;t<10;++t) {
    unsigned before=eagle_word(g_ram+roof+5);frame(0);s=MmxCoopGetState();
    moved+=eagle_word(g_ram+roof+5)!=before;
    both+=(g_ram[roof+0x3f]&3)==2;
    if(t%30==29)fprintf(stderr,"car t=%u state=%02x%02x%02x pos=%u/%u riders=%u hp=%u/%u\n",t,g_ram[car],g_ram[car+1],g_ram[car+2],eagle_word(g_ram+car+5),eagle_word(g_ram+car+8),g_ram[roof+0x2c],s.players[0].body[0x27],s.players[1].body[0x27]);
  }
  fprintf(stderr,"normal car moved=%u both=%u x=%u p1=%u p2=%u\n",moved,both,eagle_word(g_ram+car+5),coop_x(&s.players[0]),coop_x(&s.players[1]));
  check(moved>5 && both>5 && (s.players[0].body[0x27]&127) && (s.players[1].body[0x27]&127),
      "living P2 rides the actual moving Highway car while living P1 stays off");
  check(coop_x(&s.players[1])==eagle_word(g_ram+roof+5)+8,
      "native car motion preserves the passenger offset");
  g_config.widescreen=true;g_mmx_custom_aspect=MMX_ASPECT_ADAPTIVE;
  g_mmx_custom_view=MmxRendererViewport(MMX_ASPECT_ADAPTIVE,1280,720,kSnesDisplayAspect_Crt4x3);
  frame(0);
  check(g_mmx_custom_view.width==342,"current moving-car field uses adaptive Fit geometry");
  uint32_t *picture=malloc((size_t)g_mmx_custom_view.width*224*4);
  check(picture!=NULL,"adaptive car picture allocates");
  memcpy(actual,g_ram,0x20000);
  check(MmxRendererDraw(picture,g_mmx_custom_view,false) && !memcmp(actual,g_ram,0x20000),
      "adaptive moving-car picture leaves native gameplay memory unchanged");
  free(picture);owner_wide_image("highway-car");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".highway-car.cap");
  n=RtlSaveSnapshotToMemory(start,cap);coop_frames(20,SNES_PAD_LEFT,0);
  size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"current-build moving car state loads");
  coop_frames(20,SNES_PAD_LEFT,0);
  same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"moving car continuation repeats exactly after load");
  s=MmxCoopGetState();
  check((s.players[0].body[0x27]&127) && (s.players[1].body[0x27]&127),
      "both test players remain alive through the moving-car continuation");
  s=MmxCoopGetState();s.players[1].status=MMX_COOP_ABSENT;MmxCoopSetState(&s);
  highway_car_call(0x81de1c,roof,false);
  check(g_ram[roof+0x2c]==0 && g_ram[roof+0x3f]==0,
      "a withdrawn passenger no longer owns the native car suspension");
  /* Recycled object memory is real input to the native constructor. */
  g_ram[roof+1]=0;g_ram[roof+0x3f]=3;
  highway_car_call(0x81dd2d,roof,false);
  check(g_ram[roof+0x3f]==0,"native roof constructor discards recycled rider ownership");
  free(start);free(expected);free(actual);
}
