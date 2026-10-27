static void wide_penguin_blizzard(void) {
  memset(&ppu,0,sizeof(ppu));memset(ram,0,sizeof(ram));memset(rom_bytes,0,sizeof(rom_bytes));
  MmxRendererReset();MmxRendererSetRom(NULL,0);g_mmx_custom_renderer=true;
  ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;
  ppu.inidisp=15;ppu.bgmode=9;ppu.screenEnabled[0]=4;ppu.bgXsc[2]=8;
  ppu.cgram[1]=0x7fff;ppu.cgram[2]=31<<5;
  for(unsigned i=0;i<1024;++i)ppu.vram[0x800+i]=i&1?1:2;
  for(unsigned y=0;y<8;++y) {ppu.vram[8+y]=0x55;ppu.vram[16+y]=0xaa00;}
  for(unsigned rush=0;rush<2;++rush) {
    ram[0x1f7a]=rush?4:8;MmxBossRushReset();
    if(rush) {
      MmxBossRushStart(true,1);assert(MmxBossRushAssign(0,0,0xe68));
      MmxBossRushState s=MmxBossRushGetState();s.mode=MMX_RUSH_PLAYING;
      assert(MmxBossRushSetState(&s));
    }
    for(unsigned aspect=0;aspect<4;++aspect)for(unsigned phase=0;phase<2;++phase) {
      MmxRenderView v=aspect==0?(MmxRenderView){256,0,4.0/3.0}:
          MmxRendererViewport(MMX_ASPECT_ADAPTIVE,aspect==1?1920:aspect==2?3840:10000,1080,kSnesDisplayAspect_Crt4x3);
      ram[0x1f3f]=120;ppu.hScroll[2]=(uint16_t)(phase?251:3);ppu.vScroll[2]=phase?5:0;
      capture();assert(MmxRendererDraw(output,v,false));
      for(unsigned y=0;y<224;++y)for(int x=0;x<v.width;++x) {
        unsigned center=(unsigned)(x-v.extra)&255;
        assert(output[y*v.width+x]==output[y*v.width+v.extra+center]);
      }
      /* Dialogue and an ended attack retain their bounded BG3 plane. */
      ram[0x1f3f]=0;capture();assert(MmxRendererDraw(output,v,false));
      for(int x=0;x<v.width;++x)if(x<v.extra || x>=v.extra+256)assert(!output[48*v.width+x]);
    }
  }
  MmxBossRushReset();g_mmx_custom_renderer=false;MmxRendererReset();
}

static bool overlap_overlay(const uint8_t *r,const MmxZeroState *z,MmxRenderPlayerOverlay *out) {
  (void)r;
  static uint8_t pixels[80];static const uint16_t colors[]={0,31};
  if(z->active_x)return false;
  memset(pixels,1,sizeof(pixels));memset(out,0,sizeof(*out));out->active=true;
  out->body=(MmxRenderPlayerOverlayPlane){pixels,10,8,62,64};
  out->palette=colors;out->palette_count=2;return true;
}
/* The caller has loaded the small synthetic Zero sheet used below. */
static void coop_player_overlap(void) {
  memset(rom_bytes,0,sizeof(rom_bytes));
  rom_long(0x68000,0x8d9000);rom_long(0x69000,0x8d9100);
  rom_bytes[0x69100]=1;
  MmxRendererSetRom(NULL,0);MmxRendererSetRom(rom_bytes,sizeof(rom_bytes));
  MmxCoopViewsSetOnline(false);g_mmx_custom_renderer=true;g_mmx_expanded_sprites=false;
  for(unsigned current=0;current<2;++current)for(unsigned repairs=0;repairs<2;++repairs)
  for(unsigned overlay=0;overlay<2;++overlay)for(unsigned wide=0;wide<2;++wide) {
    memset(&ppu,0,sizeof(ppu));memset(ram,0,sizeof(ram));memset(stock,0,sizeof(stock));
    MmxRendererReset();g_mmx_render_asset_repairs=repairs!=0;
    MmxRendererSetPlayerOverlayProvider(overlay?overlap_overlay:NULL);
    ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;
    ppu.inidisp=15;ppu.bgmode=1;ppu.screenEnabled[0]=16;ppu.cgram[129]=31<<10;
    for(unsigned i=0;i<128;++i)ppu.oam[i*2]=0xe000;
    for(unsigned y=0;y<8;++y)ppu.vram[y]=0xf0;
    MmxCoopState c={0};c.initialized=1;c.current=c.anchor=(uint8_t)current;
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopPlayer *p=&c.players[seat];p->character=(uint8_t)seat;p->status=MMX_COOP_ALIVE;
      p->zero.active_x=seat==0;p->body[0]=1;p->body[1]=2;p->body[14]=1;
      p->body[17]=0x20;p->body[5]=40;p->body[8]=seat?56:48;
    }
    memcpy(ram+0xba8,c.players[current].body,sizeof(c.players[current].body));
    MmxZeroSetState(c.players[current].zero);
    put_word(0,40);put_word(2,48);put_word(0x18,0x9100);ram[0x1a]=0x8d;ram[0xf]=0x20;
    MmxRendererObserveObject(ram,0xba8);MmxRendererRecordPiece(ram,0);MmxRendererLatchSprites();
    ppu.oam[32]=0x3028;ppu.oam[33]=0x2000;
    capture();MmxRendererCoopFrame(&c);
    MmxRenderView v=wide?MmxRendererViewport(MMX_ASPECT_ADAPTIVE,3840,1080,kSnesDisplayAspect_Crt4x3):
        (MmxRenderView){256,0,4.0/3.0};
    assert(MmxRendererDraw(output,v,false));
    /* Blue X covers red Zero; transparent X pixels expose Zero's Saber body.
     * Both projected seats and the original Zero sheet must agree. */
    for(unsigned x=40;x<44;++x)assert(output[48*v.width+v.extra+x]==0x0000ff);
    if(output[48*v.width+v.extra+44]!=(overlay?0xff0000u:0))fprintf(stderr,"overlap current=%u repairs=%u overlay=%u wide=%u pixel=%06x\n",current,repairs,overlay,wide,output[48*v.width+v.extra+44]);
    assert(output[48*v.width+v.extra+44]==(overlay?0xff0000u:0));
    assert(output[48*v.width+v.extra+38]==(overlay?0xff0000u:0));
    assert(!output[47*v.width+v.extra+40]);
    if(!current && repairs && overlay && wide) {
      /* A world sprite already ahead of X stays ahead of him. The partner
       * keeps its old relation to that sprite outside X's opaque footprint. */
      ppu.cgram[145]=31<<5;
      ppu.oam[32]=0x3029;ppu.oam[33]=0x2200;
      ppu.oam[34]=0x3028;ppu.oam[35]=0x2000;
      put_word(0,41);ram[0xf]=0x22;
      MmxRendererObserveObject(ram,0xe68);MmxRendererRecordPiece(ram,0);
      put_word(0,40);ram[0xf]=0x20;
      MmxRendererObserveObject(ram,0xba8);MmxRendererRecordPiece(ram,0);MmxRendererLatchSprites();
      capture();MmxRendererCoopFrame(&c);assert(MmxRendererDraw(output,v,false));
      assert(output[48*v.width+v.extra+40]==0x0000ff);
      assert(output[48*v.width+v.extra+41]==0x00ff00);
      assert(output[48*v.width+v.extra+44]==0xff0000);
    }
  }
  MmxRendererSetPlayerOverlayProvider(NULL);MmxCoopViewsSetOnline(false);
  g_mmx_custom_renderer=false;g_mmx_render_asset_repairs=true;MmxRendererReset();
}
