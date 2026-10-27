/* Focused ROM regressions for the owner's October 3 playtest. */
#include "mmx_render_assets.h"
/* MMX_COOP_TEST=1 MMX_NATIVE_SPECIAL_ART_TEST=1, with the normal ground fixture:
 * compare both facing directions against independent PPU output over startup
 * and traveling phases, including native object overlap at the muzzle. */
static void native_special_art_checks(const char *fixture) {
  static uint8_t prior[0x20000];static uint32_t native[256*224],control[256*224],private_image[256*224];
  for(unsigned facing=0;facing<2;++facing)for(unsigned weapon=2;weapon<=5;weapon+=3) {
    check(RtlLoadSnapshot(fixture),"native special reference fixture loads");
    MmxCoopDisable();MmxZeroDisable();MmxWeaponsDisable();
    g_mmx_custom_renderer=true;g_mmx_expanded_sprites=false;g_mmx_render_asset_repairs=false;
    for(unsigned w=1;w<=8;++w)g_ram[0x1f86+w*2]=0xc0|28;
    for(unsigned w=0;w<weapon;++w) {frame(SNES_PAD_R);zero_replay(3);}
    check(g_ram[0xbdb]==weapon*2,"native reference selects requested X1 special");
    frame(facing?SNES_PAD_LEFT:SNES_PAD_RIGHT);
    unsigned samples=0,differences=0,matched_pieces=0,presentation_changes=0,reference_differences=0;
    for(unsigned tick=0;tick<32;++tick) {
      memcpy(prior,g_ram,sizeof(prior));frame(tick?0:SNES_PAD_Y);
      const uint32_t *stock=MmxRendererStockFrame();check(stock!=NULL,"native special PPU reference materializes");
      memcpy(native,stock,sizeof(native));
      check(MmxRendererDraw(control,(MmxRenderView){256,0,4.0/3.0},false),"original native arrangement renders");
      unsigned tick_changes=0;
      MmxCoopState c={0};c.initialized=1;c.players[0].status=MMX_COOP_ALIVE;
      memcpy(c.players[0].body,prior+0xba8,sizeof(c.players[0].body));
      memcpy(c.players[0].auxiliaries,prior+0xc38,sizeof(c.players[0].auxiliaries));
      memcpy(c.players[0].shots,prior+0x1228,sizeof(c.players[0].shots));
      MmxRendererCoopFrame(&c);
      check(MmxRendererDraw(private_image,(MmxRenderView){256,0,4.0/3.0},false),"private special reference renders");
      const uint8_t *rom=g_snes->cart->rom;
      for(unsigned d=0x1228;d<0x1428;d+=64) {
        const uint8_t *q=prior+d;if(!q[0] || !(q[14]&128))continue;
        unsigned address=rom[0x68000+q[22]*3]|rom[0x68001+q[22]*3]<<8|rom[0x68002+q[22]*3]<<16;
        unsigned offset=((address>>16)&127)*0x8000+(address&0x7fff)+(q[23]&127)*3;
        address=rom[offset]|rom[offset+1]<<8|rom[offset+2]<<16;
        const uint8_t *layout=rom+((address>>16)&127)*0x8000+(address&0x7fff);
        int ox=(int16_t)(eagle_word(q+5)-eagle_word(prior+0x1e4d));
        int oy=(int16_t)(eagle_word(q+8)+(int8_t)q[25]-eagle_word(prior+0x1e50));
        for(unsigned i=0;i<layout[0];++i) {
          const uint8_t *p=layout+1+i*4;unsigned size=p[3]&32?16:8;
          int x=ox+((q[17]&64)?-(int8_t)p[0]-(int)size:(int8_t)p[0]);
          int y=oy+(int8_t)p[1];
          unsigned attr=((((p[3]&0xce)|(q[17]&63))^(q[17]&64))<<8)|((p[2]+q[24])&255);
          bool found=false;
          for(unsigned slot=16;slot<128;++slot) {
            unsigned pos=g_ppu->oam[slot*2],high=g_ppu->highOam[slot/4]>>(slot%4*2);
            int sx=(pos&255)|((high&1)<<8);if(sx>=256)sx-=512;
            if(sx==x && (pos>>8)==((unsigned)y&255) && g_ppu->oam[slot*2+1]==attr)found=true;
          }
          if(!found)continue;++matched_pieces;
          for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
            int px=x+(int)col,py=y+(int)row;if(px<0 || px>=256 || py<0 || py>=224)continue;
            ++samples;differences+=native[py*256+px]!=private_image[py*256+px];
            tick_changes+=control[py*256+px]!=private_image[py*256+px];
            reference_differences+=control[py*256+px]!=native[py*256+px];
          }
        }
      }
      MmxRendererCoopFrame(NULL);
      presentation_changes+=tick_changes;
      if(tick_changes)fprintf(stderr,"SPECIAL_PHASE weapon=%u tick=%u changed=%u\n",weapon,tick,tick_changes);
    }
    fprintf(stderr,"NATIVE_SPECIAL facing=%u weapon=%u matched_pieces=%u samples=%u differences=%u control_differences=%u private_changes=%u\n",facing,weapon,matched_pieces,samples,differences,reference_differences,presentation_changes);
    check(samples>128,"native weapon comparison covers submitted projectile pixels");
    check(reference_differences==0,"native reference composition matches the PPU");
    check(differences==0,"private Storm Tornado and Chameleon Sting match native pixels");
  }
  g_mmx_render_asset_repairs=true;puts("MMX NATIVE SPECIAL ART CHECKS COMPLETE");
}
static void modern_image(const char *suffix);
static void coop_followup_fixture(const char *fixture);
static void coop_dash_effect_checks(const char *fixture,uint8_t *start,uint8_t *expected,
                                    uint8_t *actual,size_t cap) {
  bool custom=g_mmx_custom_renderer;g_mmx_custom_renderer=true;
  for(unsigned roster=0;roster<2;++roster) for(unsigned anchor=0;anchor<2;++anchor) {
    fprintf(stderr,"dash case roster=%u anchor=%u\n",roster,anchor);
    coop_followup_fixture(fixture);g_ram[0x1f99]|=8;
    MmxCoopState s=MmxCoopGetState();
    for(unsigned seat=0;seat<2;++seat) {
      s.players[seat].character=(uint8_t)(roster^seat);
      s.players[seat].zero.active_x=!(roster^seat);
    }
    MmxZeroSetState(s.players[s.current].zero);
    MmxCoopSetState(&s);MmxCoopSelect(g_ram,1);
    g_ram[0xc11]=0;g_ram[0xbce]=127;MmxCoopCapture(g_ram);
    MmxCoopSelect(g_ram,anchor);s=MmxCoopGetState();s.anchor=(uint8_t)anchor;MmxCoopSetState(&s);
    coop_frames(120,0,SNES_PAD_Y);
    MmxCoopSelect(g_ram,1);g_ram[0xc11]=0;MmxCoopCapture(g_ram);MmxCoopSelect(g_ram,anchor);
    unsigned effects=0;
    for(unsigned t=0;t<18;++t) {
      coop_frames(1,0,SNES_PAD_Y|SNES_PAD_A|SNES_PAD_LEFT);s=MmxCoopGetState();
      if(t==1) fprintf(stderr,"dash action=%02x ground=%02x held=%02x facing=%02x\n",s.players[1].body[2],s.players[1].body[0x5e],s.players[1].body[0x36],s.players[1].body[0x69]);
      check(s.current==anchor && !s.effect_return,"dash update restores its caller's player projection");
      for(unsigned d=0x1928;d<0x1d08;d+=32) if(g_ram[d] && g_ram[d+10]==0x0b) {
        check(g_ram[d+12]==2 && g_ram[d+13]==0xd5,"native dash effect records its creator in WRAM");
        unsigned x=eagle_word(g_ram+d+5);
        check(x<=coop_x(&s.players[1])+64 && x>=coop_x(&s.players[1]),"dash ignition stays at the creator's start position");
        const MmxSpriteAsset *art=MmxRenderAssetsObjectSprite(g_ram,d,0x60);
        check(art && !art->live_tiles && art->live_colors,"dash uses isolated original ROM art and native palette");
        ++effects;
      }
      if(t==8) {
        size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"mid-dash snapshot saves");
        coop_frames(4,0,SNES_PAD_Y|SNES_PAD_A|SNES_PAD_LEFT);
        size_t en=RtlSaveSnapshotToMemory(expected,cap);
        check(RtlLoadSnapshotFromMemory(start,n),"mid-dash snapshot restores");
        coop_frames(4,0,SNES_PAD_Y|SNES_PAD_A|SNES_PAD_LEFT);
        same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),"owned dash effects replay deterministically");
        if(!roster) {char suffix[48];snprintf(suffix,sizeof(suffix),"dash-fixed-anchor-%u",anchor);modern_image(suffix);}
      }
    }
    check(effects>=4,"partner dash effects remain visible while the other actor idles");
    coop_frames(45,0,0);
    for(unsigned d=0x1928;d<0x1d08;d+=32)
      check(!g_ram[d] || g_ram[d+10]!=0x0b,"native dash effects end with their creator's dash");
  }
  for(unsigned owner=0;owner<2;++owner) {
    coop_followup_fixture(fixture);MmxCoopSelect(g_ram,owner^1);
    MmxCoopState s=MmxCoopGetState();s.anchor=(uint8_t)(owner^1);
    s.players[owner].status=MMX_COOP_ABSENT;MmxCoopSetState(&s);
    uint8_t *effect=g_ram+0x1928;memset(effect,0,32);
    effect[0]=0x81;effect[10]=0x0b;effect[12]=(uint8_t)(owner+1);effect[13]=0xd5;
    coop_frames(2,0,0);s=MmxCoopGetState();
    check(!effect[0] && s.current==(owner^1) && !s.effect_return,
        "withdrawn player's dash retires without damaging the survivor's return stack");
  }
  /* Submit only the flame, then corrupt X's entire live body page. This
   * covers the screenshot's three-piece trail as well as the ignition sparks. */
  coop_followup_fixture(fixture);MmxCoopState isolated=MmxCoopGetState();
  g_mmx_custom_renderer=true;
  g_ppu->inidisp=15;g_ppu->screenEnabled[0]|=16;
  for(unsigned seat=0;seat<2;++seat) {
    memset(isolated.players[seat].body,0,sizeof(isolated.players[seat].body));
    memset(isolated.players[seat].auxiliaries,0,sizeof(isolated.players[seat].auxiliaries));
    isolated.players[seat].status=MMX_COOP_FALLEN;
  }
  memset(g_ram+0xba8,0,0x90);memset(g_ram+0xc38,0,0x1e0);
  memset(g_ram+0xe7,0,6);g_ram[0xe7]=1;eagle_putword(g_ram+0x920,0x1928);
  uint8_t *q=g_ram+0x1928;memset(q,0,32);q[0]=0x81;q[1]=1;q[10]=0x0b;q[14]=0x81;q[17]=0x22;q[22]=0x60;
  eagle_putword(q+5,eagle_word(g_ram+0x1e4d)+128);
  eagle_putword(q+8,eagle_word(g_ram+0x1e50)+128);
  uint16_t saved[1024];memcpy(saved,g_ppu->vram+0x6000,sizeof(saved));
  uint8_t dma=g_ram[0xa3];g_ram[0xa3]=0;
  for(unsigned slot=0;slot<128;++slot) g_ppu->oam[slot*2]=0xf000;
  bool expanded=g_mmx_expanded_sprites;g_mmx_expanded_sprites=true;
  const unsigned poses[]={0,1,4,5,6};
  static uint32_t original[256*224],poisoned[256*224],empty[256*224];
  for(unsigned i=0;i<sizeof(poses)/sizeof(poses[0]);++i) {
    q[23]=(uint8_t)poses[i];
    MmxRendererReset();MmxRendererObserveObject(g_ram,0x1928);MmxRendererLatchSprites();
    memcpy(g_ppu->vram+0x6000,saved,sizeof(saved));coop_x_graphics_draw(original,&isolated);
    memset(g_ppu->vram+0x6000,0xa5,sizeof(saved));coop_x_graphics_draw(poisoned,&isolated);
    check(!memcmp(original,poisoned,sizeof(original)),"dash pixels survive arbitrary X body uploads");
    if(getenv("MMX_ZERO_TEST_CAPTURE")) zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".dash-layout.cap");
    MmxRendererReset();g_ram[0xe7]=0;MmxRendererObserveObject(g_ram,0x1928);MmxRendererLatchSprites();
    coop_x_graphics_draw(empty,&isolated);g_ram[0xe7]=1;
    unsigned visible=0;for(unsigned p=0;p<256*224;++p) visible+=original[p]!=empty[p];
    check(visible>5,"dash CHR regression includes visible original effect pixels");
  }
  memcpy(g_ppu->vram+0x6000,saved,sizeof(saved));g_ram[0xa3]=dma;g_mmx_expanded_sprites=expanded;
  MmxCoopDisable();g_mmx_custom_renderer=custom;puts("MMX CO-OP DASH EFFECT CHECKS PASSED");
}
static void coop_followup_fixture(const char *fixture) {
  MmxCoopDisable();check(RtlLoadSnapshot(fixture),"follow-up fixture loads");
  MmxZeroSetModern(false);check(MmxCoopEnable(0),"follow-up co-op enables");
  coop_frames(8,0,0);MmxCoopState s=MmxCoopGetState();
  check(coop_place_fixture_partner(coop_x(&s.players[0])+32,coop_y(&s.players[0])),"follow-up partner placed");
  coop_frames(8,0,0);
}
static void coop_storm_landing_checks(const char *fixture) {
  MmxCoopSetDiagnosticsEnabled(true);
  const unsigned supports[]={0x0f,0x10,0x13,0x14};
  for(unsigned roster=0;roster<2;++roster) for(unsigned index=0;index<4;++index) {
    unsigned kind=supports[index],origin_offset=kind==0x13?24:kind==0x14?32:23;
    coop_followup_fixture(fixture);MmxCoopState s=MmxCoopGetState();
    for(unsigned seat=0;seat<2;++seat) {
      s.players[seat].character=roster^seat;s.players[seat].zero.active_x=!(roster^seat);
    }
    MmxCoopSetState(&s);
    unsigned x=coop_x(&s.players[0]),y=coop_y(&s.players[0])-60,d=0x16b8;
    memset(g_ram+0x1628,0,0x300);memset(g_ram+d,0,48);
    g_ram[d]=1;g_ram[d+1]=kind==0x10 ? 4 : 2;g_ram[d+2]=kind==0x14?0:2;g_ram[d+10]=(uint8_t)kind;
    eagle_putword(g_ram+d+5,x);eagle_putword(g_ram+d+8,y+origin_offset);
    eagle_putword(g_ram+d+0x22,x);eagle_putword(g_ram+d+0x24,y+origin_offset);
    eagle_putword(g_ram+d+0x29,y+origin_offset+64);eagle_putword(g_ram+d+0x1c,0x180);
    eagle_putword(g_ram+d+0x1e,x);g_ram[d+0x26]=64;
    g_ram[d+0x2c]=1;
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopSelect(g_ram,seat);
      eagle_putword(g_ram+0xbad,x-(seat?13:0));eagle_putword(g_ram+0xbb0,y-(seat?7:0));
      eagle_putword(g_ram+0xbca,x-(seat?13:0));eagle_putword(g_ram+0xbcc,y-(seat?9:0));
      eagle_putword(g_ram+0xbc4,seat?0xfd00:0);
      g_ram[0xbd3]=0;g_ram[0xbd4]=seat?0:4;g_ram[0xbaa]=seat?8:0;g_ram[0xbab]=2;
    }
    MmxCoopSelect(g_ram,0);
    unsigned landed=0;
    for(unsigned t=0;t<10;++t) {
      coop_frames(1,0,0);s=MmxCoopGetState();
      fprintf(stderr,"landing roster=%u kind=%02x t=%u platform=%u riders=%u p1=%u p2=%u vy=%d flags=%02x/%02x box=%02x%02x\n",roster,kind,t,eagle_word(g_ram+d+8),g_ram[d+0x2c],coop_y(&s.players[0]),coop_y(&s.players[1]),(int16_t)eagle_word(s.players[1].body+0x1c),s.players[1].body[0x2b],s.players[1].body[0x2c],s.players[1].body[0x21],s.players[1].body[0x20]);
      landed|=g_ram[d+0x2c]&2;
    }
    check(landed,"falling partner boards moving Storm support at native speed");
  }
  /* $81:E4C7 gates the capsule solely on shared upgrade bits, even when
   * the other player has died. Do not respawn armor already granted by a password. */
  for(unsigned owned=0;owned<2;++owned) {
    coop_followup_fixture(fixture);MmxCoopState s=MmxCoopGetState();
    s.players[1].status=MMX_COOP_FALLEN;s.players[1].body[0x27]=128;
    s.stage=5;s.stage_pending=0;
    MmxCoopSetState(&s);g_ram[0x1f7a]=5;
    g_ram[0x1f99]=(g_ram[0x1f99]&~1u)|owned;
    unsigned d=0x16b8;memset(g_ram+d,0,48);
    g_ram[d]=1;g_ram[d+10]=5;g_ram[d+11]=1;
    eagle_putword(g_ram+d+5,coop_x(&s.players[0])+80);
    eagle_putword(g_ram+d+8,coop_y(&s.players[0]));
    coop_frames(4,0,0);
    check(owned ? !g_ram[d] : g_ram[d] && g_ram[d+1]==2 && g_ram[d+22]==0x96,
        "native helmet capsule initializes with fallen Zero and hides only when owned");
  }
  MmxCoopSetDiagnosticsEnabled(false);MmxCoopDisable();puts("MMX STORM LANDING CHECKS PASSED");
}
static unsigned coop_buster_pixels_equal(const uint32_t *a,const uint32_t *b,const MmxCoopState *state) {
  const uint8_t *rom=g_snes->cart->rom;unsigned checked=0;
  for(unsigned seat=0;seat<2;++seat) for(unsigned slot=0;slot<8;++slot) {
    const uint8_t *q=state->players[seat].shots+slot*64;
    if(!q[0] || q[10]<1 || q[10]>3) continue;
    const MmxSpriteAsset *art=MmxRenderAssetsChargedBuster(q[22],q[23]&127);
    if(!art) continue;
    size_t directory=0x68000+q[22]*3;
    for(unsigned level=0;level<2;++level) {
      unsigned address=rom[directory]|rom[directory+1]<<8|rom[directory+2]<<16;
      directory=((address>>16)&127)*0x8000+(address&0x7fff);
      if(!level) directory+=(q[23]&127)*3;
    }
    int x=eagle_word(q+5)-eagle_word(g_ram+0x1e4d);
    int y=eagle_word(q+8)+(int8_t)q[25]-eagle_word(g_ram+0x1e50);
    const uint8_t *layout=rom+directory;
    for(unsigned part=0;part<layout[0];++part) {
      const uint8_t *p=layout+1+part*4;int size=p[3]&32?16:8;
      int left=x+((q[17]&64)?-(int8_t)p[0]-size:(int8_t)p[0]);
      int top=y+((q[17]&128)?-(int8_t)p[1]-size:(int8_t)p[1]);
      unsigned flip=p[3]^q[17];
      for(int row=0;row<size;++row) for(int col=0;col<size;++col) {
        int dx=left+col,dy=top+row;if(dx<0 || dx>=256 || dy<0 || dy>=224) continue;
        unsigned cx=flip&64?size-1-col:col,cy=flip&128?size-1-row:row;
        unsigned tile=((((p[2]>>4)+cy/8)&15)<<4)|(((p[2]&15)+cx/8)&15);
        const uint8_t *bits=art->tiles+tile*32+(cy&7)*2;unsigned shift=7-(cx&7);
        unsigned pixel=((bits[0]>>shift)&1)|(((bits[1]>>shift)&1)<<1)|
            (((bits[16]>>shift)&1)<<2)|(((bits[17]>>shift)&1)<<3);
        if(!pixel) continue;
        if(a[dy*256+dx]!=b[dy*256+dx]) {
          fprintf(stderr,"shared CHR affected seat %u shot %u pose %u pixel %d,%d\n",seat,slot,q[23]&127,dx,dy);
          check(false,"charged projectile pixels survive replacement of shared CHR");
        }
        ++checked;
      }
    }
  }
  return checked;
}
static void coop_graphics_followup_checks(const char *fixture) {
  const char *vile=getenv("MMX_COOP_VILE_FIXTURE");
  if(!vile) vile=fixture;
  coop_followup_fixture(vile);
  MmxCoopSelect(g_ram,1);
  g_ram[0x1f7a]=0;g_ram[0xbaa]=0x32;g_ram[0xbab]=2;
  MmxZeroAnimationStart(0xba8,0x49);
  coop_frames(4,0,0);
  check(MmxCoopGetState().players[1].body[2]==0x32,"Vile fixture holds native stunned action");
  modern_image("vile-stun");
  coop_followup_fixture(fixture);
  memset(g_ram+0x1228,0,512);memset(g_ram+0xc98,0,0x180);
  g_ram[0xc01]=g_ram[0xc03]=g_ram[0xc2a]=0;
  g_ram[0x1228]=1;g_ram[0x1232]=2;g_ram[0xbdd]=1;
  MmxCoopSelect(g_ram,1);
  MmxZeroState z=MmxZeroGetState();z.charge=201;MmxZeroSetState(z);
  MmxCoopSelect(g_ram,0);
  unsigned beams=0,checked=0,seats=0;
  for(unsigned t=0;t<28;++t) {
    coop_frames(1,0,0);
    MmxCoopState s=MmxCoopGetState();
    for(unsigned seat=0;seat<2;++seat) for(unsigned slot=0;slot<8;++slot) {
      const uint8_t *shot=s.players[seat].shots+slot*64;
      if(shot[0] && (shot[10]==2 || shot[10]==3) &&
          ((shot[23]&127)<8 || (shot[10]==2 && (shot[23]&127)>=12))) {
        ++beams;seats|=1u<<seat;
        const MmxSpriteAsset *art=MmxRenderAssetsChargedBuster(shot[22],shot[23]&127);
        check(art && !art->live_tiles,"each traveling blue buster has independent original art");
      }
    }
    if(t==10 || t==17) {
      static uint32_t original[256*224],poisoned[256*224];
      uint16_t saved[512];memcpy(saved,g_ppu->vram+0x6200,sizeof(saved));
      uint16_t saved_oam[256];memcpy(saved_oam,g_ppu->oam,sizeof(saved_oam));
      uint8_t dma=g_ram[0xa3];g_ram[0xa3]=0;
      /* Isolate each seat's traveling shots from the native charge arc and
       * other effects. Those legitimately still use these same CHR pages.
       * Render both original native variants through the partner compositor. */
      unsigned pixels=0;
      for(unsigned seat=0;seat<2;++seat) {
        MmxCoopState isolated=s;unsigned partner=s.current^1;
        memset(isolated.players[s.current].shots,0,sizeof(isolated.players[s.current].shots));
        memcpy(isolated.players[partner].shots,s.players[seat].shots,sizeof(isolated.players[partner].shots));
        memset(isolated.players[partner].body,0,sizeof(isolated.players[partner].body));
        memset(isolated.players[partner].auxiliaries,0,sizeof(isolated.players[partner].auxiliaries));
        for(unsigned i=0;i<128;++i) g_ppu->oam[i*2]=0xf000;
        MmxRendererReset();memcpy(g_ppu->vram+0x6200,saved,sizeof(saved));
        coop_x_graphics_draw(original,&isolated);
        memset(g_ppu->vram+0x6200,0,sizeof(saved));
        coop_x_graphics_draw(poisoned,&isolated);
        pixels+=coop_buster_pixels_equal(original,poisoned,&isolated);
      }
      check(pixels>200,"CHR regression covers opaque pixels across both charged shots");
      memcpy(g_ppu->vram+0x6200,saved,sizeof(saved));
      memcpy(g_ppu->oam,saved_oam,sizeof(saved_oam));g_ram[0xa3]=dma;++checked;
    }
    if(t==3 || t==11 || t==18) {
      char suffix[64];snprintf(suffix,sizeof(suffix),"dual-charge-%u",t);
      modern_image(suffix);zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),suffix);
    }
    if(t==3) {
      static uint32_t before[256*224],after[256*224];
      MmxCoopState isolated=s;
      memset(isolated.players[s.current^1].shots,0,sizeof(isolated.players[s.current^1].shots));
      coop_x_graphics_draw(before,&isolated);
      uint8_t poses[8];
      for(unsigned slot=0;slot<8;++slot) {
        unsigned d=0x1228+slot*64;poses[slot]=g_ram[d+23];
        if(g_ram[d] && (g_ram[d+22]==0x0e || g_ram[d+22]==0x9e)) g_ram[d+23]=12;
      }
      coop_x_graphics_draw(after,&isolated);
      same(before,sizeof(before),after,sizeof(after),"submitted native beam art retains its pose when the projectile advances");
      for(unsigned slot=0;slot<8;++slot) g_ram[0x1228+slot*64+23]=poses[slot];
    }
  }
  check(beams>=20 && checked==2 && seats==3,"dual shot test includes both traveling full-charge beams");
  coop_followup_fixture(fixture);
  MmxCoopSelect(g_ram,1);g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;
  MmxCoopCapture(g_ram);MmxCoopSelect(g_ram,0);
  unsigned circles=0;
  for(unsigned t=0;t<90;++t) {
    coop_frames(1,t<40?SNES_PAD_RIGHT:SNES_PAD_LEFT,0);
    if(t==35 || t==50) {
      for(unsigned d=0x1928;d<0x1d08;d+=32) if(g_ram[d] && g_ram[d+10]==14) {
        const MmxSpriteAsset *a=MmxRenderAssetsObjectSprite(g_ram,d,0x1d);
        check(a && !a->live_tiles,"death orbs use isolated original ROM graphics");
        ++circles;
      }
      char suffix[64];snprintf(suffix,sizeof(suffix),"zero-death-%u",t);
      modern_image(suffix);zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),suffix);
    }
  }
  check(circles>=8 && MmxCoopGetState().players[0].status==MMX_COOP_ALIVE,
      "Zero's native death circles survive while X continues playing");
  MmxCoopDisable();puts("MMX CO-OP GRAPHICS FOLLOWUP CAPTURES PASSED");
}
static void coop_followup_checks(const char *fixture) {
  coop_followup_fixture(fixture);
  coop_frames(60,0,SNES_PAD_Y);
  unsigned previous=MmxCoopGetState().players[1].zero.charge;bool landed=false;
  for(unsigned t=0;t<90;++t) {
    coop_frames(1,0,SNES_PAD_Y|(t<10?SNES_PAD_B:0));
    MmxCoopPlayer p=MmxCoopGetState().players[1];
    landed|=p.body[2]==0x0a;
    if(p.zero.charge<previous) fprintf(stderr,"charge dropped at action %02x: %u -> %u\n",p.body[2],previous,p.zero.charge);
    check(p.zero.charge>=previous,"held Zero buster charge survives every jump/landing frame");
    previous=p.zero.charge;
  }
  check(landed,"charge check includes native landing recovery");
  for(unsigned kind=0x0f;kind<=0x10;++kind) for(unsigned solo=0;solo<2;++solo) {
    coop_followup_fixture(fixture);MmxCoopState s=MmxCoopGetState();
    unsigned x=coop_x(&s.players[0])+16,y=coop_y(&s.players[0])-40,d=0x16b8;
    memset(g_ram+0x1628,0,0x300);memset(g_ram+d,0,48);
    g_ram[d]=1;g_ram[d+1]=2;g_ram[d+2]=2;g_ram[d+10]=(uint8_t)kind;
    eagle_putword(g_ram+d+5,x);eagle_putword(g_ram+d+8,y);
    eagle_putword(g_ram+d+0x22,x);eagle_putword(g_ram+d+0x24,y);
    eagle_putword(g_ram+d+0x29,y+64);eagle_putword(g_ram+d+0x1c,0x100);
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopSelect(g_ram,seat);eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbb0,y-26);
      eagle_putword(g_ram+0xbca,x);eagle_putword(g_ram+0xbcc,y-26);
      eagle_putword(g_ram+0xbc4,0);g_ram[0xbd3]=0;g_ram[0xbaa]=8;g_ram[0xbab]=2;
      if(solo && !seat) {
        eagle_putword(g_ram+0xbad,x-48);eagle_putword(g_ram+0xbb0,y+40);
        g_ram[0xbaa]=g_ram[0xbab]=0;g_ram[0xbd3]=4;
      }
    }
    MmxCoopSelect(g_ram,0);coop_frames(12,0,0);s=MmxCoopGetState();
    fprintf(stderr,"platform kind=%02x rider=%u y=%u players=%u/%u state=%u\n",kind,g_ram[d+0x2c],eagle_word(g_ram+d+8),coop_y(&s.players[0]),coop_y(&s.players[1]),g_ram[d+1]);
    check(g_ram[d+0x2c]==(solo?2:3) &&
        (solo ? coop_y(&s.players[1])+20<coop_y(&s.players[0]) : coop_y(&s.players[0])==coop_y(&s.players[1])),
        "Zero rides rising/flying platforms alone or alongside X");
    if(kind==0x10) check(g_ram[d+1]==4,"Zero boarding launches the original flying platform");
  }
  coop_followup_fixture(fixture);
  coop_weapon_select(0,1,8);
  g_ram[0x1f7a]=8;g_ram[0x1fa0]=0;g_ram[0xd3]=10;g_ram[0xd4]=0;
  unsigned last=~0u,seen=0;
  for(unsigned t=0;t<1500;++t) {
    coop_frames(1,0,0);
    unsigned signature=g_ram[0xd1]|g_ram[0xd2]<<8|g_ram[0xd3]<<16|g_ram[0xd4]<<24;
    if(signature!=last) {fprintf(stderr,"demo t=%u mode=%06x weapon=%u command=%u\n",t,signature,g_ram[0xbdb],g_ram[0xc01]);last=signature;}
    if(g_ram[0xd3]==10 && g_ram[0xd4]>=4) for(unsigned d=0x1228;d<0x1428;d+=64)
      if(g_ram[d] && g_ram[d+10]==14) {
        if(++seen==10) modern_image("weapon-get-ice");
      }
  }
  fprintf(stderr,"demo ice frames=%u\n",seen);
  check(seen,"Shotgun Ice acquisition demo creates native ice projectiles");
  MmxCoopDisable();puts("MMX CO-OP FOLLOW-UP CHECKS PASSED");
}
