#include "mmx_dash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t r[0x20000];
static void fresh(void) {
  memset(r,0,sizeof(r));MmxDashReset();
  r[0xd1]=2;r[0xd2]=r[0xd3]=4;r[0xba9]=2;r[0xbcf]=16;
  r[0x1f7a]=1;r[0xbaa]=6;r[0xc11]=64;
}
static void trigger(void) {r[0xbde]=r[0xbe2]=128;MmxDashPrePlayer(r,true,false);}
static void cache(const char *path) {
  FILE *f=fopen(path,"wb");assert(f);fwrite("MMXDSH01\6\0\0\0",1,12,f);
  uint8_t tiles[8192]={0},colors[32]={0};
  for(unsigned i=0;i<6;++i) {
    tiles[0]=(uint8_t)(i+1);fwrite(tiles,1,8192,f);fwrite(colors,1,32,f);
    uint8_t layout[]={1,32,0xf2,0,4};fwrite(layout,1,sizeof(layout),f);
  }
  assert(!fclose(f));
}
int main(void) {
  fresh();assert(MmxDashUpgradeBits(0,true)==0);MmxDashEnable(MMX_DASH_EARLY);
  assert(MmxDashUpgradeBits(0,true)==8 && MmxDashUpgradeBits(0,false)==0 && !r[0x1f99]);
  trigger();assert(!MmxDashGetState().ticks); /* Early Dash does not grant Air Dash */
  MmxDashEnable(MMX_DASH_AIR);trigger();assert(!MmxDashGetState().ticks); /* leg prerequisite */
  r[0x1f99]=8;trigger();assert(MmxDashGetState().ticks==17 && MmxDashGetState().used);
  MmxDashState start=MmxDashGetState();
  MmxDashPlayerMotion(r,0xba8,true,false);
  assert((r[0xbc2]|r[0xbc3]<<8)==0x375 && !(r[0xbc4]|r[0xbc5]) && r[0xbaa]==0x14);
  for(unsigned i=0;i<17;++i) {
    MmxDashPlayerMotion(r,0xba8,true,false);MmxDashPlayerEnd(r,true,false);
    if(i<16)MmxDashPrePlayer(r,true,false);
  }
  assert(!MmxDashGetState().ticks && MmxDashGetState().recovery==8 && r[0xbaa]==8 && !(r[0xc06]&4));
  MmxDashState end=MmxDashGetState();MmxDashSetState(&start);
  r[0xbaa]=0x14;r[0xbde]=128;
  for(unsigned i=0;i<17;++i) {
    MmxDashPlayerMotion(r,0xba8,true,false);MmxDashPlayerEnd(r,true,false);
    if(i<16)MmxDashPrePlayer(r,true,false);
  }
  MmxDashState replay=MmxDashGetState();assert(!memcmp(&end,&replay,sizeof(end)));
  trigger();assert(!MmxDashGetState().ticks); /* no second dash */
  r[0xbd3]=4;MmxDashPrePlayer(r,true,false);assert(!MmxDashGetState().used);
  r[0xbd3]=0;r[0xbaa]=6;trigger();assert(MmxDashGetState().ticks==17);
  MmxDashState phase=MmxDashGetState();MmxDashPrePlayer(r,false,false);MmxDashPlayerEnd(r,false,false);
  replay=MmxDashGetState();assert(!memcmp(&phase,&replay,sizeof(phase))); /* other seat */
  r[0xbaa]=12;MmxDashPrePlayer(r,true,false);
  assert(!MmxDashGetState().ticks && r[0xbaa]==12 && !(r[0xc06]&4)); /* native death retained */
  r[0xbaa]=0x10;MmxDashPrePlayer(r,true,false);assert(!MmxDashGetState().used);
  r[0xbaa]=6;trigger();assert(MmxDashGetState().ticks);
  MmxDashPrePlayer(r,true,true);assert(!MmxDashGetState().valid); /* full armor owns movement */
  MmxDashState bad={0};bad.ticks=18;assert(!MmxDashValidState(&bad));MmxDashSetState(&bad);
  assert(!MmxDashGetState().ticks);
  uint8_t native[]={1,0xf2,0,4,32},donor[]={32,0xf2,0,4};
  assert(!MmxDashPieceMissing(native,donor));donor[1]++;assert(MmxDashPieceMissing(native,donor));
  donor[1]--;donor[0]^=64;assert(MmxDashPieceMissing(native,donor));
  cache("dash-test.bin");assert(MmxDashArtLoad("dash-test.bin"));
  uint16_t palette[16];for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(i+100);
  const uint8_t *layout=NULL;const MmxSpriteAsset *a=MmxDashArt(54,&layout,palette);
  assert(a && a->tiles[0]==1 && a->colors[13]==palette[1] && layout[0]==1);
  assert(!MmxDashArt(53,NULL,NULL) && MmxDashArt(59,NULL,NULL)->tiles[0]==6);
  assert(!MmxDashArtLoad("missing-dash-test.bin") && !MmxDashArt(54,NULL,NULL));
  remove("dash-test.bin");
  puts("mmx_dash: movement, current-state and missing-piece controls PASS");
  return 0;
}
