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
static void visual_history(void) {
  uint16_t target[16];for(unsigned i=0;i<16;++i) target[i]=(uint16_t)(i|((31-i)<<5)|(15<<10));
  MmxBossRushReset();MmxBossRushVisualFade(1,123,target,0);
  for(unsigned i=0;i<8;++i) MmxBossRushVisualFade(1,123,target,1);
  MmxBossRushVisualState saved=MmxBossRushVisualGetState();
  assert(saved.colors[1][0]==(8<<5|8<<10));
  for(unsigned i=0;i<32;++i) MmxBossRushVisualFade(1,123,target,1);
  MmxBossRushVisualState complete=MmxBossRushVisualGetState();
  assert(!memcmp(complete.colors[1],target,sizeof(target)));
  for(unsigned i=0;i<32;++i) MmxBossRushVisualFade(1,123,target,-1);
  complete=MmxBossRushVisualGetState();
  for(unsigned i=0;i<16;++i) assert(!complete.colors[1][i]);
  assert(MmxBossRushVisualSetState(&saved));
  MmxBossRushVisualFade(1,123,target,1);
  complete=MmxBossRushVisualGetState();assert(complete.colors[1][0]==(9<<5|9<<10));
  assert(!complete.initialized[0]);
  MmxBossRushVisualState bad=saved;bad.colors[1][0]|=0x8000;assert(!MmxBossRushVisualSetState(&bad));
  MmxBossRushVisualFade(1,124,target,-1);
  complete=MmxBossRushVisualGetState();assert(complete.generation[1]==124 && complete.colors[1][0]==(30<<5|14<<10));
  MmxBossRushReset();complete=MmxBossRushVisualGetState();assert(!complete.initialized[1]);
}
static void roster(void) {
  /* X1 is always offered; other bits are trimmed to the 24-boss roster. */
  MmxBossRushSetOffered(0);assert(MmxBossRushOffered()==MMX_RUSH_X1_MASK);
  MmxBossRushSetOffered(0xff000000u|(1u<<23));
  assert(MmxBossRushOffered()==(MMX_RUSH_X1_MASK|(1u<<23)));
  MmxBossRushStart(false,77);
  MmxBossRushRosterState r=MmxBossRushRosterGetState();
  assert(r.available==(MMX_RUSH_X1_MASK|(1u<<23)) && !r.queued);
  /* Later launcher edits do not change a run in progress. */
  MmxBossRushSetOffered(MMX_RUSH_X1_MASK|MMX_RUSH_X2_MASK|MMX_RUSH_X3_MASK);
  uint32_t seen=0;
  for(unsigned i=0;i<9;++i) {int id=MmxBossRushNextBoss();assert(id>=0 && !(seen&(1u<<id)));seen|=1u<<id;}
  assert(seen==(MMX_RUSH_X1_MASK|(1u<<23)));
  /* All 24: every round covers the full roster without repeats. */
  MmxBossRushStart(true,99);seen=0;
  for(unsigned i=0;i<MMX_RUSH_ROSTER;++i) {
    int id=MmxBossRushNextBoss();assert(id>=0 && id<MMX_RUSH_ROSTER && !(seen&(1u<<id)));seen|=1u<<id;
  }
  assert(seen==(1u<<MMX_RUSH_ROSTER)-1);
  assert(MmxBossRushAssign(0,20,0xe68));
  MmxBossRushState s=MmxBossRushGetState();r=MmxBossRushRosterGetState();
  assert(MmxBossRushValidState(&s) && MmxBossRushRosterMatches(&s,&r));
  MmxBossRushRosterState x1=r;x1.available=MMX_RUSH_X1_MASK;x1.queued=0;memset(x1.queue,0,sizeof(x1.queue));
  assert(MmxBossRushRosterValidState(&x1) && !MmxBossRushRosterMatches(&s,&x1));
  /* Invalid rosters: missing X1, duplicates, ids outside the set, stale tail. */
  MmxBossRushRosterState bad=x1;bad.available=1u<<23;assert(!MmxBossRushRosterSetState(&bad));
  bad=x1;bad.available|=1u<<24;assert(!MmxBossRushRosterSetState(&bad));
  bad=x1;bad.queued=2;bad.queue[0]=bad.queue[1]=3;assert(!MmxBossRushRosterSetState(&bad));
  bad=x1;bad.queued=1;bad.queue[0]=9;assert(!MmxBossRushRosterSetState(&bad));
  bad=x1;bad.queue[5]=2;assert(!MmxBossRushRosterSetState(&bad));
  bad=x1;bad.reserved[1]=1;assert(!MmxBossRushRosterSetState(&bad));
  /* Pre-roster snapshots import their X1 queue unchanged. */
  MmxBossRushState legacy={0};legacy.mode=MMX_RUSH_PLAYING;legacy.random=5;
  legacy.queued=3;legacy.queue[0]=4;legacy.queue[1]=0;legacy.queue[2]=7;
  MmxBossRushRosterState imported=MmxBossRushRosterFromLegacy(&legacy);
  assert(imported.available==MMX_RUSH_X1_MASK && imported.queued==3 && imported.queue[2]==7);
  assert(MmxBossRushRosterValidState(&imported) && MmxBossRushRosterMatches(&legacy,&imported));
  legacy.mode=MMX_RUSH_OFF;imported=MmxBossRushRosterFromLegacy(&legacy);
  assert(!imported.available && !imported.queued);
  /* Setting a legacy queue directly orders the next draws through the roster. */
  MmxBossRushStart(false,5);legacy.mode=MMX_RUSH_PLAYING;
  assert(MmxBossRushSetState(&legacy));
  assert(!MmxBossRushGetState().queued && MmxBossRushRosterGetState().queued==3);
  assert(MmxBossRushNextBoss()==4 && MmxBossRushNextBoss()==0 && MmxBossRushNextBoss()==7);
  MmxBossRushSetOffered(MMX_RUSH_X1_MASK);
}
int main(void) {
  visual_history();
  roster();
  MmxBossRushReset();assert(!MmxBossRushActive());assert(MmxBossRushCoop());
  MmxBossRushStart(false,123);assert(!MmxBossRushCoop());
  unsigned seen=0;
  for(unsigned i=0;i<8;++i) {int id=MmxBossRushNextBoss();assert(id>=0 && !(seen&(1u<<id)));seen|=1u<<id;}
  assert(seen==255);
  MmxBossRushStart(true,123);spawn(0);spawn(1);
  MmxBossRushState saved=MmxBossRushGetState();
  MmxBossRushRosterState saved_roster=MmxBossRushRosterGetState();
  assert(!MmxBossRushAssign(1,saved.bosses[0].id,0xea8));
  unsigned held=saved.bosses[0].id,counts[8]={0};
  for(unsigned i=0;i<10000;++i) {
    retire(1);spawn(1);MmxBossRushState s=MmxBossRushGetState();
    assert(s.bosses[0].id==held && s.bosses[1].id!=held);
    assert(MmxBossRushValidState(&s));++counts[s.bosses[1].id];
  }
  for(unsigned i=0;i<8;++i) if(i!=held) assert(counts[i]>1000);
  assert(MmxBossRushGetState().defeated==10000);
  /* Snapshots own both the Rush state and its roster queue. */
  assert(MmxBossRushSetState(&saved) && MmxBossRushRosterSetState(&saved_roster));retire(0);spawn(0);
  MmxBossRushState expected=MmxBossRushGetState();
  MmxBossRushRosterState expected_roster=MmxBossRushRosterGetState();
  assert(MmxBossRushSetState(&saved) && MmxBossRushRosterSetState(&saved_roster));retire(0);spawn(0);
  MmxBossRushState actual=MmxBossRushGetState();assert(!memcmp(&actual,&expected,sizeof(actual)));
  MmxBossRushRosterState actual_roster=MmxBossRushRosterGetState();
  assert(!memcmp(&actual_roster,&expected_roster,sizeof(actual_roster)));
  MmxBossRushState bad=saved;bad.queued=9;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.bosses[1].id=bad.bosses[0].id;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.owner[0]=3;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.queue[1]=bad.queue[0];bad.queued=2;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.random=0;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.actor=1;bad.actor_object=0x1228;assert(!MmxBossRushSetState(&bad));
  bad=saved;bad.loaded=MMX_RUSH_MUSIC_PENDING;assert(!MmxBossRushSetState(&bad));
  bad.mode=MMX_RUSH_PREPARING;assert(MmxBossRushValidState(&bad));
  static uint32_t image[256*224];
  saved.mode=MMX_RUSH_FINISHED;MmxBossRushDraw(image,256,0,&saved);
  unsigned pixels=0;for(unsigned i=0;i<256*224;++i) pixels+=image[i]!=0;assert(pixels>100);
  MmxBossRushFinish();assert(MmxBossRushNextBoss()==-1);
  puts("Boss Rush 24-boss roster, queue, persistence, duplicate exclusion, survivor starvation and validation passed");
  return 0;
}
