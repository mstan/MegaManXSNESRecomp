#include "mmx_saber_wave_runtime.h"
#include "mmx_saber_state.h"

#include <string.h>

#include "../mmx_wide_policy.h"
#include "../mmx_zero.h"
#include "mmx_saber_tuning.h"

enum {
  MMX_SABER_WAVE_SLOT_BYTES = 64,
  MMX_SABER_WAVE_SLOT_FIRST = 0x1228,
  MMX_SABER_WAVE_SLOT_END = 0x1428,
  MMX_SABER_WAVE_STAGE = 0x1f7a,
  MMX_SABER_WAVE_CAMERA = 0x1e4d,
  MMX_SABER_WAVE_COUNT = 0x0bdd,
  /* The native contact bridge writes the projectile's $30..$3A response
   * bytes after the damage callback. Keep the two guards used by the old
   * target-live check in the otherwise unused tail before the native $3e tag;
   * the original ABI bytes remain the native response record. */
  MMX_SABER_WAVE_ID_MARKER = 0x3b,
  MMX_SABER_WAVE_ID_KIND = 0x3c,
  MMX_SABER_WAVE_ID_STAGE = 0x3d,
  MMX_SABER_WAVE_ID_MARKER_VALUE = 0xa5
};

typedef struct MmxSaberWaveCollision {
  uint8_t *rom;
  uint8_t bytes[MMX_SABER_WAVE_COLLISION_RECORD_BYTES];
  bool installed;
  bool ready;
} MmxSaberWaveCollision;

static uint8_t *runtime_ram;
static uint8_t next_generation;
static uint8_t observed_stage;
static bool observed_stage_valid;
static uint8_t collision_record[MMX_SABER_WAVE_COLLISION_RECORD_BYTES];
static bool collision_record_valid;
static MmxSaberWaveCollision collision;

static unsigned word(const uint8_t *p) {
  return p[0] | ((unsigned)p[1] << 8);
}

static void putword(uint8_t *p, unsigned value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static bool slot_valid(unsigned slot) {
  return slot >= MMX_SABER_WAVE_SLOT_FIRST &&
      slot < MMX_SABER_WAVE_SLOT_END &&
      (slot & (MMX_SABER_WAVE_SLOT_BYTES - 1)) == 0x28;
}

static bool tag_family(unsigned tag) {
  return (tag & MMX_SABER_WAVE_TAG_FAMILY_MASK) ==
      MMX_SABER_WAVE_TAG_FAMILY;
}

static bool tagged(const uint8_t *ram, unsigned slot) {
  return ram && slot_valid(slot) && tag_family(word(ram + slot + 0x3e));
}

static bool live(const uint8_t *ram, unsigned slot) {
  return tagged(ram, slot) && ram[slot] != 0;
}

static bool target_slot_valid(unsigned enemy) {
  return enemy >= 0xe68 && enemy < 0x1228 &&
      (enemy & (MMX_SABER_WAVE_SLOT_BYTES - 1)) == 0x28;
}

static unsigned clamp_tuned_damage(int damage) {
  if (damage < 0) return 0;
  if (damage > 32) return 32;
  return (unsigned)damage;
}

static unsigned wave_tuned_damage(const uint8_t *ram, unsigned enemy) {
  const bool boss = ram && target_slot_valid(enemy) &&
      MmxWidePolicy_IsBossEncounter(ram[enemy + 0x0a]);
  const int damage = boss ?
      MmxSaberTuningBossDamage(MMX_SABER_TUNING_DAMAGE_WAVE) :
      MmxSaberTuningNormalDamage(MMX_SABER_TUNING_DAMAGE_WAVE);
  return clamp_tuned_damage(damage);
}

static bool identity_recorded(const uint8_t *ram, unsigned slot) {
  return live(ram, slot) &&
      ram[slot + MMX_SABER_WAVE_ID_MARKER] ==
          MMX_SABER_WAVE_ID_MARKER_VALUE;
}

static uint8_t target_kind(const uint8_t *ram, unsigned slot) {
  return identity_recorded(ram, slot) ?
      ram[slot + MMX_SABER_WAVE_ID_KIND] :
      ram[slot + MMX_SABER_WAVE_SLOT_TARGET_KIND];
}

static uint8_t target_stage(const uint8_t *ram, unsigned slot) {
  return identity_recorded(ram, slot) ?
      ram[slot + MMX_SABER_WAVE_ID_STAGE] :
      ram[slot + MMX_SABER_WAVE_SLOT_STAGE];
}

static bool free_slot(const uint8_t *ram, unsigned slot) {
  return slot_valid(slot) && word(ram + slot) == 0;
}

static uint8_t generation_for_ram(const uint8_t *ram, uint8_t candidate) {
  for (unsigned slot = MMX_SABER_WAVE_SLOT_FIRST;
       slot < MMX_SABER_WAVE_SLOT_END; slot += MMX_SABER_WAVE_SLOT_BYTES) {
    if (word(ram + slot + 0x3e) ==
        (MMX_SABER_WAVE_TAG_FAMILY | candidate))
      return 0;
  }
  return candidate;
}

static uint8_t allocate_generation(const uint8_t *ram) {
  uint8_t candidate = next_generation;
  for (unsigned attempt = 0; attempt < 255; ++attempt) {
    if (++candidate == 0) candidate = 1;
    if (!ram || generation_for_ram(ram, candidate)) {
      next_generation = candidate;
      return candidate;
    }
  }
  return 0;
}

static uint8_t *target_ram(uint8_t *ram) {
  if (ram) runtime_ram = ram;
  return ram ? ram : runtime_ram;
}

static void retire_slot(uint8_t *ram, unsigned slot) {
  bool counted;
  if (!tagged(ram, slot)) return;
  counted = ram[slot] != 0;
  memset(ram + slot, 0, MMX_SABER_WAVE_SLOT_BYTES);
  if (counted && ram[MMX_SABER_WAVE_COUNT]) --ram[MMX_SABER_WAVE_COUNT];
}

static bool offscreen(const uint8_t *ram, unsigned slot) {
  const int x = (int16_t)word(ram + slot + 5);
  const int camera = (int16_t)word(ram + MMX_SABER_WAVE_CAMERA);
  return x < camera - MMX_SABER_WAVE_MAX_VIEW_MARGIN ||
      x > camera + 256 + MMX_SABER_WAVE_MAX_VIEW_MARGIN;
}

static void collision_reset(void) {
  static const uint8_t empty[MMX_SABER_WAVE_COLLISION_RECORD_BYTES] =
      {0xff, 0xff, 0xff, 0xff};
  if (collision.rom &&
      !memcmp(collision.rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
              collision.bytes, sizeof(collision.bytes)))
    memcpy(collision.rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
           empty, sizeof(empty));
  collision.rom = NULL;
  collision.installed = false;
  collision.ready = false;
}

bool MmxSaberWaveRuntimeReserve(uint8_t *ram, unsigned *slot) {
  unsigned first = 0;
  unsigned free_count = 0;
  uint8_t generation;
  if (slot) *slot = 0;
  if (!ram) return false;
  runtime_ram = ram;
  for (unsigned d = MMX_SABER_WAVE_SLOT_FIRST;
       d < MMX_SABER_WAVE_SLOT_END; d += MMX_SABER_WAVE_SLOT_BYTES) {
    if (!free_slot(ram, d)) continue;
    if (!first) first = d;
    ++free_count;
  }
  /* The finisher's slash and its future wave must be atomic. */
  if (free_count < 2) return false;
  generation = allocate_generation(ram);
  if (!generation) return false;
  memset(ram + first, 0, MMX_SABER_WAVE_SLOT_BYTES);
  ram[first + 1] = 0x80;
  putword(ram + first + 0x3e, MMX_SABER_WAVE_TAG_FAMILY | generation);
  ram[first + MMX_SABER_WAVE_SLOT_STAGE] = ram[MMX_SABER_WAVE_STAGE];
  if (slot) *slot = first;
  return true;
}

bool MmxSaberWaveRuntimeReleaseReservation(uint8_t *ram, unsigned slot) {
  uint8_t *target = target_ram(ram);
  if (!target || !tagged(target, slot) || target[slot]) return false;
  memset(target + slot, 0, MMX_SABER_WAVE_SLOT_BYTES);
  return true;
}

bool MmxSaberWaveRuntimePublish(uint8_t *ram, unsigned slot) {
  MmxZeroState zero;
  unsigned tag;
  uint8_t facing;
  uint8_t *target = target_ram(ram);
  if (!target || !slot_valid(slot) || !tagged(target, slot)) return false;
  if (!collision.ready) {
    MmxSaberWaveRuntimeReleaseReservation(target, slot);
    return false;
  }
  if (target[slot] || target[slot + 1] != 0x80) {
    /* A native allocator may have claimed the promised address. Preserve it
     * and make its former wave tag non-owning, exactly as the old branch did. */
    if (target[slot]) putword(target + slot + 0x3e, 0x5758);
    return false;
  }
  tag = word(target + slot + 0x3e);
  zero = MmxZeroGetState();
  facing = zero.facing & 0x40;
  memset(target + slot, 0, MMX_SABER_WAVE_SLOT_BYTES);
  target[slot] = 1;
  target[slot + 1] = 2;
  target[slot + 10] = 3;
  target[slot + 0x11] = facing;
  putword(target + slot + 5,
          (uint16_t)((int16_t)word(target + 0x0bad) +
              (facing ? MMX_SABER_WAVE_SPAWN_OFFSET_X :
                        -MMX_SABER_WAVE_SPAWN_OFFSET_X)));
  putword(target + slot + 8,
          (uint16_t)((int16_t)word(target + 0x0bb0) +
              MMX_SABER_WAVE_SPAWN_OFFSET_Y));
  putword(target + slot + 0x20, MMX_SABER_WAVE_COLLISION_POINTER);
  target[slot + MMX_SABER_WAVE_SLOT_AGE] = 0;
  target[slot + MMX_SABER_WAVE_SLOT_STATE] =
      MMX_SABER_WAVE_SLOT_STATE_TRAVEL;
  target[slot + MMX_SABER_WAVE_SLOT_PULSES] = 0;
  target[slot + MMX_SABER_WAVE_SLOT_COUNTDOWN] = 0;
  target[slot + MMX_SABER_WAVE_SLOT_STAGE] = target[MMX_SABER_WAVE_STAGE];
  /* Old src/mmx_saber.c:1701-1704 keeps the publication frame stationary. */
  target[slot + MMX_SABER_WAVE_SLOT_BIRTH] = 1;
  putword(target + slot + 0x3e, tag);
  ++target[MMX_SABER_WAVE_COUNT];
  return true;
}

bool MmxSaberWaveRuntimeObserveStage(uint8_t *ram) {
  uint8_t *target = target_ram(ram);
  bool changed = false;
  if (!target) return false;
  if (observed_stage_valid && observed_stage != target[MMX_SABER_WAVE_STAGE])
    changed = true;
  observed_stage = target[MMX_SABER_WAVE_STAGE];
  observed_stage_valid = true;
  for (unsigned slot = MMX_SABER_WAVE_SLOT_FIRST;
       slot < MMX_SABER_WAVE_SLOT_END; slot += MMX_SABER_WAVE_SLOT_BYTES)
    if (tagged(target, slot)) {
      if (target_stage(target, slot) != target[MMX_SABER_WAVE_STAGE]) {
        retire_slot(target, slot);
        changed = true;
      }
    }
  return changed;
}

void MmxSaberWaveRuntimeRetireAll(uint8_t *ram) {
  uint8_t *target = target_ram(ram);
  if (!target) return;
  for (unsigned slot = MMX_SABER_WAVE_SLOT_FIRST;
       slot < MMX_SABER_WAVE_SLOT_END; slot += MMX_SABER_WAVE_SLOT_BYTES)
    retire_slot(target, slot);
}

void MmxSaberWaveRuntimeReset(uint8_t *ram) {
  MmxSaberWaveRuntimeRetireAll(ram);
  observed_stage_valid = false;
  collision_reset();
}

bool MmxSaberWaveRuntimeOwns(const uint8_t *ram, unsigned slot) {
  return live(ram, slot);
}

bool MmxSaberWaveRuntimeActive(const uint8_t *ram) {
  const uint8_t *target = ram ? ram : runtime_ram;
  if (!target) return false;
  for (unsigned slot = MMX_SABER_WAVE_SLOT_FIRST;
       slot < MMX_SABER_WAVE_SLOT_END; slot += MMX_SABER_WAVE_SLOT_BYTES)
    if (live(target, slot)) return true;
  return false;
}

unsigned MmxSaberWaveRuntimeLiveWaves(MmxSaberWaveRuntimeLiveWave *out,
                                      unsigned max) {
  unsigned count = 0;
  if (!runtime_ram || !max) return 0;
  for (unsigned slot = MMX_SABER_WAVE_SLOT_FIRST;
       slot < MMX_SABER_WAVE_SLOT_END && count < max;
       slot += MMX_SABER_WAVE_SLOT_BYTES) {
    if (!live(runtime_ram, slot)) continue;
    if (out) {
      out[count].world_x = (int16_t)word(runtime_ram + slot + 5);
      out[count].world_y = (int16_t)word(runtime_ram + slot + 8);
      out[count].age = runtime_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
      out[count].facing_left = (runtime_ram[slot + 0x11] & 0x40) != 0;
    }
    ++count;
  }
  return count;
}

static bool target_live(const uint8_t *ram, unsigned slot) {
  unsigned enemy;
  if (!live(ram, slot) ||
      ram[slot + MMX_SABER_WAVE_SLOT_STATE] !=
          MMX_SABER_WAVE_SLOT_STATE_CUTTING)
    return false;
  enemy = word(ram + slot + MMX_SABER_WAVE_SLOT_TARGET);
  return target_slot_valid(enemy) && ram[enemy] &&
      ram[enemy + 10] == target_kind(ram, slot);
}

static bool collision_due(const uint8_t *ram, unsigned slot) {
  return live(ram, slot) &&
      word(ram + slot + 0x20) == MMX_SABER_WAVE_COLLISION_POINTER;
}

unsigned MmxSaberWaveRuntimeWeaponTick(uint8_t *ram, unsigned slot,
                                       unsigned value) {
  uint8_t age;
  uint8_t wave_state;
  uint8_t *target = target_ram(ram);
  (void)value;
  if (!target || !live(target, slot)) return value;
  if (!collision.ready ||
      target_stage(target, slot) != target[MMX_SABER_WAVE_STAGE]) {
    retire_slot(target, slot);
    return 0;
  }
  wave_state = target[slot + MMX_SABER_WAVE_SLOT_STATE];
  if (wave_state != MMX_SABER_WAVE_SLOT_STATE_TRAVEL &&
      wave_state != MMX_SABER_WAVE_SLOT_STATE_CUTTING) {
    retire_slot(target, slot);
    return 0;
  }
  age = target[slot + MMX_SABER_WAVE_SLOT_AGE];
  if (age >= MMX_SABER_WAVE_LIFETIME) {
    retire_slot(target, slot);
    return 0;
  }
  if (target[slot + MMX_SABER_WAVE_SLOT_BIRTH]) {
    target[slot + MMX_SABER_WAVE_SLOT_BIRTH] = 0;
    if (offscreen(target, slot)) retire_slot(target, slot);
    return 0;
  }
  if (wave_state == MMX_SABER_WAVE_SLOT_STATE_CUTTING) {
    unsigned enemy;
    uint8_t countdown;
    if (target[slot + MMX_SABER_WAVE_SLOT_PULSES] >=
            MMX_SABER_WAVE_MAX_PULSES ||
        !target_live(target, slot)) {
      retire_slot(target, slot);
      return 0;
    }
    enemy = word(target + slot + MMX_SABER_WAVE_SLOT_TARGET);
    putword(target + slot + 5, word(target + enemy + 5));
    putword(target + slot + 8, word(target + enemy + 8));
    ++age;
    target[slot + MMX_SABER_WAVE_SLOT_AGE] = age;
    if (age >= MMX_SABER_WAVE_LIFETIME) {
      retire_slot(target, slot);
      return 0;
    }
    countdown = target[slot + MMX_SABER_WAVE_SLOT_COUNTDOWN];
    if (countdown) --countdown;
    target[slot + MMX_SABER_WAVE_SLOT_COUNTDOWN] = countdown;
    putword(target + slot + 0x20,
            countdown ? 0 : MMX_SABER_WAVE_COLLISION_POINTER);
    return 0;
  }
  {
    int x = (int16_t)word(target + slot + 5);
    x += (target[slot + 0x11] & 0x40) ? MMX_SABER_WAVE_SPEED :
        -MMX_SABER_WAVE_SPEED;
    putword(target + slot + 5, (uint16_t)x);
  }
  ++age;
  target[slot + MMX_SABER_WAVE_SLOT_AGE] = age;
  if (age >= MMX_SABER_WAVE_LIFETIME || offscreen(target, slot)) {
    retire_slot(target, slot);
    return 0;
  }
  putword(target + slot + 0x20, MMX_SABER_WAVE_COLLISION_POINTER);
  return 0;
}

unsigned MmxSaberWaveRuntimeDamage(uint8_t *ram, unsigned enemy,
                                   unsigned slot, unsigned value) {
  uint8_t *target = target_ram(ram);
  uint8_t wave_state;
  uint8_t pulses;
  unsigned target_enemy;
  if (!target || !live(target, slot)) return value;
  if (value & 128) {
    /* Old src/mmx_saber.c:2450-2452 retires on reflection/special response. */
    retire_slot(target, slot);
    return value;
  }
  if (!collision.ready ||
      target_stage(target, slot) != target[MMX_SABER_WAVE_STAGE]) {
    retire_slot(target, slot);
    return value;
  }
  wave_state = target[slot + MMX_SABER_WAVE_SLOT_STATE];
  if (wave_state != MMX_SABER_WAVE_SLOT_STATE_TRAVEL &&
      wave_state != MMX_SABER_WAVE_SLOT_STATE_CUTTING) {
    retire_slot(target, slot);
    return value;
  }
  if (!target_slot_valid(enemy) || !value) {
    /* Native immunity is deliberately a no-op: the wave keeps travelling. */
    return value;
  }
  if (wave_state == MMX_SABER_WAVE_SLOT_STATE_TRAVEL) {
    if (!target[enemy]) return value;
    putword(target + slot + MMX_SABER_WAVE_SLOT_TARGET, enemy);
    target[slot + MMX_SABER_WAVE_SLOT_STATE] =
        MMX_SABER_WAVE_SLOT_STATE_CUTTING;
    target[slot + MMX_SABER_WAVE_SLOT_PULSES] = 1;
    target[slot + MMX_SABER_WAVE_SLOT_COUNTDOWN] =
        MMX_SABER_WAVE_PULSE_FRAMES;
    target[slot + MMX_SABER_WAVE_SLOT_TARGET_KIND] = target[enemy + 10];
    target[slot + MMX_SABER_WAVE_SLOT_TARGET_STATE] = target[enemy + 1];
    target[slot + MMX_SABER_WAVE_SLOT_STAGE] = target[MMX_SABER_WAVE_STAGE];
    target[slot + MMX_SABER_WAVE_SLOT_TARGET_SUBSTATE] = target[enemy + 2];
    target[slot + MMX_SABER_WAVE_SLOT_TARGET_FLAGS] = target[enemy + 3];
    target[slot + MMX_SABER_WAVE_ID_KIND] = target[enemy + 10];
    target[slot + MMX_SABER_WAVE_ID_STAGE] = target[MMX_SABER_WAVE_STAGE];
    target[slot + MMX_SABER_WAVE_ID_MARKER] =
        MMX_SABER_WAVE_ID_MARKER_VALUE;
    putword(target + slot + 0x20, 0);
    return wave_tuned_damage(target, enemy);
  }
  if (target[slot + MMX_SABER_WAVE_SLOT_STATE] !=
      MMX_SABER_WAVE_SLOT_STATE_CUTTING) {
    retire_slot(target, slot);
    return value;
  }
  target_enemy = word(target + slot + MMX_SABER_WAVE_SLOT_TARGET);
  if (enemy != target_enemy) return 0;
  if (!target_live(target, slot)) {
    retire_slot(target, slot);
    return 0;
  }
  if (!collision_due(target, slot)) return 0;
  pulses = target[slot + MMX_SABER_WAVE_SLOT_PULSES];
  if (!pulses || pulses >= MMX_SABER_WAVE_MAX_PULSES) {
    retire_slot(target, slot);
    return 0;
  }
  ++pulses;
  target[slot + MMX_SABER_WAVE_SLOT_PULSES] = pulses;
  if (pulses >= MMX_SABER_WAVE_MAX_PULSES) {
    /* Retire after returning the third native override. */
    retire_slot(target, slot);
  } else {
    target[slot + MMX_SABER_WAVE_SLOT_COUNTDOWN] =
        MMX_SABER_WAVE_PULSE_FRAMES;
    putword(target + slot + 0x20, 0);
  }
  return wave_tuned_damage(target, enemy);
}

unsigned MmxSaberWaveRuntimeHitbox(const uint8_t *ram, unsigned enemy,
                                   unsigned slot, unsigned value) {
  uint8_t wave_state;
  if (!MmxSaberWaveRuntimeOwns(ram, slot)) return value;
  if (target_stage(ram, slot) != ram[MMX_SABER_WAVE_STAGE])
    return 0;
  wave_state = ram[slot + MMX_SABER_WAVE_SLOT_STATE];
  if (wave_state != MMX_SABER_WAVE_SLOT_STATE_TRAVEL &&
      wave_state != MMX_SABER_WAVE_SLOT_STATE_CUTTING)
    return 0;
  if (!collision_due(ram, slot)) return 0;
  if (wave_state == MMX_SABER_WAVE_SLOT_STATE_CUTTING) {
    if (enemy != word(ram + slot + MMX_SABER_WAVE_SLOT_TARGET) ||
        !target_live(ram, slot)) return 0;
  }
  return value;
}

void MmxSaberWaveRuntimeSetCollisionRecord(const uint8_t *record,
                                            size_t size) {
  if (!record || size != MMX_SABER_WAVE_COLLISION_RECORD_BYTES) {
    collision_record_valid = false;
    return;
  }
  memcpy(collision_record, record, sizeof(collision_record));
  collision_record_valid = true;
}

void MmxSaberWaveRuntimeCollisionRom(uint8_t *rom, size_t size) {
  static const uint8_t empty[MMX_SABER_WAVE_COLLISION_RECORD_BYTES] =
      {0xff, 0xff, 0xff, 0xff};
  collision.ready = false;
  if (collision.installed &&
      (collision.rom != rom || !rom ||
       size < MMX_SABER_WAVE_COLLISION_ROM_OFFSET +
           MMX_SABER_WAVE_COLLISION_RECORD_BYTES)) {
    collision_reset();
  }
  if (!rom || size < MMX_SABER_WAVE_COLLISION_ROM_OFFSET +
          MMX_SABER_WAVE_COLLISION_RECORD_BYTES ||
      !collision_record_valid) return;
  if (collision.installed) {
    if (memcmp(rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
               collision.bytes, sizeof(collision.bytes)) == 0) {
      /* A launched wave is world-owned. Keep its collision record valid while
       * Zero is offscreen during exchange; lifecycle reset/disable owns the
       * explicit restoration path. */
      collision.ready = true;
      return;
    }
    /* A third party changed the record. Never overwrite unknown bytes. */
    collision.installed = false;
    collision.rom = NULL;
    return;
  }
  if (!MmxZeroActive() ||
      memcmp(rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET, empty,
             sizeof(empty)) != 0)
    return;
  memcpy(rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
         collision_record, sizeof(collision_record));
  memcpy(collision.bytes, collision_record, sizeof(collision.bytes));
  collision.rom = rom;
  collision.installed = true;
  collision.ready = true;
}

typedef struct MmxSaberWaveRuntimeSavedState {
  uint8_t next_generation;
  uint8_t observed_stage;
  bool observed_stage_valid;
} MmxSaberWaveRuntimeSavedState;

size_t MmxSaberWaveRuntimeStateSize(void) {
  return sizeof(MmxSaberWaveRuntimeSavedState);
}

void MmxSaberWaveRuntimeStateSave(uint8_t *out) {
  MmxSaberWaveRuntimeSavedState saved;
  memset(&saved, 0, sizeof(saved));
  saved.next_generation = next_generation;
  saved.observed_stage = observed_stage;
  saved.observed_stage_valid = observed_stage_valid;
  memcpy(out, &saved, sizeof(saved));
}

void MmxSaberWaveRuntimeStateLoad(const uint8_t *in) {
  MmxSaberWaveRuntimeSavedState saved;
  memcpy(&saved, in, sizeof(saved));
  next_generation = saved.next_generation;
  observed_stage = saved.observed_stage;
  observed_stage_valid = saved.observed_stage_valid;
}
