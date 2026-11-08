#include "mmx_damage.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static bool actor_x = true;
bool MmxCoopActorIsX(void) { return actor_x; }

int main(void) {
  assert(MmxDamageParseMultiplier("0.1") == 10);
  assert(MmxDamageParseMultiplier("0.25") == 25);
  assert(MmxDamageParseMultiplier("0.5") == 50);
  assert(MmxDamageParseMultiplier("1") == 100);
  assert(MmxDamageParseMultiplier("1.25") == 125);
  assert(MmxDamageParseMultiplier("2") == 200);
  assert(MmxDamageParseMultiplier("10") == 1000);
  assert(MmxDamageParseMultiplier("0") == 10);
  const char *invalid[] = {NULL,"","-1","nan","inf","1e9","1x",".5","1.","0.001","99999999999999999999"};
  for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i)
    assert(MmxDamageParseMultiplier(invalid[i]) == 100);
  MmxDamageReset();
  assert(!MmxDamageDealtEnabled());
  for (unsigned damage = 0; damage <= 127; ++damage) {
    assert(MmxDamageDealt(damage) == damage);
    assert(MmxDamageReceived(damage, false) == damage);
  }
  MmxDamageSetDealt(200);
  assert(MmxDamageDealt(4) == 8);
  assert(MmxDamageReceived(4, false) == 4);
  actor_x = false;
  assert(MmxDamageDealt(4) == 4);
  actor_x = true;
  MmxDamageSetReceived(50);
  assert(MmxDamageReceived(4, false) == 2);
  assert(MmxDamageReceived(3, false) == 2);
  assert(MmxDamageReceived(1, false) == 1);
  assert(MmxDamageReceived(64, true) == 64);
  assert(MmxDamageReceived(127, true) == 127);
  MmxDamageSetDealt(50);
  assert(MmxDamageDealt(4) == 2);
  /* Exhaust the supported operand/multiplier range: monotonic, bounded,
   * positive hits cannot wrap or become immune, and zero stays zero. */
  for (unsigned multiplier = 10; multiplier <= 1000; ++multiplier) {
    MmxDamageSetDealt(multiplier); MmxDamageSetReceived(multiplier);
    unsigned previous = 0;
    for (unsigned damage = 0; damage <= 127; ++damage) {
      unsigned result = MmxDamageDealt(damage);
      assert(result <= 127 && result >= previous);
      assert((result == 0) == (damage == 0));
      assert(result == MmxDamageReceived(damage, false));
      previous = result;
    }
  }
  MmxDamageSetDealt(UINT_MAX); MmxDamageSetReceived(0);
  assert(MmxDamageDealt(UINT_MAX) == 127);
  assert(MmxDamageReceived(10, false) == 1);
  MmxDamageReset();
  assert(MmxDamageDealt(4) == 4 && MmxDamageReceived(4, false) == 4);
  puts("MMX DAMAGE ARITHMETIC CHECKS PASSED");
  return 0;
}
