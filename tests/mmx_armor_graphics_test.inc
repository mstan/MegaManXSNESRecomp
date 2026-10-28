static void armor_graphics_word(FILE *f,unsigned v) {
  fputc(v&255,f);fputc(v>>8,f);
}
static void armor_menu_and_hud(void) {
  const char *path="armor-render-fixture.bin";
  const uint16_t colors[]={0,31,0x3e0,0x7c00,0x7fff,0x3ff};
  const uint32_t rgb[]={0,0xff0000,0x00ff00,0x0000ff,0xffffff,0xffff00};
  for(unsigned game=2;game<=3;++game) {
    FILE *f=fopen(path,"wb");assert(f);
    unsigned frames=game==2?112:100,bytes=game==2?0x41e:0x584,start=game==2?266:544;
    fwrite("MMXARMR1",8,1,f);
    const unsigned header[]={game,frames,5,game==2?133:136,bytes,start};
    for(unsigned i=0;i<6;++i)armor_graphics_word(f,header[i]);
    for(unsigned p=0;p<2;++p)for(unsigned c=0;c<16;++c)
      armor_graphics_word(f,c<6?colors[c]:0);
    for(unsigned i=0;i<bytes;++i)fputc(0,f);
    for(unsigned pose=0;pose<frames;++pose)for(unsigned part=0;part<5;++part) {
      armor_graphics_word(f,64+part);armor_graphics_word(f,64);
      armor_graphics_word(f,1);armor_graphics_word(f,1);fputc(17+part,f);
    }
    assert(!fclose(f));assert(MmxArmorLoad(path,game));
    memset(&ppu,0,sizeof(ppu));memset(ram,0,sizeof(ram));
    memset(rom_bytes,0,sizeof(rom_bytes));memset(stock,0,sizeof(stock));
    ppu.inidisp=15;ppu.bgmode=1;ppu.screenEnabled[0]=16;
    for(unsigned i=0;i<128;++i)ppu.oam[i*2]=0xe000;
    /* A stock menu body with different colors, requiring the donor overlay.
     * No gameplay sprite list is available after a paused snapshot restore. */
    rom_long(0x68000,0x8d9000);rom_long(0x69000,0x8d9100);
    rom_bytes[0x69100]=1;rom_bytes[0x69104]=0x20;
    ppu.oam[32]=0xa080;ppu.oam[33]=0x2200;
    ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;ram[0xc3]=128;ram[0x1f10]=6;
    ram[0x1989]=1;ram[0x1996]=0x81;ram[0x1999]=0x22;
    put_word(0x198d,128);put_word(0x1990,160);
    MmxRendererSetRom(rom_bytes,sizeof(rom_bytes));
    for(unsigned parts=0;parts<16;++parts) {
      ram[0x1f99]=(uint8_t)parts;
      MmxRendererReset();capture();
      assert(MmxRendererDraw(output,(MmxRenderView){256,0,4.0/3.0},true));
      assert(MmxRendererGetStats().custom_lines==224);
      unsigned head=(parts&1)!=0;
      assert(output[160*256+128+head]==rgb[1+head]);
      for(unsigned part=0;part<3;++part)
        assert(output[160*256+130+part]==((parts&(2u<<part))?rgb[3+part]:0));
    }
    ram[0xc3]=ram[0x1f10]=ram[0x1989]=0;
    MmxArmorState armor=MmxArmorGetState();armor.energy=armor.hyper_energy=0x1c00;
    armor.gold=game==3;MmxArmorSetState(armor);capture();
    assert(MmxRendererDraw(output,(MmxRenderView){256,0,4.0/3.0},true));
    for(unsigned y=208;y<215;++y)for(unsigned x=8;x<56;++x)
      assert(output[y*256+x]==0); /* No invented Giga/Gold HUD. */
    MmxArmorDisable();MmxRendererReset();MmxRendererSetRom(NULL,0);
  }
  assert(!remove(path));
}
