#include "mmx_wide_policy.h"

#include <assert.h>
#include <string.h>

static uint8_t ram[0x20000];
static void put(unsigned a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static unsigned get(unsigned a) { return ram[a] | (ram[a + 1] << 8); }
static void test_flyer_and_armor_range(void) {
  for (unsigned margin = 0; margin <= 384; margin += 8) {
    /* A flyer at the spawn lead can approach within its 64px attack range
     * before it exhausts the leash, including the extra spawn column. */
    assert(MmxWidePolicy_FlyerLeash(margin) > 128 + margin + 32 - 64);
    assert(!MmxWidePolicy_RideArmorCull((uint16_t)(-128 - (int)margin + 128), margin));
    assert(MmxWidePolicy_RideArmorCull((uint16_t)(-129 - (int)margin + 128), margin));
    assert(!MmxWidePolicy_RideArmorCull((uint16_t)(383 + margin + 128), margin));
    assert(MmxWidePolicy_RideArmorCull((uint16_t)(384 + margin + 128), margin));
  }
  assert(MmxWidePolicy_FlyerLeash(0) == 160);
  memset(ram, 0, sizeof(ram));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 8;
  put(0xe1d, 0x1220); put(0xe20, 0x390); ram[0xe2e] = 0x4a;
  ram[0xe3f] = 0x10; put(0xe38, 0xbb4c); put(0xbad, 0xeca);
  put(0x1e4d, 0xe4a); put(0x1e50, 0x300);
  assert(MmxWidePolicy_PrematureRideArmor(ram));
  assert(!MmxWidePolicy_RecoverRideArmor(ram, 384)); /* Still beyond even max view. */
  put(0x1e4d, 0x1000);
  assert(!MmxWidePolicy_RecoverRideArmor(ram, 0));
  ram[0xe3f] = 0;
  assert(!MmxWidePolicy_PrematureRideArmor(ram)); /* Destroyed. */
  ram[0xe3f] = 0x10; ram[0xe2f] = 1;
  assert(!MmxWidePolicy_PrematureRideArmor(ram)); /* Already animated. */
  ram[0xe2f] = 0; put(0xe20, 0x391);
  assert(!MmxWidePolicy_PrematureRideArmor(ram)); /* Already moved. */
  put(0xe20, 0x390); put(0x1e50, 0x100);
  assert(!MmxWidePolicy_RecoverRideArmor(ram, 384)); /* Preserve vertical cull. */
  put(0x1e50, 0x300);
  assert(MmxWidePolicy_RecoverRideArmor(ram, 384));
  assert(ram[0xe18] == 1 && get(0xe1d) == 0x1220 && get(0xbad) == 0xeca);
  assert(!MmxWidePolicy_RecoverRideArmor(ram, 384)); /* No duplication. */
}
static void test_bee_camera_and_descent(void) {
  memset(ram, 0, sizeof(ram));
  ram[0xe68] = 1; ram[0xe69] = 2; ram[0xe72] = 0x22;
  put(0xe6d, 0xaf4); put(0xe99, 0x1b00); /* Saved right limit. */
  put(0x1e4d, 0x885); put(0xbad, 0x8cb);
  put(0x1e5e, 0xa04); put(0x1e60, 0xa04);
  assert(MmxWidePolicy_BeeEntrance(ram, 0xe68, 376) == 0);
  assert(get(0x1e5e) == 0 && get(0x1e60) == 0x1b00);
  assert(get(0x1e4d) == 0x885 && get(0xbad) == 0x8cb);
  put(0x1e4d, 0x9df); /* Last pixel before the native spawn column. */
  assert(MmxWidePolicy_BeeEntrance(ram, 0xe68, 376) == 0);
  assert(get(0x1e5e) == 0);
  put(0x1e4d, 0x9e0);
  assert(MmxWidePolicy_BeeEntrance(ram, 0xe68, 376) == 376);
  assert(get(0x1e5e) == 0xa04 && get(0x1e60) == 0xa04);
  assert(MmxWidePolicy_BeeEntrance(ram, 0xe68, 128) == 128);
  ram[0xe69] = 4; put(0x1e4d, 0x885);
  assert(MmxWidePolicy_BeeEntrance(ram, 0xe68, 376) == 376);
  assert(get(0x1e5e) == 0xa04); /* Never unwind a running encounter. */
}

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
  assert(!MmxWidePolicy_SpawnRecordAllowed(0x00, 3, 0x22, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x00, 3, 0x22, true));
  assert(!MmxWidePolicy_SpawnRecordAllowed(0x08, 3, 0x02, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x08, 3, 0x02, true));
  assert(!MmxWidePolicy_SpawnRecordAllowed(6, 3, 0x37, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(6, 3, 0x37, true));
  assert(MmxWidePolicy_SpawnRecordAllowed(6, 0, 0x0b, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(6, 0, 0x0b, true));
  assert(!MmxWidePolicy_SpawnRecordAllowed(6, 2, 0x0b, false));
  assert(!MmxWidePolicy_SpawnRecordAllowed(6, 0, 0x05, false));

  /* Highway traffic remains eligible in both passes. */
  assert(MmxWidePolicy_SpawnRecordAllowed(0x00, 1, 0x21, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(0x00, 1, 0x21, true));
}

int main(void) {
  memset(ram, 0, sizeof(ram)); ram[0xd1] = 2; ram[0xd2] = 4;
  for (int scene = 0; scene <= 8; scene += 2) {
    ram[0xd3] = (uint8_t)scene; assert(MmxWidePolicy_IsStageScene(ram));
  }
  ram[0xd3] = 10; assert(!MmxWidePolicy_IsStageScene(ram));
  ram[0xd2] = 2; ram[0xd3] = 2; assert(!MmxWidePolicy_IsStageScene(ram));
  assert(!MmxWidePolicy_ForceNativeSpawnTiming(9, 0x8ff, 0));
  assert(MmxWidePolicy_ForceNativeSpawnTiming(9, 0x900, 0));
  assert(MmxWidePolicy_ForceNativeSpawnTiming(9, 0xa80, 0));
  assert(!MmxWidePolicy_ForceNativeSpawnTiming(9, 0xa81, 0));
  /* 32:9: rounded margin 216 plus 32-pixel spawn slack. */
  assert(!MmxWidePolicy_ForceNativeSpawnTiming(9, 0x807, 248));
  assert(MmxWidePolicy_ForceNativeSpawnTiming(9, 0x808, 248));
  assert(MmxWidePolicy_ForceNativeSpawnTiming(9, 0x880, 248));
  assert(!MmxWidePolicy_ForceNativeSpawnTiming(0, 0x880, 248));
  test_boss_door_stack();
  test_non_door_stack();
  test_spawn_cursors_are_independent();
  test_spawn_record_ownership();
  test_bee_camera_and_descent();
  test_flyer_and_armor_range();
  return 0;
}
