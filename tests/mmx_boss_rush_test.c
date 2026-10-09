#include "mmx_boss_rush.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void spawn(unsigned slot) {
  int id=MmxBossRushNextBoss();assert(id>=0 && id<8);
  assert(MmxBossRushAssign(slot,(unsigned)id,(uint16_t)(0xe68+slot*64)));
}
static void retire(unsigned slot) {
  MmxBossRushState s=MmxBossRushGetState();s.mode=MMX_RUSH_PLAYING;
  s.bosses[slot].phase=MMX_RUSH_FIGHTING;assert(MmxBossRushSetState(&s));
  assert(MmxBossRushDefeat(slot));assert(!MmxBossRushDefeat(slot));
  s=MmxBossRushGetState();memset(s.bosses+slot,0,sizeof(s.bosses[slot]));
  assert(MmxBossRushSetState(&s));
}
int main(void) {
  MmxBossRushReset();assert(!MmxBossRushActive());assert(MmxBossRushCoop());
  MmxBossRushStart(false,123);assert(!MmxBossRushCoop());
  unsigned seen=0;
  for(unsigned i=0;i<8;++i) {int id=MmxBossRushNextBoss();assert(id>=0 && !(seen&(1u<<id)));seen|=1u<<id;}
  assert(seen==255);
  MmxBossRushStart(true,123);spawn(0);spawn(1);
  MmxBossRushState saved=MmxBossRushGetState();
  assert(!MmxBossRushAssign(1,saved.bosses[0].id,0xea8));
  unsigned held=saved.bosses[0].id,counts[8]={0};
  for(unsigned i=0;i<10000;++i) {
    retire(1);spawn(1);MmxBossRushState s=MmxBossRushGetState();
    assert(s.bosses[0].id==held && s.bosses[1].id!=held);
    assert(MmxBossRushValidState(&s));++counts[s.bosses[1].id];
  }
  for(unsigned i=0;i<8;++i) if(i!=held) assert(counts[i]>1000);
  assert(MmxBossRushGetState().defeated==10000);
  assert(MmxBossRushSetState(&saved));retire(0);spawn(0);
  MmxBossRushState expected=MmxBossRushGetState();
  assert(MmxBossRushSetState(&saved));retire(0);spawn(0);
  MmxBossRushState actual=MmxBossRushGetState();assert(!memcmp(&actual,&expected,sizeof(actual)));
  MmxBossRushState bad=saved;bad.queued=9;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.bosses[1].id=bad.bosses[0].id;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.owner[0]=3;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.queue[1]=bad.queue[0];bad.queued=2;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.random=0;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.actor=1;bad.actor_object=0x1228;assert(!MmxBossRushSetState(&bad));
  static uint32_t image[256*224];
  saved.mode=MMX_RUSH_FINISHED;MmxBossRushDraw(image,256,0,&saved);
  unsigned pixels=0;for(unsigned i=0;i<256*224;++i) pixels+=image[i]!=0;assert(pixels>100);
  MmxBossRushFinish();assert(MmxBossRushNextBoss()==-1);
  puts("Boss Rush queue, persistence, duplicate exclusion, survivor starvation and validation passed");
  return 0;
}
