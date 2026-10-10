#include "mmx_saber_priority.h"
#include "mmx_saber_state.h"

#include <string.h>

#include "mmx_saber_attack.h"
#include "mmx_saber_tuning.h"

enum {
  MMX_SABER_PRIORITY_STAGE = 0x1f7a,
  MMX_SABER_PRIORITY_ENEMY_KIND = 0x0a,
  MMX_SABER_PRIORITY_ENEMY_STATE = 0x01,
  MMX_SABER_PRIORITY_ENEMY_FLAGS = 0x03,
  MMX_SABER_PRIORITY_ENEMY_LIVE = 0x00,
  MMX_SABER_PRIORITY_ENEMY_HP = 0x27,
  MMX_SABER_PRIORITY_ENEMY_ROW = 0x28,
  MMX_SABER_PRIORITY_ENEMY_HARD_SKIP = 0x30,
  MMX_SABER_PRIORITY_ENEMY_PROTECTION = 0x38,
  MMX_SABER_PRIORITY_ARMADILLO_KIND = 0x14,
  MMX_SABER_PRIORITY_ARMADILLO_EXPOSED_ROW = 0x0b,
  MMX_SABER_PRIORITY_PROJECTILE_KIND = 0x0a,
  MMX_SABER_PRIORITY_PROJECTILE_TAG = 0x3e,
  MMX_SABER_PRIORITY_TAG_MASK = 0xff00,
  MMX_SABER_PRIORITY_SABER_TAG = 0x5300,
  MMX_SABER_PRIORITY_WAVE_TAG = 0x5600,
  MMX_SABER_PRIORITY_FINISHER_TAG = 0x5a53
};

typedef struct MmxSaberPriorityProjectileProvenance {
  bool valid;
  uint16_t tag;
  MmxSaberPriorityClass priority_class;
} MmxSaberPriorityProjectileProvenance;

typedef struct MmxSaberPriorityProjectileObservation {
  bool live;
  uint16_t tag;
} MmxSaberPriorityProjectileObservation;

typedef struct MmxSaberPriorityPending {
  bool active;
  uint16_t enemy;
  uint16_t projectile;
  uint16_t tag;
  uint8_t generation;
  uint8_t stage;
  uint8_t native_damage_row;
  MmxSaberPriorityClassification classification;
  uint32_t frame;
} MmxSaberPriorityPending;

typedef struct MmxSaberPriorityState {
  uint32_t frame;
  uint8_t stage;
  bool stage_valid;
  uint16_t enemy_live;
  uint8_t enemy_generation[MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT];
  MmxSaberPriorityHistory history[MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT];
  MmxSaberPriorityProjectileProvenance provenance[
      MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT];
  MmxSaberPriorityProjectileObservation pre_projectile[
      MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT];
  uint8_t pre_shot_mask;
  uint8_t pre_burst;
  bool pre_valid;
  MmxSaberPriorityPending pending;
} MmxSaberPriorityState;

static MmxSaberPriorityState state;

static unsigned word(const uint8_t *p) {
  return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned enemy_index(unsigned slot) {
  if (slot < MMX_SABER_PRIORITY_ENEMY_FIRST ||
      slot >= MMX_SABER_PRIORITY_ENEMY_END ||
      (slot & (MMX_SABER_PRIORITY_SLOT_BYTES - 1)) != 0x28)
    return MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT;
  return (slot - MMX_SABER_PRIORITY_ENEMY_FIRST) /
      MMX_SABER_PRIORITY_SLOT_BYTES;
}

static unsigned projectile_index(unsigned slot) {
  if (slot < MMX_SABER_PRIORITY_PROJECTILE_FIRST ||
      slot >= MMX_SABER_PRIORITY_PROJECTILE_END ||
      (slot & (MMX_SABER_PRIORITY_SLOT_BYTES - 1)) != 0x28)
    return MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT;
  return (slot - MMX_SABER_PRIORITY_PROJECTILE_FIRST) /
      MMX_SABER_PRIORITY_SLOT_BYTES;
}

static unsigned projectile_slot(unsigned index) {
  return MMX_SABER_PRIORITY_PROJECTILE_FIRST +
      index * MMX_SABER_PRIORITY_SLOT_BYTES;
}

static uint16_t projectile_tag(const uint8_t *ram, unsigned slot) {
  return ram ? (uint16_t)word(ram + slot + MMX_SABER_PRIORITY_PROJECTILE_TAG) : 0;
}

static bool projectile_live(const uint8_t *ram, unsigned slot) {
  return ram && projectile_index(slot) <
      MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT && ram[slot] != 0;
}

static bool tag_family(uint16_t tag, unsigned family) {
  return (tag & MMX_SABER_PRIORITY_TAG_MASK) == family;
}

static bool enemy_live(const uint8_t *ram, unsigned slot) {
  return ram && enemy_index(slot) < MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT &&
      ram[slot + MMX_SABER_PRIORITY_ENEMY_LIVE] != 0 &&
      (ram[slot + MMX_SABER_PRIORITY_ENEMY_HP] & 0x7f) != 0;
}

static bool enemy_forced(const uint8_t *ram, unsigned slot) {
  return ram && (ram[slot + MMX_SABER_PRIORITY_ENEMY_HARD_SKIP] != 0 ||
      ram[slot + MMX_SABER_PRIORITY_ENEMY_STATE] == 0x0c ||
      ram[slot + MMX_SABER_PRIORITY_ENEMY_STATE] == 0x0e ||
      (ram[slot + MMX_SABER_PRIORITY_ENEMY_FLAGS] & 0x80) != 0);
}

static bool enemy_eligible_target(const uint8_t *ram, unsigned slot) {
  return enemy_live(ram, slot) && !enemy_forced(ram, slot);
}

static bool armadillo_protected_positive(const uint8_t *ram,
                                         unsigned enemy_slot,
                                         unsigned original) {
  /* Row $00 is Armadillo's armor/guard row in the generated handler. The
   * native fixture's class-3 table value is positive ($4A), so bit 7 alone is
   * not enough to protect that row from the nonzero admission. */
  return original != 0 && !(original & 0x80) &&
      enemy_index(enemy_slot) < MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT &&
      ram && ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_LIVE] != 0 &&
      (ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_HP] & 0x7f) != 0 &&
      ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_KIND] ==
          MMX_SABER_PRIORITY_ARMADILLO_KIND &&
      ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_ROW] ==
          MMX_SABER_PRIORITY_ARMADILLO_EXPOSED_ROW &&
      ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_HARD_SKIP] == 0 &&
      ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_PROTECTION] != 0;
}

static void clear_history_records(void) {
  memset(state.history, 0, sizeof(state.history));
  state.enemy_live = 0;
  memset(&state.pending, 0, sizeof(state.pending));
}

static void clear_provenance(void) {
  memset(state.provenance, 0, sizeof(state.provenance));
  memset(state.pre_projectile, 0, sizeof(state.pre_projectile));
  state.pre_shot_mask = 0;
  state.pre_burst = 0;
  state.pre_valid = false;
}

static void clear_runtime_state(void) {
  memset(&state, 0, sizeof(state));
}

static void sync_stage(const uint8_t *ram) {
  uint8_t stage;
  if (!ram) return;
  stage = ram[MMX_SABER_PRIORITY_STAGE];
  if (!state.stage_valid) {
    state.stage = stage;
    state.stage_valid = true;
    return;
  }
  if (state.stage == stage) return;

  /* A stage identity is stronger than a slot address. Retire all non-saved
   * Saber timeline state before observing the first frame of the new stage. */
  clear_history_records();
  clear_provenance();
  state.frame = 0;
  memset(state.enemy_generation, 0, sizeof(state.enemy_generation));
  state.stage = stage;
}

static uint8_t next_generation(unsigned index) {
  uint8_t generation;
  if (index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT) return 0;
  generation = (uint8_t)(state.enemy_generation[index] + 1);
  if (!generation) generation = 1;
  state.enemy_generation[index] = generation;
  return generation;
}

static bool history_matches_current(const uint8_t *ram, unsigned slot,
                                    unsigned index) {
  const MmxSaberPriorityHistory *history;
  if (!ram || index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT ||
      !enemy_eligible_target(ram, slot))
    return false;
  history = &state.history[index];
  return history->slot == slot &&
      history->kind == ram[slot + MMX_SABER_PRIORITY_ENEMY_KIND] &&
      history->generation == state.enemy_generation[index] &&
      history->stage == state.stage && history->priority != 0;
}

static void sync_enemies(const uint8_t *ram) {
  if (!ram) return;
  sync_stage(ram);
  for (unsigned i = 0; i < MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT; ++i) {
    const unsigned slot = MMX_SABER_PRIORITY_ENEMY_FIRST +
        i * MMX_SABER_PRIORITY_SLOT_BYTES;
    const uint16_t bit = (uint16_t)(1u << i);
    const bool live = enemy_live(ram, slot);
    MmxSaberPriorityHistory *history = &state.history[i];

    if (!live) {
      memset(history, 0, sizeof(*history));
      state.enemy_live &= (uint16_t)~bit;
      continue;
    }
    if (!(state.enemy_live & bit)) {
      next_generation(i);
      state.enemy_live |= bit;
      memset(history, 0, sizeof(*history));
    }
    if (history->slot &&
        (history->kind != ram[slot + MMX_SABER_PRIORITY_ENEMY_KIND] ||
         history->generation != state.enemy_generation[i] ||
         history->stage != state.stage))
      memset(history, 0, sizeof(*history));
    if (enemy_forced(ram, slot)) memset(history, 0, sizeof(*history));
  }
}

static void sync_provenance(const uint8_t *ram) {
  if (!ram) return;
  for (unsigned i = 0; i < MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT; ++i) {
    const unsigned slot = projectile_slot(i);
    const uint16_t tag = projectile_tag(ram, slot);
    MmxSaberPriorityProjectileProvenance *record = &state.provenance[i];
    if (!projectile_live(ram, slot) || (record->valid && record->tag != tag))
      memset(record, 0, sizeof(*record));
  }
}

static void set_result(MmxSaberPriorityClassification *result,
                       MmxSaberPriorityClass priority_class) {
  if (!result) return;
  result->priority_class = priority_class;
  result->priority = (uint8_t)MmxSaberPriorityTuned(priority_class);
}

int MmxSaberPriorityTuned(MmxSaberPriorityClass priority_class) {
  MmxSaberTuningPriorityKind kind;
  switch (priority_class) {
    case MMX_SABER_PRIORITY_CLASS_SLASH1:
      kind = MMX_SABER_TUNING_PRIORITY_SLASH1; break;
    case MMX_SABER_PRIORITY_CLASS_SLASH2:
      kind = MMX_SABER_TUNING_PRIORITY_SLASH2; break;
    case MMX_SABER_PRIORITY_CLASS_SLASH3:
      kind = MMX_SABER_TUNING_PRIORITY_SLASH3; break;
    case MMX_SABER_PRIORITY_CLASS_AIR:
      kind = MMX_SABER_TUNING_PRIORITY_AIR; break;
    case MMX_SABER_PRIORITY_CLASS_WALL:
      kind = MMX_SABER_TUNING_PRIORITY_WALL; break;
    case MMX_SABER_PRIORITY_CLASS_DASH:
      kind = MMX_SABER_TUNING_PRIORITY_DASH; break;
    case MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL:
      kind = MMX_SABER_TUNING_PRIORITY_CHARGE_SMALL; break;
    case MMX_SABER_PRIORITY_CLASS_CHARGE_FULL:
      kind = MMX_SABER_TUNING_PRIORITY_CHARGE_FULL; break;
    case MMX_SABER_PRIORITY_CLASS_MAX_SHOT1:
      kind = MMX_SABER_TUNING_PRIORITY_MAX_SHOT1; break;
    case MMX_SABER_PRIORITY_CLASS_MAX_SHOT2:
      kind = MMX_SABER_TUNING_PRIORITY_MAX_SHOT2; break;
    case MMX_SABER_PRIORITY_CLASS_X3_FINISHER:
      kind = MMX_SABER_TUNING_PRIORITY_X3_FINISHER; break;
    case MMX_SABER_PRIORITY_CLASS_WAVE:
      kind = MMX_SABER_TUNING_PRIORITY_WAVE; break;
    default:
      return 0;
  }
  return MmxSaberTuningPriority(kind);
}

static MmxSaberPriorityClass slash_class(void) {
  const MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
  if (snapshot.phase != SABER_PHASE_ACTIVE) return MMX_SABER_PRIORITY_CLASS_NONE;
  switch (snapshot.kind) {
    case SABER_KIND_GROUND1: return MMX_SABER_PRIORITY_CLASS_SLASH1;
    case SABER_KIND_GROUND2: return MMX_SABER_PRIORITY_CLASS_SLASH2;
    case SABER_KIND_GROUND3: return MMX_SABER_PRIORITY_CLASS_SLASH3;
    case SABER_KIND_AIR: return MMX_SABER_PRIORITY_CLASS_AIR;
    case SABER_KIND_WALL: return MMX_SABER_PRIORITY_CLASS_WALL;
    case SABER_KIND_DASH: return MMX_SABER_PRIORITY_CLASS_DASH;
    default: return MMX_SABER_PRIORITY_CLASS_NONE;
  }
}

static bool buster_provenance_class(const uint8_t *ram, unsigned slot,
                                    MmxSaberPriorityClass *priority_class) {
  const unsigned index = projectile_index(slot);
  const MmxSaberPriorityProjectileProvenance *record;
  if (index >= MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT ||
      !projectile_live(ram, slot)) return false;
  record = &state.provenance[index];
  if (!record->valid || record->tag != projectile_tag(ram, slot)) return false;
  if (ram[slot + MMX_SABER_PRIORITY_PROJECTILE_KIND] != 1 &&
      ram[slot + MMX_SABER_PRIORITY_PROJECTILE_KIND] != 3) return false;
  if (priority_class) *priority_class = record->priority_class;
  return true;
}

bool MmxSaberPriorityClassify(const uint8_t *ram, unsigned projectile_slot,
                              MmxSaberPriorityClassification *result) {
  uint16_t tag;
  MmxSaberPriorityClass priority_class = MMX_SABER_PRIORITY_CLASS_NONE;
  if (result) {
    result->priority_class = MMX_SABER_PRIORITY_CLASS_NONE;
    result->priority = 0;
    result->native_damage_row = 0;
  }
  if (!projectile_live(ram, projectile_slot)) return false;
  sync_stage(ram);
  sync_enemies(ram);
  sync_provenance(ram);
  tag = projectile_tag(ram, projectile_slot);

  if (tag_family(tag, MMX_SABER_PRIORITY_WAVE_TAG))
    priority_class = MMX_SABER_PRIORITY_CLASS_WAVE;
  else if (tag == MMX_SABER_PRIORITY_FINISHER_TAG)
    priority_class = MMX_SABER_PRIORITY_CLASS_X3_FINISHER;
  else if (tag_family(tag, MMX_SABER_PRIORITY_SABER_TAG))
    priority_class = slash_class();
  else if (!buster_provenance_class(ram, projectile_slot, &priority_class))
    return false;

  if (priority_class == MMX_SABER_PRIORITY_CLASS_NONE) return false;
  set_result(result, priority_class);
  return result != NULL && result->priority != 0;
}

unsigned MmxSaberPriorityResponse(uint8_t *ram, unsigned enemy_slot,
                                  unsigned projectile_slot, unsigned original) {
  MmxSaberPriorityClassification candidate;
  MmxSaberPriorityHistory history;
  uint32_t frame;

  /* A response callback is a one-shot producer. A new native collision must
   * never inherit a token from an earlier response, including a reflection or
   * another nonzero response. */
  memset(&state.pending, 0, sizeof(state.pending));
  if (!ram || (original != 0 &&
               !armadillo_protected_positive(ram, enemy_slot, original)) ||
      !MmxSaberPriorityClassify(ram, projectile_slot, &candidate))
    return original;
  /* Classification synchronizes stage state, so sample the frame only after
   * that synchronization. */
  frame = MmxSaberPriorityCurrentFrame();
  if (!MmxSaberPriorityHistoryEligible(ram, enemy_slot, candidate.priority,
                                       frame) ||
      !MmxSaberPriorityHistoryLookup(ram, enemy_slot, &history))
    return original;

  state.pending.active = true;
  state.pending.enemy = (uint16_t)enemy_slot;
  state.pending.projectile = (uint16_t)projectile_slot;
  state.pending.tag = projectile_tag(ram, projectile_slot);
  state.pending.generation = history.generation;
  state.pending.stage = history.stage;
  state.pending.native_damage_row = history.native_damage_row;
  state.pending.classification = candidate;
  state.pending.frame = frame;
  /* Sentinel 1 re-enters the native positive-response path. The final table
   * read at $9E6E is still zero; the damage callback consumes this token. */
  return 1;
}

bool MmxSaberPriorityConsumePending(
    const uint8_t *ram, unsigned enemy_slot, unsigned projectile_slot,
    MmxSaberPriorityClassification *classification) {
  const unsigned index = enemy_index(enemy_slot);
  MmxSaberPriorityHistory history;

  if (classification) memset(classification, 0, sizeof(*classification));
  if (!ram || !state.pending.active ||
      index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT)
    return false;
  sync_enemies(ram);
  if (state.pending.enemy != enemy_slot ||
      state.pending.projectile != projectile_slot ||
      state.pending.frame != state.frame ||
      state.pending.stage != state.stage ||
      state.pending.generation != state.enemy_generation[index] ||
      !projectile_live(ram, projectile_slot) ||
      projectile_tag(ram, projectile_slot) != state.pending.tag ||
      !MmxSaberPriorityHistoryLookup(ram, enemy_slot, &history) ||
      history.generation != state.pending.generation ||
      history.stage != state.pending.stage)
    return false;

  if (classification) {
    *classification = state.pending.classification;
    classification->native_damage_row = state.pending.native_damage_row;
  }
  memset(&state.pending, 0, sizeof(state.pending));
  return true;
}

void MmxSaberPriorityReset(void) {
  clear_runtime_state();
}

void MmxSaberPriorityObservePrePlayer(const uint8_t *ram,
                                      uint8_t shot_mask, uint8_t burst) {
  if (!ram) {
    state.pre_valid = false;
    return;
  }
  sync_stage(ram);
  sync_enemies(ram);
  if (++state.frame == 0) clear_history_records();
  sync_provenance(ram);
  for (unsigned i = 0; i < MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT; ++i) {
    const unsigned slot = projectile_slot(i);
    state.pre_projectile[i].live = projectile_live(ram, slot);
    state.pre_projectile[i].tag = projectile_tag(ram, slot);
  }
  state.pre_shot_mask = shot_mask;
  state.pre_burst = burst;
  state.pre_valid = true;
}

static void record_buster_birth(const uint8_t *ram, unsigned index,
                                uint8_t shot_mask, uint8_t burst) {
  const unsigned slot = projectile_slot(index);
  const uint8_t kind = ram[slot + MMX_SABER_PRIORITY_PROJECTILE_KIND];
  const uint16_t tag = projectile_tag(ram, slot);
  MmxSaberPriorityClass priority_class = MMX_SABER_PRIORITY_CLASS_NONE;
  uint8_t emission_burst = burst == 1 || burst == 2 ? burst : state.pre_burst;
  bool marked_burst = (shot_mask & (uint8_t)(1u << index)) != 0 &&
      (emission_burst == 1 || emission_burst == 2);

  if (!projectile_live(ram, slot) || tag == MMX_SABER_PRIORITY_FINISHER_TAG ||
      tag_family(tag, MMX_SABER_PRIORITY_SABER_TAG) ||
      tag_family(tag, MMX_SABER_PRIORITY_WAVE_TAG)) return;
  if (kind == 1) priority_class = MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL;
  else if (kind == 3)
    priority_class = marked_burst ?
        (emission_burst == 1 ? MMX_SABER_PRIORITY_CLASS_MAX_SHOT1 :
                               MMX_SABER_PRIORITY_CLASS_MAX_SHOT2) :
        MMX_SABER_PRIORITY_CLASS_CHARGE_FULL;
  if (priority_class == MMX_SABER_PRIORITY_CLASS_NONE) return;
  state.provenance[index].valid = true;
  state.provenance[index].tag = tag;
  state.provenance[index].priority_class = priority_class;
}

void MmxSaberPriorityObservePlayerEnd(const uint8_t *ram,
                                      uint8_t shot_mask, uint8_t burst) {
  if (!ram) {
    state.pre_valid = false;
    return;
  }
  sync_stage(ram);
  sync_enemies(ram);
  if (!state.pre_valid) {
    sync_provenance(ram);
    return;
  }
  for (unsigned i = 0; i < MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT; ++i) {
    const unsigned slot = projectile_slot(i);
    const bool live = projectile_live(ram, slot);
    const uint16_t tag = projectile_tag(ram, slot);
    const bool birth = live && (!state.pre_projectile[i].live ||
        state.pre_projectile[i].tag != tag);
    MmxSaberPriorityProjectileProvenance *record = &state.provenance[i];
    if (!live) {
      memset(record, 0, sizeof(*record));
      continue;
    }
    if (record->valid && record->tag != tag) memset(record, 0, sizeof(*record));
    if (birth) {
      memset(record, 0, sizeof(*record));
      record_buster_birth(ram, i, shot_mask, burst);
    }
  }
  state.pre_valid = false;
}

uint32_t MmxSaberPriorityCurrentFrame(void) {
  return state.frame;
}

bool MmxSaberPriorityHistoryRecord(
    const uint8_t *ram, unsigned enemy_slot,
    const MmxSaberPriorityClassification *candidate, uint32_t frame) {
  const unsigned index = enemy_index(enemy_slot);
  MmxSaberPriorityHistory *history;
  if (!ram || !candidate || candidate->priority_class ==
          MMX_SABER_PRIORITY_CLASS_NONE || candidate->priority == 0 ||
      index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT)
    return false;
  sync_enemies(ram);
  if (!enemy_eligible_target(ram, enemy_slot)) return false;
  history = &state.history[index];
  history->slot = (uint16_t)enemy_slot;
  history->kind = ram[enemy_slot + MMX_SABER_PRIORITY_ENEMY_KIND];
  history->generation = state.enemy_generation[index];
  history->stage = state.stage;
  history->priority = candidate->priority;
  history->native_damage_row = candidate->native_damage_row ?
      candidate->native_damage_row : (uint8_t)(ram[enemy_slot + 0x28] + 1);
  history->frame = frame;
  return true;
}

bool MmxSaberPriorityHistoryLookup(const uint8_t *ram, unsigned enemy_slot,
                                   MmxSaberPriorityHistory *history) {
  const unsigned index = enemy_index(enemy_slot);
  if (history) memset(history, 0, sizeof(*history));
  if (!ram || index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT) return false;
  sync_enemies(ram);
  if (!history_matches_current(ram, enemy_slot, index)) return false;
  if (history) *history = state.history[index];
  return true;
}

bool MmxSaberPriorityHistoryEligible(const uint8_t *ram, unsigned enemy_slot,
                                     unsigned candidate_priority,
                                     uint32_t frame) {
  MmxSaberPriorityHistory history;
  uint32_t delta;
  int configured_window;
  if (!candidate_priority || !MmxSaberPriorityHistoryLookup(
          ram, enemy_slot, &history)) return false;
  delta = frame - history.frame;
  configured_window = MmxSaberTuningPriorityWindowFrames();
  return candidate_priority > history.priority &&
      delta <= (unsigned)(configured_window < 0 ? 0 : configured_window);
}

bool MmxSaberPriorityRecord(
    const uint8_t *ram, unsigned enemy_slot,
    const MmxSaberPriorityClassification *candidate, uint32_t frame) {
  return MmxSaberPriorityHistoryRecord(ram, enemy_slot, candidate, frame);
}

bool MmxSaberPriorityLookup(const uint8_t *ram, unsigned enemy_slot,
                            MmxSaberPriorityHistory *history) {
  return MmxSaberPriorityHistoryLookup(ram, enemy_slot, history);
}

bool MmxSaberPriorityEligible(const uint8_t *ram, unsigned enemy_slot,
                              unsigned candidate_priority, uint32_t frame) {
  return MmxSaberPriorityHistoryEligible(ram, enemy_slot, candidate_priority,
                                          frame);
}

uint8_t MmxSaberPriorityEnemyGeneration(const uint8_t *ram,
                                        unsigned enemy_slot) {
  const unsigned index = enemy_index(enemy_slot);
  if (!ram || index >= MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT) return 0;
  sync_enemies(ram);
  return state.enemy_generation[index];
}

size_t MmxSaberPriorityStateSize(void) {
  return sizeof(state);
}

void MmxSaberPriorityStateSave(uint8_t *out) {
  memcpy(out, &state, sizeof(state));
}

void MmxSaberPriorityStateLoad(const uint8_t *in) {
  memcpy(&state, in, sizeof(state));
}
