#include "mmx_damage.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool actor_x=true;
static uint8_t ram[0x20000];
bool MmxCoopActorIsX(void) { return actor_x; }
bool MmxCoopEnabled(void) { return false; }
uint8_t *MmxCoopCharacterBody(uint8_t *r,unsigned actor) { (void)r;(void)actor;return NULL; }
static void fixture(void) {
  memset(ram,0,sizeof(ram));ram[0xe68]=ram[0xea8]=1;
  ram[0xe8f]=ram[0xecf]=ram[0xbcf]=ram[0x1f9a]=127;actor_x=true;MmxDamageClearState();
}
static unsigned dealt(unsigned damage) {
  unsigned result=MmxDamageDealt(ram,0xe68,damage);
  ram[0xe8f]=(uint8_t)(result>=ram[0xe8f]?0:ram[0xe8f]-result);return result;
}
static unsigned received(unsigned damage) {
  unsigned result=MmxDamageReceived(ram,damage,false);
  ram[0xbcf]=(uint8_t)(result>=ram[0xbcf]?0:ram[0xbcf]-result);return result;
}
int main(void) {
  assert(MmxDamageParseMultiplier("0.1")==10);
  assert(MmxDamageParseMultiplier("0.25")==25);
  assert(MmxDamageParseMultiplier("1.25")==125);
  assert(MmxDamageParseMultiplier("2")==200);
  assert(MmxDamageParseMultiplier("10")==1000);
  assert(MmxDamageParseMultiplier("0")==10);
  const char *invalid[]={NULL,"","-1","nan","inf","1e9","1x",".5","1.","0.001","99999999999999999999"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) assert(MmxDamageParseMultiplier(invalid[i])==100);
  MmxDamageReset();fixture();assert(!MmxDamageDealtEnabled());
  for(unsigned d=0;d<=127;++d) {
    assert(MmxDamageDealt(ram,0xe68,d)==d);assert(MmxDamageReceived(ram,d,false)==d);
  }
  MmxDamageSetDealt(300);assert(dealt(4)==12);
  actor_x=false;assert(dealt(4)==4);actor_x=true;
  assert(received(4)==4);MmxDamageSetReceived(10);
  for(unsigned i=0;i<4;++i) assert(received(2)==0);
  assert(received(2)==1); /* Five 2-HP hits now cost exactly 1 HP. */
  assert(MmxDamageReceived(ram,64,true)==64);
  assert(MmxDamageReceived(ram,127,true)==127);
  for(unsigned m=10;m<=1000;++m) for(unsigned d=0;d<=127;++d) {
    fixture();MmxDamageSetDealt(m);MmxDamageSetReceived(m);
    unsigned expected=d*m/100;if(expected>127)expected=127;
    assert(dealt(d)==expected && received(d)==expected);
  }
  const unsigned multipliers[]={10,25,50,75,125,150,175,200};
  for(unsigned i=0;i<sizeof(multipliers)/sizeof(*multipliers);++i) {
    fixture();unsigned m=multipliers[i],out=0,in=0;
    MmxDamageSetDealt(m);MmxDamageSetReceived(m);
    for(unsigned hit=1;hit<=20;++hit) {
      out+=dealt(1);in+=received(1);
      assert(out==hit*m/100 && in==hit*m/100);
    }
  }
  fixture();MmxDamageSetDealt(10);MmxDamageSetReceived(10);
  assert(dealt(2)==0);assert(MmxDamageDealt(ram,0xea8,2)==0);
  assert(received(2)==0);actor_x=false;assert(received(2)==0);actor_x=true;
  MmxDamageState saved=MmxDamageGetState();assert(MmxDamageValidState(&saved));
  assert(saved.enemies[0].fraction==20 && saved.enemies[1].fraction==20);
  assert(saved.received[0]==20 && saved.received[1]==20);
  dealt(2);received(2);MmxDamageRestoreWorld(&saved);
  MmxDamageState after=MmxDamageGetState();
  assert(!memcmp(after.enemies,saved.enemies,sizeof(after.enemies)) && after.received[0]==40);
  MmxDamageSetState(&saved);after=MmxDamageGetState();assert(!memcmp(&after,&saved,sizeof(saved)));
  ram[0xe68]=0;MmxDamageFrame(ram);ram[0xe68]=1;assert(dealt(2)==0);
  ram[0xbcf]=0;MmxDamageFrame(ram);ram[0xbcf]=127;assert(received(2)==0);
  saved.received[0]=100;assert(!MmxDamageValidState(&saved));MmxDamageSetState(&saved);
  after=MmxDamageGetState();assert(after.received[0]==0);
  fixture();MmxDamageSetDealt(UINT_MAX);assert(dealt(UINT_MAX)==127);
  MmxDamageSetReceived(0);assert(received(10)==1);
  MmxDamageReset();fixture();assert(dealt(4)==4 && received(4)==4);
  puts("MMX DAMAGE FRACTION AND BOUNDS CHECKS PASSED");return 0;
}
