#include "mmx_wide_policy.h"

#include <assert.h>
#include <string.h>

static uint8_t ram[0x20000];
static void put(unsigned a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static unsigned get(unsigned a) { return ram[a] | (ram[a + 1] << 8); }
static void test_elevator_presentation(void) {
  memset(ram, 0, sizeof(ram));
  ram[0x1f7a] = 7; ram[0xf32] = 0x3d;
  /* Owner's F4: platform center $580, camera $416. Visible in the wide
   * right margin, while the original 808F predicate rejects it. */
  uint16_t distance = 0x580 - 0x416 + 0x60;
  assert(MmxWidePolicy_PresentationCull(ram, 0xf28, distance, 0, true));
  assert(MmxWidePolicy_PresentationCull(ram, 0xf28, distance, 384, false));
  assert(!MmxWidePolicy_PresentationCull(ram, 0xf28, distance, 384, true));
  for (unsigned m = 0; m <= 384; ++m) {
    assert(!MmxWidePolicy_PresentationCull(ram, 0xf28, (uint16_t)-m, m, true));
    assert(MmxWidePolicy_PresentationCull(ram, 0xf28, (uint16_t)(-1 - m), m, true));
    assert(!MmxWidePolicy_PresentationCull(ram, 0xf28, (uint16_t)(0x1bf + m), m, true));
    assert(MmxWidePolicy_PresentationCull(ram, 0xf28, (uint16_t)(0x1c0 + m), m, true));
  }
  ram[0xf32] = 0x3e; /* Adjacent turret family keeps its native controller. */
  assert(MmxWidePolicy_PresentationCull(ram, 0xf28, distance, 384, true));
  ram[0x1632] = 0x3d; /* IDs in another object pool are not the elevator. */
  assert(MmxWidePolicy_PresentationCull(ram, 0x1628, distance, 384, true));
}
static void test_visible_lift_recovery(void) {
  for (unsigned stage = 0; stage < 13; ++stage)
    for (unsigned kind = 0; kind < 4; ++kind) {
      assert(MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x16) == (stage == 7 && kind == 3));
      assert(!MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x17)); /* Attached cannon. */
      assert(!MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x3d)); /* Boarding controller. */
      assert(!MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x37)); /* Room-timed flyer. */
      assert(MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x0b) == (kind == 0));
      assert(!MmxWidePolicy_RescanSpawnRecord(stage, kind, 0x05) || kind == 0); /* Boss vs pickup. */
    }
}
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

static void test_streaker_entry_and_recovery(void) {
  memset(ram, 0, sizeof(ram));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4;
  ram[0x1f7a] = 6; ram[0xe68] = 1; ram[0xe69] = 2;
  ram[0xe72] = 0x37; ram[0xe8f] = 2;
  put(0xe6d, 0x5f6); put(0x1e4d, 0x340);
  put(0xe74, 0xfa7a); ram[0xfa7a] = 1; ram[0xe95] = 0x40; ram[0x1f2c] = 0xc0;
  for (unsigned m = 0; m <= 384; m += 8) {
    put(0xe6d, 0x5f6); ram[0xe73] = 0;
    MmxWidePolicy_StreakerEntrance(ram, 0xe68, m);
    assert(get(0xe6d) == 0x5f6 + (m ? m + 32 : 0));
    if (m) assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
    put(0xe6d, 0x5f6); ram[0xe73] = 1;
    MmxWidePolicy_StreakerEntrance(ram, 0xe68, m);
    assert(get(0xe6d) == 0x5f6 - (m ? m + 32 : 0));
  }
  put(0xe6d, 0x5f6); ram[0xe6a] = 2;
  assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  ram[0xe6a] = 0; ram[0xe6b] = 2;
  assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  ram[0xe6b] = 0; ram[0xe8f] = 1;
  assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  ram[0xe8f] = 2; put(0x1e4d, 0x4e0);
  assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  put(0x1e4d, 0x340);
  assert(MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  assert(!ram[0xe68] && !ram[0xfa7a] && ram[0x1f2c] == 0x80);
  assert(!MmxWidePolicy_RecoverParkedStreaker(ram, 0xe68, 0x5f6));
  assert(!MmxWidePolicy_RecoverParkedStreaker(NULL, 0xe68, 0x5f6));
}

static void test_chain_platform_switches(void) {
  memset(ram, 0, sizeof(ram)); ram[0x1f7a] = 5;
  ram[0x1d12] = 4; ram[0x1d13] = 3;
  assert(MmxWidePolicy_ChainPlatformLine(ram, 0x1d08, 256, 0) == 256);
  uint16_t entry = MmxWidePolicy_ChainPlatformLine(ram, 0x1d08, 256, 384);
  assert((int16_t)(128 - entry) >= 0); /* READY arrival is already inside. */
  ram[0x1d13] = 4;
  assert(MmxWidePolicy_ChainPlatformLine(ram, 0x1d08, 0x3b0, 384) == 0x530);
  for (unsigned param = 0; param < 12; ++param) if (param != 3 && param != 4) {
    ram[0x1d13] = (uint8_t)param;
    assert(MmxWidePolicy_ChainPlatformLine(ram, 0x1d08, 256, 384) == 256);
  }
  ram[0x1d13] = 3; ram[0x1f7a] = 6;
  assert(MmxWidePolicy_ChainPlatformLine(ram, 0x1d08, 256, 384) == 256);
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
  /* The same boss IDs recur in fortress stages. No per-stage exception
   * may put any encounter back into the widened ordinary-enemy scan. */
  const uint8_t bosses[] = {2,5,7,0x0a,0x0c,0x14,0x31,0x52,0x5d,0x62,0x63,0x65,3,0x22};
  for (unsigned stage = 0; stage < 13; ++stage) for (unsigned i = 0; i < sizeof(bosses); ++i) {
    assert(MmxWidePolicy_IsBossEncounter(bosses[i]));
    assert(!MmxWidePolicy_SpawnRecordAllowed(stage, 3, bosses[i], false));
    assert(MmxWidePolicy_SpawnRecordAllowed(stage, 3, bosses[i], true));
  }
  assert(!MmxWidePolicy_IsBossEncounter(0x37)); /* A streaker is not an encounter. */
  assert(!MmxWidePolicy_IsBossEncounter(0x0b)); /* Nor is a Heart Tank. */
  assert(MmxWidePolicy_SpawnRecordAllowed(0, 3, 0x11, false)); /* Highway traffic enemy. */
  assert(MmxWidePolicy_SpawnRecordAllowed(6, 0, 0x0b, false));
  assert(MmxWidePolicy_SpawnRecordAllowed(6, 0, 0x0b, true));
  assert(!MmxWidePolicy_SpawnRecordAllowed(6, 2, 0x0b, false));
  for (unsigned id = 1; id <= 5; ++id) {
    assert(MmxWidePolicy_IsCollectible(id));
    assert(MmxWidePolicy_SpawnRecordAllowed(3, 0, id, false));
    assert(MmxWidePolicy_SpawnRecordAllowed(3, 0, id, true));
    assert(!MmxWidePolicy_SpawnRecordAllowed(3, 2, id, false));
  }
  assert(!MmxWidePolicy_IsCollectible(7)); /* Minecart. */
  assert(!MmxWidePolicy_IsCollectible(10)); /* Stage mechanism. */
  assert(!MmxWidePolicy_SpawnRecordAllowed(3, 0, 7, false));

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
  ram[0xd3] = 4; ram[0xc3] = 0xc0; ram[0x1f10] = 2;
  assert(MmxWidePolicy_IsStageScene(ram)); /* Spark's light HDMA remains gameplay. */
  ram[0x1f10] = 8;
  assert(!MmxWidePolicy_IsStageScene(ram)); /* Weapons menu, same outer game mode. */
  ram[0xc3] = 0;
  assert(MmxWidePolicy_IsStageScene(ram)); /* A hidden HUD alone is not a menu. */
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
  test_streaker_entry_and_recovery();
  test_chain_platform_switches();
  test_bee_camera_and_descent();
  test_flyer_and_armor_range();
  test_elevator_presentation();
  test_visible_lift_recovery();
  return 0;
}
