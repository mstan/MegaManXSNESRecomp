/* Room transitions must move through the native two-pixel pan, rather than
 * jump to the target bounds. Run on both raster implementations/seats. */
static void independent_camera_pan(void) {
  memset(ram,0,sizeof(ram));ram[0xd3]=4;
  put_word(0x1e52,2);put_word(0x1e54,8);
  put_word(0x1e74,128);put_word(0x1e76,128);
  put_word(0x1e70,160);put_word(0x1e72,160);
  MmxCoopViewsSetActive(true);
  for(unsigned anchor=0;anchor<2;++anchor) for(unsigned reverse=0;reverse<2;++reverse) {
    MmxCoopState s={0};s.initialized=1;s.current=s.anchor=(uint8_t)anchor;
    for(unsigned seat=0;seat<2;++seat) {
      s.players[seat].status=MMX_COOP_ALIVE;s.players[seat].body[0x27]=16;
      unsigned position=reverse?32:800;
      for(unsigned off=5;off<=8;off+=3) {
        s.players[seat].body[off]=(uint8_t)position;
        s.players[seat].body[off+1]=(uint8_t)(position>>8);
        unsigned previous=off==5?0x22:0x24;
        s.players[seat].body[previous]=(uint8_t)position;
        s.players[seat].body[previous+1]=(uint8_t)(position>>8);
      }
    }
    MmxCoopViewBoundsState b={0};b.valid=b.camera_valid=3;
    unsigned initial=reverse?400:100,target=reverse?100:400;
    for(unsigned seat=0;seat<2;++seat) {
      b.camera[seat][0]=b.camera[seat][1]=(uint16_t)initial;
      for(unsigned i=0;i<4;++i) {b.current[seat][i]=(uint16_t)initial;b.limits[seat][i]=(uint16_t)target;}
    }
    MmxCoopViewsSetBoundsState(&b);
    for(unsigned tick=0;tick<180;++tick) {
      MmxCoopViewsEndFrame(ram,&s);
      MmxCoopView v=MmxCoopViewForPlayer(ram,&s,anchor^1);
      int expected=reverse?(int)initial-(int)(tick*2):(int)initial+(int)(tick*2);
      if(reverse && expected<(int)target) expected=(int)target;
      if(!reverse && expected>(int)target) expected=(int)target;
      if(v.x!=expected || v.y!=expected) fprintf(stderr,"camera pan anchor=%u reverse=%u tick=%u xy=%d,%d expected=%d\n",anchor,reverse,tick,v.x,v.y,expected);
      assert(v.x==expected && v.y==expected);
    }
    b=MmxCoopViewsGetBoundsState();assert(MmxCoopViewsValidBoundsState(&b));
    s.menu_owner=1;MmxCoopViewsEndFrame(ram,&s);
    MmxCoopViewBoundsState paused=MmxCoopViewsGetBoundsState();
    assert(!memcmp(&b,&paused,sizeof(b)));
    s.menu_owner=0;s.scene_owner=1;MmxCoopViewsEndFrame(ram,&s);
    assert(!MmxCoopViewsGetBoundsState().camera_valid);
    MmxCoopViewsSetBoundsState(&b);
    assert(MmxCoopViewForPlayer(ram,&s,anchor^1).x==0); /* shared script */
  }
  MmxCoopViewsResetWorld();MmxCoopViewsSetActive(false);
}
