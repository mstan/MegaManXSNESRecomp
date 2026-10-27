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
  memset(ram,0,sizeof(ram));ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;ram[0xba9]=ram[0xbab]=2;
  ram[0xbcf]=0x90;ram[0x1f9a]=16;ram[0xbb6]=1;ram[0xbd3]=4;zero=false;
  MmxArmorResetState();
}
static void tick_buster(bool held,bool pressed) {
  ram[0xbdf]=(uint8_t)(held?64:0);ram[0xbe3]=(uint8_t)(pressed?64:0);
  if(ram[0x1f0d])--ram[0x1f0d];
  MmxArmorPrePlayer(ram);MmxArmorPlayerEnd(ram);
  for(unsigned d=0x1228;d<0x1428;d+=64)MmxArmorProjectileTick(ram,d,ram[d]);
  MmxArmorState s=MmxArmorGetState();assert(MmxArmorValidState(&s));
}
static void buster(unsigned game) {
  player();ram[0x1f99]=2;ram[0xc11]=64;
  ram[0xbad]=ram[0x1e4d]=0x00;ram[0xbae]=ram[0x1e4e]=0x10;
  ram[0xbb0]=ram[0x1e50]=0x00;ram[0xbb1]=ram[0x1e51]=0x02;
  for(unsigned i=0;i<220;++i)tick_buster(true,i==0);
  assert(MmxArmorGetState().charge==201);
  bool first=false,second=false,cross=false;unsigned lobe_samples=0;
  /* Original X3 upper-charge trial, class $20/subtype 3, ticks 272..283. */
  static const int16_t lobe_vx[]={-1536,-1440,-1344,-1248,-1152,-1056,-960,-864,-768,-672,-576,-480};
  for(unsigned i=0;i<100;++i) {
    tick_buster(i==12 || i==13,i==12);
    MmxArmorState s=MmxArmorGetState();
    for(unsigned j=0;j<8;++j)if(s.shots[j].active) {
      first|=s.shots[j].kind==(game==2?1:3);
      second|=s.shots[j].kind==(game==2?2:1);
      cross|=s.shots[j].kind==5;
      const MmxArmorShot *p=s.shots+j;
      if(game==3 && p->cross_fragment && p->sub==3 && p->age && p->age<=12) {
        assert(p->vx==lobe_vx[p->age-1] && p->vy==192);++lobe_samples;
      }
    }
    if(i==10) {
      MmxArmorState saved=s;MmxArmorResetState();MmxArmorSetState(saved);
      MmxArmorState restored=MmxArmorGetState();assert(!memcmp(&saved,&restored,sizeof(saved)));
    }
  }
  assert(first && second && (game==2 || cross));
  assert(game==2 || lobe_samples==12);
  assert(!MmxArmorGetState().stored_shot);
  player();ram[0x1f99]=0;
  for(unsigned i=0;i<220;++i)tick_buster(true,i==0);
  tick_buster(false,false);assert(!MmxArmorGetState().burst && !MmxArmorGetState().stored_shot);
  puts("Donor charge, buffered follow-up, projectile state restore and arm ownership passed");
}
static void helmet(unsigned game) {
  player();ram[0x1f7a]=8;
  assert(!MmxArmorFrameTick(ram,0xc10,true) && !MmxArmorGetState().tracer);
  ram[0x1f99]=1;
  if(game==3) {
    assert(MmxArmorFrameTick(ram,0,true));assert(MmxArmorGetState().tracer);
    assert(MmxArmorFrameTick(ram,8,true));assert(!MmxArmorGetState().tracer);
    assert(MmxArmorFrameTick(ram,0,true));
    assert(!MmxArmorFrameTick(ram,0,true));
    assert(MmxArmorFrameTick(ram,0xc10,true));
    assert(MmxArmorFrameTick(ram,0xc10,true)); /* Holding does not toggle twice. */
    assert(MmxArmorFrameTick(ram,0,true));
    assert(MmxArmorFrameTick(ram,0xc10,true));assert(!MmxArmorGetState().tracer);
  } else {
    assert(!MmxArmorFrameTick(ram,0xc10,true));assert(MmxArmorGetState().tracer);
    for(unsigned i=0;i<180;++i)assert(!MmxArmorFrameTick(ram,0,true));
    assert(!MmxArmorGetState().tracer);
  }
  assert(!MmxArmorFrameTick(ram,0xc10,false));assert(!MmxArmorGetState().tracer);
  puts("Helmet ownership, radar pause/dismissal and tracer lifecycle passed");
}
static void charge_boundaries(void) {
  static const unsigned holds[]={20,21,80,81,140,141,200,201};
  for(unsigned j=0;j<sizeof(holds)/sizeof(holds[0]);++j) {
    unsigned held=holds[j];player();ram[0x1f99]=2;
    for(unsigned i=0;i<held;++i)tick_buster(true,i==0);
    tick_buster(false,false);MmxArmorState s=MmxArmorGetState();
    assert((s.burst!=0)==(held>=141));
    if(held>=141)assert(s.stored_shot==(held>=201?2:1));
    else assert(ram[0xc01]==(held>=81?8:held>=21?2:0));
  }
  for(unsigned kind=1;kind<=3;kind+=2) {
    player();ram[0xc11]=64;ram[0x1228]=1;ram[0x1232]=(uint8_t)kind;
    ram[0xbdd]=1;ram[0x1264]=38;
    MmxArmorProjectileTick(ram,0x1228,1);
    assert(MmxArmorProjectileOwned(ram,0x1228));
    MmxArmorState s=MmxArmorGetState();assert(s.shots[0].active && s.shots[0].kind==(kind==1?8:1));
    assert(MmxArmorValidState(&s));
  }
  puts("Original-ROM charge boundaries and native half/full projectile conversion passed");
}
static void setword(unsigned at,unsigned value) {ram[at]=(uint8_t)value;ram[at+1]=(uint8_t)(value>>8);}
static unsigned getword(unsigned at) {return ram[at]|(unsigned)ram[at+1]<<8;}
static uint8_t *partner_body(uint8_t *r) {return r+0x5000;}
static bool low_ceiling(const uint8_t *r,int x,int y) {(void)r;(void)x;return y<990;}
static void giga(void) {
  player();ram[0x1f99]=4;setword(0xbad,1024);setword(0xbb0,1024);
  MmxArmorPrePlayer(ram);MmxArmorState s=MmxArmorGetState();s.energy=0x1c00;MmxArmorSetState(s);
  assert(MmxArmorFrameTick(ram,0xc20,true));assert(MmxArmorGetState().giga_tick==1 && !MmxArmorGetState().energy);
  for(unsigned i=1;i<145;++i)assert(MmxArmorFrameTick(ram,0,true));
  assert(getword(0xbb0)==993 && MmxArmorGetState().giga_tick==145);
  assert(!MmxArmorFrameTick(ram,0,true));MmxArmorPrePlayer(ram);
  s=MmxArmorGetState();assert(s.giga_tick==146 && s.giga_fired && s.shots[0].kind==7);
  assert(!MmxArmorHitbox(ram,0xba8,0xe68,1));
  assert(MmxArmorDamage(ram,0xe68,0x1228,3)>0 && !MmxArmorDamage(ram,0xe68,0x1228,3));
  for(unsigned i=0;i<121;++i)MmxArmorProjectileTick(ram,0x1228,1);
  assert(!MmxArmorGetState().shots[0].active);
  while(MmxArmorGetState().giga_tick)MmxArmorFrameTick(ram,0,true);
  assert(getword(0xbc6)==0x40 && ram[0xbaa]==8);
  player();ram[0x1f99]=4;setword(0xbb0,1024);MmxArmorPrePlayer(ram);
  s=MmxArmorGetState();s.energy=0x1c00;MmxArmorSetState(s);MmxArmorSetTerrainQuery(low_ceiling);
  MmxArmorFrameTick(ram,0xc20,true);for(unsigned i=1;i<31;++i)MmxArmorFrameTick(ram,0,true);
  assert(getword(0xbb0)==1008);MmxArmorSetTerrainQuery(NULL);
  player();ram[0x1f99]=4;setword(0xbb0,1024);MmxArmorPrePlayer(ram);
  memcpy(ram+0x5000,ram+0xba8,0x90);uint8_t untouched[0x90];memcpy(untouched,ram+0xba8,sizeof(untouched));
  s=MmxArmorGetState();s.energy=0x1c00;MmxArmorSetState(s);MmxArmorSetBodyQuery(partner_body);
  MmxArmorFrameTick(ram,0xc20,true);for(unsigned i=1;i<31;++i)MmxArmorFrameTick(ram,0,true);
  assert(!memcmp(untouched,ram+0xba8,sizeof(untouched)) && getword(0x5008)==993);
  MmxArmorSetBodyQuery(NULL);
  puts("Giga timing, energy, native-contact hit mask, ceiling and stored-seat ownership passed");
}
static void gold(void) {
  player();ram[0x1f99]=15;ram[0x1f7a]=3;MmxArmorPrePlayer(ram);MmxArmorHadoukenAward(ram);
  assert(MmxArmorGetState().hyper_energy==0x1c00);
  MmxArmorFrameTick(ram,0,true);MmxArmorFrameTick(ram,8,true);
  MmxArmorFrameTick(ram,0,true);MmxArmorFrameTick(ram,0,true);
  MmxArmorFrameTick(ram,0xc20,true);assert(MmxArmorGetState().hyper);
  tick_buster(true,true);assert(MmxArmorGetState().burst==1 && MmxArmorGetState().hyper_energy==0x1a00);
  MmxArmorRefill();assert(MmxArmorGetState().hyper_energy==0x1c00);
  player();ram[0x1f99]=1;ram[0x1f7a]=3;MmxArmorPrePlayer(ram);MmxArmorHadoukenAward(ram);
  ram[0x1f9a]=32;
  for(unsigned i=0;i<307;++i)MmxArmorPrePlayer(ram);
  assert((ram[0xbcf]&127)==16);MmxArmorPrePlayer(ram);assert((ram[0xbcf]&127)==17);
  for(unsigned i=0;i<179;++i)MmxArmorPrePlayer(ram);
  assert((ram[0xbcf]&127)==17);MmxArmorPrePlayer(ram);assert((ram[0xbcf]&127)==18);
  ram[0xbcf]=0xa0;ram[0x1f83]=0x80;
  for(unsigned i=0;i<180;++i)MmxArmorPrePlayer(ram);
  assert(ram[0x1f83]==0x81);
  player();ram[0x1f99]=15;ram[0x1f7a]=3;MmxArmorPrePlayer(ram);MmxArmorHadoukenAward(ram);
  MmxArmorState retry=MmxArmorGetState();retry.hyper=1;retry.hyper_energy=12;retry.dash_used=2;
  MmxArmorSetState(retry);ram[0xd2]=2;assert(!MmxArmorFrameTick(ram,0,true));
  ram[0xd2]=4;MmxArmorPrePlayer(ram);retry=MmxArmorGetState();
  assert(retry.gold && retry.hyper_energy==0x1c00 && !retry.hyper && !retry.dash_used);
  assert(MmxArmorFrameTick(ram,0,true) && MmxArmorGetState().tracer);
  puts("Gold Hyper Charge cost/refill and source-timed health/Sub Tank recovery passed");
}
int main(int argc,char **argv) {
  if(argc>=3) {
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
    if(game==3) {
      MmxArmorGraphic graphic;
      assert(MmxArmorGraphicGet(0x33,0x16,&graphic) && graphic.w && graphic.h);
      player();ram[0x1f99]=8;ram[0xbd3]=0;ram[0xbaa]=8;
      ram[0xbde]=ram[0xbe2]=128;ram[0xbdf]=8;
      /* Original unobstructed trial: 21 slow + 12 thrust movement ticks. */
      for(unsigned tick=0;tick<33;++tick) {
        MmxArmorPrePlayer(ram);assert(MmxArmorGetState().dash_up && MmxArmorGetState().dash_ticks);
        assert((ram[0xbc4]|ram[0xbc5]<<8)==(tick<21?0x78:0x500));
        MmxArmorAnimationAdvance(0xba8);MmxArmorPlayerEnd(ram);ram[0xbe2]=0;
      }
      assert(MmxArmorGetState().dash_recovery==9 && !MmxArmorGetState().dash_ticks);
      for(unsigned tick=0;tick<9;++tick) {
        MmxArmorPrePlayer(ram);MmxArmorAnimationAdvance(0xba8);MmxArmorPlayerEnd(ram);
      }
      assert(!MmxArmorGetState().dash_recovery && !MmxArmorGetState().anim_group);
    }
    buster(game);
    helmet(game);
    charge_boundaries();
    if(game==2)giga();else gold();
    if(argc==4) {
      FILE *f=fopen(argv[3],"rb");assert(f);assert(!fseek(f,0,SEEK_END));
      size_t n=(size_t)ftell(f);rewind(f);uint8_t *rom=malloc(n);assert(rom);
      assert(fread(rom,1,n,f)==n);fclose(f);size_t header=n%32768==512?512:0;
      MmxArmorSetRom(rom+header,n-header);
      uint8_t *normalized=rom+header;
      /* A co-op Saber partner owns these three independent collision spans. */
      memset(normalized+0x37f40,0x35,40);memset(normalized+0x37fa0,0x56,16);memset(normalized+0x37fd8,0x78,40);
      MmxArmorCollisionRom(normalized,n-header);
      for(unsigned k=0;k<40;++k)assert(normalized[0x37f40+k]==0x35 && normalized[0x37fd8+k]==0x78);
      for(unsigned k=0;k<16;++k)assert(normalized[0x37fa0+k]==0x56);
      assert(normalized[0x37f00]!=255);MmxArmorCollisionRom(NULL,0);free(rom);
      unsigned total[5]={0};ram[0x1f99]=ram[0x1f9c]=ram[0x1f7e]=0;
      for(unsigned stage=0;stage<13;++stage) {
        ram[0x1f7a]=(uint8_t)stage;MmxArmorTarget t[8];unsigned count=MmxArmorTargets(ram,t,8);
        for(unsigned i=0;i<count;++i) {assert(t[i].kind>=1 && t[i].kind<=4);++total[t[i].kind];}
      }
      assert(total[1]==8 && total[2]==4 && total[3]==4 && total[4]==1);
      ram[0x1f99]=ram[0x1f9c]=0xff;ram[0x1f7e]=128;
      for(unsigned stage=0;stage<13;++stage) {
        ram[0x1f7a]=(uint8_t)stage;MmxArmorTarget t[8];assert(!MmxArmorTargets(ram,t,8));
      }
    }
    MmxArmorDisable();puts("Original-ROM armor combinations, aliases and collectible ownership passed");return 0;
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
