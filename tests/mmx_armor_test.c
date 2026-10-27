#include "mmx_armor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool zero;
bool MmxZeroActive(void) {return zero;}
static uint8_t ram[0x20000],pixels[128*128];
static uint16_t palette[128];
static void word(FILE *f,unsigned v) {fputc(v&255,f);fputc(v>>8,f);}
static void asset(const char *path,unsigned game) {
  FILE *f=fopen(path,"wb");assert(f);
  fwrite("MMXARMR1",8,1,f);word(f,game);word(f,game==2?112:100);word(f,5);word(f,game==2?133:136);
  unsigned bytes=game==2?0x41e:0x584,start=game==2?266:544;
  word(f,bytes);word(f,start);
  for(unsigned p=0;p<2;++p)for(unsigned c=0;c<16;++c)word(f,c+p*32);
  uint8_t anim[0x584]={0};
  for(unsigned i=0;i<start;i+=2) {anim[i]=(uint8_t)start;anim[i+1]=(uint8_t)(start>>8);}
  anim[start]=2;anim[start+1]=128;anim[start+3]=253;anim[start+4]=255;
  fwrite(anim,bytes,1,f);
  for(unsigned i=0;i<(game==2?112u:100u);++i)for(unsigned j=0;j<5;++j) {
    word(f,64+j);word(f,64);word(f,1);word(f,1);fputc(17+j,f);
  }
  assert(!fclose(f));
}
static void player(void) {
  memset(ram,0,sizeof(ram));ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;ram[0xba9]=2;
  ram[0xbcf]=0x90;ram[0x1f9a]=16;ram[0xbb6]=1;ram[0xbd3]=4;zero=false;
  MmxArmorResetState();
}
int main(int argc,char **argv) {
  if(argc==3) {
    unsigned game=(unsigned)atoi(argv[2]);assert(MmxArmorLoad(argv[1],game));player();
    for(unsigned parts=0;parts<16;++parts) {
      ram[0x1f99]=(uint8_t)parts;
      for(unsigned sequence=0;sequence<81;++sequence) {
        MmxArmorAnimationStart(0xba8,sequence);
        assert(MmxArmorGetState().anim_valid);
        for(unsigned tick=0;tick<40;++tick) {
          assert(MmxArmorCompose(ram,NULL,true,false,pixels,palette));
          MmxArmorAnimationAdvance(0xba8);
          MmxArmorState s=MmxArmorGetState();assert(MmxArmorValidState(&s));
        }
      }
    }
    MmxArmorDisable();puts("Original-ROM armor combinations and animation aliases passed");return 0;
  }
  const char *path="armor-fixture.bin";
  assert(!MmxArmorLoad("missing-armor-fixture",2));
  for(unsigned game=2;game<=3;++game) {
    asset(path,game);assert(MmxArmorLoad(path,game));player();
    assert(MmxArmorUpgradeBits(0x81971c,0)==8 && !ram[0x1f99]);
    assert(MmxArmorUpgradeBits(0x819ea5,0)==0);
    for(unsigned parts=0;parts<16;++parts) {
      ram[0x1f99]=(uint8_t)parts;
      assert(MmxArmorCompose(ram,NULL,true,false,pixels,palette));
      assert(pixels[64*128+64]==((parts&1)?0:17));
      assert(pixels[64*128+65]==((parts&1)?18:0));
      for(unsigned j=0;j<3;++j)assert(pixels[64*128+66+j]==((parts&(2u<<j))?19+j:0));
      assert(!MmxArmorGetState().gold);
    }
    assert(!MmxArmorCompose(ram,NULL,false,false,pixels,palette));
    zero=true;assert(MmxArmorUpgradeBits(0x81971c,0)==0);zero=false;
    player();ram[0xbd3]=0;ram[0xbaa]=8;ram[0xbe2]=ram[0xbde]=128;
    MmxArmorPrePlayer(ram);assert(!MmxArmorGetState().dash_ticks && !ram[0x1f99]);
    ram[0x1f99]=8;MmxArmorPrePlayer(ram);assert(MmxArmorGetState().dash_ticks==17);
    MmxArmorState saved=MmxArmorGetState();MmxArmorResetState();MmxArmorSetState(saved);
    MmxArmorState restored=MmxArmorGetState();assert(!memcmp(&saved,&restored,sizeof(saved)));
    assert(MmxArmorGetState().dash_ticks==17);
    saved.dash_used=3;assert(!MmxArmorValidState(&saved));MmxArmorSetState(saved);
    assert(!MmxArmorGetState().dash_ticks);
    player();ram[0x1f99]=15;
    ram[0x1f7a]=2;MmxArmorHadoukenAward(ram);assert(!MmxArmorGetState().gold);
    ram[0x1f7a]=3;MmxArmorHadoukenAward(ram);assert(MmxArmorGetState().gold==(game==3));
    assert(MmxArmorCompose(ram,NULL,true,false,pixels,palette));
    assert(palette[17]==(game==3?33:1));
    player();ram[0x1f99]=4;
    for(unsigned raw=1;raw<=12;++raw) {
      MmxArmorResetState();ram[0xe68+0x26]=(uint8_t)raw;ram[0]=(uint8_t)((raw+1)/2);
      MmxArmorTakeDamage(ram,0xe68);
      unsigned expected=game==2?(raw>1?raw-1:1):(raw/2?raw/2:1);
      if(game==2 && expected>=5)--expected;
      assert(ram[0]==expected);
    }
    if(game==3) {
      player();ram[0x1f99]=4;ram[0xe8e]=8;MmxArmorTakeDamage(ram,0xe68);
      assert(ram[0]==4 && MmxArmorGetState().barrier_ticks==300);
      MmxArmorState shield=MmxArmorGetState();shield.valid=1;shield.stage=0;shield.barrier_delay=0;
      MmxArmorSetState(shield);MmxArmorTakeDamage(ram,0xe68);
      assert(ram[0]==3 && !MmxArmorGetState().barrier_ticks);
      ram[0x1f7a]=3;MmxArmorHadoukenAward(ram);MmxArmorTakeDamage(ram,0xe68);
      shield=MmxArmorGetState();shield.barrier_delay=0;MmxArmorSetState(shield);
      MmxArmorTakeDamage(ram,0xe68);assert(ram[0]==2 && MmxArmorGetState().barrier_ticks);
    }
    MmxArmorDisable();assert(!MmxArmorEnabled() && MmxArmorUpgradeBits(0x81971c,0)==0);
  }
  assert(!remove(path));puts("X armor ownership, donor mitigation, Gold reward and state checks passed");
  return 0;
}
