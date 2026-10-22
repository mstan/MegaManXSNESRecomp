#include "mmx_vs.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b) {
    MmxVsStart();assert(!MmxVsChoose(1,b));assert(!MmxVsChoose(0,8));
    assert(MmxVsChoose(0,a));assert(!MmxVsChoose(0,b));assert(MmxVsChoose(1,b));
    MmxVsState s=MmxVsGetState();assert(s.fighters[0].boss==a && s.fighters[1].boss==b);
    assert(s.fighters[0].hp==32 && s.fighters[1].hp==32 && MmxVsValidState(&s));
    s.phase=MMX_VS_FIGHT;assert(MmxVsSetState(&s));MmxVsResolveRound(0,1);
    s=MmxVsGetState();assert(s.score[1]==1 && s.first==1 && s.winner==1);
    MmxVsResolveRound(0,1);assert(MmxVsGetState().score[1]==1);
    MmxVsNextRound();assert(MmxVsGetState().picker==1);assert(MmxVsChoose(1,b));assert(MmxVsChoose(0,a));
    s=MmxVsGetState();s.phase=MMX_VS_FIGHT;assert(MmxVsSetState(&s));
    MmxVsResolveRound(0,0);assert(MmxVsGetState().winner==2 && MmxVsGetState().score[1]==1);
    MmxVsNextRound();assert(MmxVsGetState().picker==1);
  }
  MmxVsStart();assert(MmxVsChoose(0,4));MmxVsState s=MmxVsGetState();
  assert(!MmxVsPickVisible(&s,1,0,true));assert(MmxVsPickVisible(&s,0,0,true));
  assert(MmxVsPickVisible(&s,1,0,false));assert(MmxVsChoose(1,4));s=MmxVsGetState();
  assert(MmxVsPickVisible(&s,1,0,true));
  assert(MmxVsAttackClass(4,MMX_VS_PRIMARY,true)==13);
  assert(MmxVsAttackClass(4,MMX_VS_SPECIAL,false)==13);
  assert(MmxVsAttackClass(4,MMX_VS_TELEPORT,false)==1);
  assert(MmxVsAttackClass(0,MMX_VS_SPECIAL,false)==1);
  assert(MmxVsAttackClass(2,MMX_VS_SPECIAL,false)==8);
  assert(MmxVsAttackClass(2,MMX_VS_PRIMARY,true)==1);
  assert(MmxVsAttackClass(6,MMX_VS_ABILITY,true)==1);
  assert(MmxVsAttackClass(5,MMX_VS_SPECIAL,true)==1);
  s.phase=255;assert(!MmxVsSetState(&s));s=MmxVsGetState();s.fighters[1].hp=33;assert(!MmxVsSetState(&s));
  puts("MMX VS state, all 64 selections, order, privacy and attack identity passed");return 0;
}
