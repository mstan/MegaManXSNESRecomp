#include "mmx_saber_attack.h"
#include "mmx_saber_priority.h"
#include "mmx_saber_tuning.h"

#include <stdio.h>
#include <string.h>

static int failures;
static uint8_t ram[0x20000];
static MmxSaberAttackSnapshot test_attack;

static bool window_reader(const char *id, char *value, size_t size,
                          void *context) {
  (void)context;
  if (strcmp(id, "priority_window_frames") != 0) return false;
  snprintf(value, size, "%s", "71");
  return true;
}

/* The classifier consumes the existing Saber attack snapshot. Keep this
 * focused unit independent from the renderer/ROM host by supplying the same
 * small query used by the runtime owner. */
MmxSaberAttackSnapshot MmxSaberAttackSnapshotGet(void) {
  return test_attack;
}

static void check(int condition, const char *message) {
  if (condition) return;
  fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

static void make_enemy(unsigned slot, uint8_t kind) {
  ram[slot] = 1;
  ram[slot + 0x27] = 0x10;
  ram[slot + 0x0a] = kind;
  ram[slot + 0x01] = 0;
  ram[slot + 0x03] = 0;
  ram[slot + 0x30] = 0;
}

static void make_projectile(unsigned slot, uint8_t kind, uint16_t tag) {
  ram[slot] = 1;
  ram[slot + 0x0a] = kind;
  ram[slot + 0x3e] = (uint8_t)tag;
  ram[slot + 0x3f] = (uint8_t)(tag >> 8);
}

static void check_class(unsigned slot, MmxSaberPriorityClass expected,
                        unsigned priority, const char *message) {
  MmxSaberPriorityClassification result;
  check(MmxSaberPriorityClassify(ram, slot, &result) &&
            result.priority_class == expected && result.priority == priority,
        message);
}

static void history_checks(void) {
  const unsigned enemy = 0x0ea8;
  MmxSaberPriorityClassification candidate = {
      MMX_SABER_PRIORITY_CLASS_SLASH2, 3};
  MmxSaberPriorityHistory history;
  uint8_t first_generation;

  memset(ram, 0, sizeof(ram));
  ram[0x1f7a] = 4;
  make_enemy(enemy, 0x22);
  MmxSaberPriorityReset();

  check(MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 100),
        "history records a live Saber hit");
  check(MmxSaberPriorityHistoryLookup(ram, enemy, &history) &&
            history.slot == enemy && history.kind == 0x22 &&
            history.stage == 4 && history.priority == 3 &&
            history.frame == 100 && history.generation != 0,
        "history returns slot/kind/generation/stage/priority/frame");
  first_generation = history.generation;

  check(!MmxSaberPriorityHistoryEligible(ram, enemy, 3, 100) &&
            !MmxSaberPriorityHistoryEligible(ram, enemy, 2, 100),
        "equal and lower priorities are rejected strictly");
  check(MmxSaberPriorityHistoryEligible(ram, enemy, 4, 170) &&
            !MmxSaberPriorityHistoryEligible(ram, enemy, 4, 171),
        "the default 70-frame window is inclusive at its upper edge");

  MmxSaberTuningLoad(NULL, NULL);
  MmxSaberTuningLoad(window_reader, NULL);
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 200) &&
            MmxSaberPriorityHistoryEligible(ram, enemy, 4, 271) &&
            !MmxSaberPriorityHistoryEligible(ram, enemy, 4, 272),
        "the history uses the tuned 71-frame window");

  ram[enemy] = 0;
  ram[enemy + 0x27] = 0;
  check(!MmxSaberPriorityHistoryLookup(ram, enemy, &history),
        "dead enemy slots have no history");
  make_enemy(enemy, 0x22);
  check(MmxSaberPriorityEnemyGeneration(ram, enemy) != first_generation &&
            !MmxSaberPriorityHistoryLookup(ram, enemy, &history),
        "dead-to-live slot reuse bumps generation and invalidates history");

  check(MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 300),
        "reused live slot accepts a new history record");
  ram[enemy + 0x0a] = 0x23;
  check(!MmxSaberPriorityHistoryLookup(ram, enemy, &history),
        "a changed enemy kind invalidates the record");
  ram[enemy + 0x0a] = 0x22;
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 301),
        "kind-restored slot accepts a fresh record");

  ram[0x1f7a] = 5;
  check(!MmxSaberPriorityHistoryLookup(ram, enemy, &history),
        "a stage change invalidates the record");
  ram[0x1f7a] = 4;
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 302),
        "stage-restored slot accepts a fresh record");

  ram[enemy + 0x30] = 1;
  check(!MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 303),
        "hard-skip enemy state rejects recording");
  ram[enemy + 0x30] = 0;
  ram[enemy + 0x01] = 0x0c;
  check(!MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 304),
        "forced enemy state 0x0C rejects recording");
  ram[enemy + 0x01] = 0;
  ram[enemy + 0x01] = 0x0e;
  check(!MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 305),
        "forced enemy state 0x0E rejects recording");
  ram[enemy + 0x01] = 0;
  ram[enemy + 0x03] = 0x80;
  check(!MmxSaberPriorityHistoryRecord(ram, enemy, &candidate, 306),
        "forced enemy flag rejects recording");

  MmxSaberPriorityReset();
  ram[enemy + 0x03] = 0;
  check(!MmxSaberPriorityHistoryLookup(ram, enemy, &history),
        "reset clears non-serialized history");
  MmxSaberTuningLoad(NULL, NULL);
}

static void classifier_checks(void) {
  const unsigned slash = 0x1228;
  const unsigned small = 0x1268;
  const unsigned full = 0x12a8;
  const unsigned max1 = 0x12e8;
  const unsigned max2 = 0x1328;
  const unsigned finisher = 0x1368;
  const unsigned wave = 0x13a8;

  memset(ram, 0, sizeof(ram));
  ram[0x1f7a] = 4;
  MmxSaberPriorityReset();
  test_attack = (MmxSaberAttackSnapshot){
      SABER_KIND_GROUND1, 0, SABER_PHASE_ACTIVE, 0, 0, 0, 0};
  make_projectile(slash, 3, 0x5301);
  check_class(slash, MMX_SABER_PRIORITY_CLASS_SLASH1, 2,
              "ground slash 1 classifies with tuned priority");

  MmxSaberPriorityReset();
  memset(ram + small, 0, 64);
  MmxSaberPriorityObservePrePlayer(ram, 0, 0);
  make_projectile(small, 1, 0);
  MmxSaberPriorityObservePlayerEnd(ram, 0, 0);
  check_class(small, MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL, 1,
              "tier-4 provenance classifies as small charge");

  MmxSaberPriorityReset();
  memset(ram + full, 0, 64);
  MmxSaberPriorityObservePrePlayer(ram, 0, 0);
  make_projectile(full, 3, 0);
  MmxSaberPriorityObservePlayerEnd(ram, 0, 0);
  check_class(full, MMX_SABER_PRIORITY_CLASS_CHARGE_FULL, 1,
              "non-burst class-3 provenance classifies as full charge");

  MmxSaberPriorityReset();
  memset(ram + max1, 0, 64);
  MmxSaberPriorityObservePrePlayer(ram, 0, 1);
  make_projectile(max1, 3, 0);
  MmxSaberPriorityObservePlayerEnd(ram, 1u << 3, 1);
  check_class(max1, MMX_SABER_PRIORITY_CLASS_MAX_SHOT1, 2,
              "burst-1 provenance classifies as maximum shot 1");

  MmxSaberPriorityReset();
  memset(ram + max2, 0, 64);
  MmxSaberPriorityObservePrePlayer(ram, 0, 2);
  make_projectile(max2, 3, 0);
  MmxSaberPriorityObservePlayerEnd(ram, 1u << 4, 2);
  check_class(max2, MMX_SABER_PRIORITY_CLASS_MAX_SHOT2, 3,
              "burst-2 provenance classifies as maximum shot 2");

  MmxSaberPriorityReset();
  make_projectile(finisher, 3, 0x5a53);
  check_class(finisher, MMX_SABER_PRIORITY_CLASS_X3_FINISHER, 4,
              "the upstream $5A53 finisher classifies");
  make_projectile(wave, 3, 0x5601);
  check_class(wave, MMX_SABER_PRIORITY_CLASS_WAVE, 5,
              "the $5600 wave classifies");

  MmxSaberPriorityReset();
  memset(ram + small, 0, 64);
  MmxSaberPriorityObservePrePlayer(ram, 0, 0);
  make_projectile(small, 3, 0);
  MmxSaberPriorityObservePlayerEnd(ram, 1u << 1, 1);
  check_class(small, MMX_SABER_PRIORITY_CLASS_MAX_SHOT1, 2,
              "class-3 burst provenance is not collapsed into full charge");
  memset(ram + small, 0, 64);
  check(!MmxSaberPriorityClassify(ram, small, NULL),
        "dead buster slots are not classified");
}

static void response_checks(void) {
  const unsigned enemy = 0x0ea8;
  const unsigned lower = 0x1228;
  const unsigned higher = 0x1268;
  MmxSaberPriorityClassification lower_class = {
      MMX_SABER_PRIORITY_CLASS_SLASH1, 2};
  MmxSaberPriorityClassification pending_class;

  memset(ram, 0, sizeof(ram));
  ram[0x1f7a] = 4;
  make_enemy(enemy, 0x22);
  make_projectile(lower, 3, 0x5301);
  make_projectile(higher, 3, 0x5302);
  MmxSaberPriorityReset();
  test_attack = (MmxSaberAttackSnapshot){
      SABER_KIND_GROUND1, 0, SABER_PHASE_ACTIVE, 0, 0, 0, 0};
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &lower_class, 0),
        "response test establishes lower-priority history");

  test_attack.kind = SABER_KIND_GROUND2;
  check(MmxSaberPriorityResponse(ram, enemy, higher, 0) == 1,
        "strictly higher Saber response arms the sentinel");
  check(MmxSaberPriorityConsumePending(ram, enemy, higher, &pending_class) &&
            pending_class.priority_class == MMX_SABER_PRIORITY_CLASS_SLASH2 &&
            pending_class.priority == 3,
        "pending token retains the accepted classification");
  check(!MmxSaberPriorityConsumePending(ram, enemy, higher, NULL),
        "pending response token is consumed exactly once");

  MmxSaberPriorityReset();
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &lower_class, 0),
        "reflection test establishes lower-priority history");
  check(MmxSaberPriorityResponse(ram, enemy, higher, 0x80) == 0x80 &&
            !MmxSaberPriorityConsumePending(ram, enemy, higher, NULL),
        "bit-7 reflection response is preserved and never bypassed");

  MmxSaberPriorityReset();
  check(MmxSaberPriorityHistoryRecord(ram, enemy, &lower_class, 0),
        "window test establishes lower-priority history");
  for (unsigned i = 0; i < 71; ++i)
    MmxSaberPriorityObservePrePlayer(ram, 0, 0);
  check(MmxSaberPriorityResponse(ram, enemy, higher, 0) == 0,
        "a higher priority after the configured window is not bypassed");

  MmxSaberPriorityReset();
  check(MmxSaberPriorityResponse(ram, enemy, higher, 0) == 0,
        "reset clears response history before a new bypass candidate");
}

int main(void) {
  history_checks();
  classifier_checks();
  response_checks();
  if (failures) return 1;
  puts("ok: saber-priority");
  return 0;
}
