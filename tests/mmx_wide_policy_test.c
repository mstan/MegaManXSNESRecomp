#include "mmx_wide_policy.h"

#include <assert.h>

static void test_boss_door_stack(void) {
  const uint16_t left_facing[] = {0x00cf, 0x0172, 0x0173, 0x0174};
  const uint16_t right_facing[] = {0x00ce, 0x0172, 0x0173, 0x0174};

  for (int row = 0; row < 4; row++) {
    assert(MmxWidePolicy_IsBossDoorRow(left_facing, row));
    assert(MmxWidePolicy_IsBossDoorRow(right_facing, row));
  }
}

static void test_non_door_stack(void) {
  const uint16_t ordinary_wall[] = {0x00cf, 0x0172, 0x0173, 0x0175};
  assert(!MmxWidePolicy_IsBossDoorRow(ordinary_wall, 0));
  assert(!MmxWidePolicy_IsBossDoorRow(NULL, 0));
  assert(!MmxWidePolicy_IsBossDoorRow(ordinary_wall, -1));
  assert(!MmxWidePolicy_IsBossDoorRow(ordinary_wall, 4));
}

int main(void) {
  test_boss_door_stack();
  test_non_door_stack();
  return 0;
}
