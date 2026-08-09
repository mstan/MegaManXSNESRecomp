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

static void test_spawn_cursors_are_independent(void) {
  MmxWideSpawnCursor cursor = {0};

  assert(MmxWidePolicy_BeginWideSpawnPass(&cursor, 0x9000) == 0x9000);
  MmxWidePolicy_EndWideSpawnPass(&cursor, 0x9040);

  /* Advancing the native cursor must not rewind or replace the widened one. */
  assert(MmxWidePolicy_BeginWideSpawnPass(&cursor, 0x9020) == 0x9040);
  MmxWidePolicy_EndWideSpawnPass(&cursor, 0x9060);
  assert(cursor.wide == 0x9060);
  assert(cursor.valid);
}

static void test_spawn_record_ownership(void) {
  /* Ordinary enemies are early/wide only; controllers are native only. */
  assert(MmxWidePolicy_SpawnRecordAllowed(0x06, 3, 0x20, false));
  assert(!MmxWidePolicy_SpawnRecordAllowed(0x06, 3, 0x20, true));
  assert(!MmxWidePolicy_SpawnRecordAllowed(0x06, 2, 0x15, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x06, 2, 0x15, true));

  /* Spark's kind-3 mid-boss controller is deliberately native-timed. */
  assert(!MmxWidePolicy_SpawnRecordAllowed(0x06, 3, 0x03, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x06, 3, 0x03, true));

  /* Highway traffic remains eligible in both passes. */
  assert(MmxWidePolicy_SpawnRecordAllowed(0x00, 1, 0x21, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x00, 1, 0x21, true));
}

int main(void) {
  test_boss_door_stack();
  test_non_door_stack();
  test_spawn_cursors_are_independent();
  test_spawn_record_ownership();
  return 0;
}
