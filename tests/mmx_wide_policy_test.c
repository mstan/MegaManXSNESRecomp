#include "mmx_wide_policy.h"

#include <assert.h>

static void test_boss_door_stack(void) {
  const uint16_t chill_penguin[][4] = {
      {0x06df, 0x46df, 0x06ef, 0x46ef},
      {0x06ff, 0x46ff, 0x86ff, 0xc6ff},
      {0x86ef, 0xc6ef, 0x86df, 0xc6df},
  };
  const uint16_t spark_mandrill[][4] = {
      {0x048d, 0x448d, 0x048e, 0x448e},
      {0x048f, 0x448f, 0x848f, 0xc48f},
      {0x848e, 0xc48e, 0x848d, 0xc48d},
  };

  for (int row = 0; row < 3; row++) {
    assert(MmxWidePolicy_IsBossDoorBody(chill_penguin, row));
    assert(MmxWidePolicy_IsBossDoorBody(spark_mandrill, row));
  }
}

static void test_non_door_stack(void) {
  const uint16_t almost_door[][4] = {
      {0x048d, 0x448d, 0x048e, 0x448e},
      {0x048f, 0x448f, 0x848f, 0xc48f},
      {0x848e, 0xc48e, 0x848d, 0xc48c},
  };
  const uint16_t repeated_wall[][4] = {
      {0x0100, 0x0100, 0x0100, 0x0100},
      {0x0100, 0x0100, 0x0100, 0x0100},
      {0x0100, 0x0100, 0x0100, 0x0100},
  };
  assert(!MmxWidePolicy_IsBossDoorBody(almost_door, 0));
  assert(!MmxWidePolicy_IsBossDoorBody(repeated_wall, 0));
  assert(!MmxWidePolicy_IsBossDoorBody(NULL, 0));
  assert(!MmxWidePolicy_IsBossDoorBody(almost_door, -1));
  assert(!MmxWidePolicy_IsBossDoorBody(almost_door, 3));
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
