#include "mmx_saber_attack.h"
#include "mmx_saber_state.h"

#include <stdio.h>
#include <string.h>

#include "../mmx_wide_policy.h"
#include "../mmx_zero.h"
#include "mmx_saber_sfx.h"
#include "mmx_saber_hitbox_debug.h"
#include "mmx_saber_priority.h"
#include "mmx_saber_tuning.h"
#include "mmx_saber_wave_runtime.h"

/* The native records are center offset, center offset, and half-extents.
 * Ground pointers use $FFD8 + 4*segment; air starts at $FF40, wall at
 * $FF50, and dash at $FF5C.  The four refit attack tables below are measured
 * against the rendered saber blade; the other tables remain donor records. */
static const MmxSaberBoundsSegment kGroundSlash1Bounds[] = {
    {4, 5, 7, -28, 12, 10},
    {6, 7, 28, -19, 19, 19},
    {8, 9, 36, 1, 17, 15},
    {10, 11, 36, 10, 17, 6}};

static const MmxSaberBoundsSegment kGroundSlash2Bounds[] = {
    {0, 3, 24, -5, 14, 13},
    {4, 7, 13, -5, 42, 13},
    {8, 11, -21, -5, 18, 13}};

static const MmxSaberBoundsSegment kGroundFinisherBounds[] = {
    {0, 5, -8, -20, 19, 14},
    {6, 9, 30, -9, 37, 29},
    {10, 13, 38, -4, 30, 25}};

static const MmxSaberBoundsSegment kAirSlashBounds[] = {
    {4, 5, 16, -24, 17, 12},
    {6, 7, 14, -16, 30, 19},
    {8, 9, 13, -12, 42, 23},
    {10, 11, -13, -14, 18, 11}};

static const MmxSaberBoundsSegment kWallSlashBounds[] = {
    {0, 3, 31, -10, 33, 19},
    {4, 7, 22, 0, 23, 15},
    {8, 11, 14, 1, 16, 12}};

static const MmxSaberBoundsSegment kDashSlashBounds[] = {
    {2, 5, 24, 1, 37, 17},
    {6, 9, 45, 0, 30, 17},
    {10, 11, 14, -15, 24, 6}};

/* Ported from old src/mmx_saber.c:260-400.  In particular, keep the numeric
 * timing values here independent of the ROM test's old-table oracle. */
static const MmxSaberAttack kSaberAttacks[] = {
    {
        .kind = SABER_KIND_GROUND1,
        .index = 0,
        .visual_animation = 1,
        .facing_xor = 1,
        .startup_ticks = 4,
        .active_ticks = 8,
        .recovery_ticks = 18,
        .total_ticks = 30,
        .chain_open_tick = 12,
        .chain_close_tick = 29,
        .buffer_open_tick = 4,
        .buffer_close_tick = 11,
        .next_index = 1,
        .bounds_segments = kGroundSlash1Bounds,
        .bounds_segment_count = sizeof(kGroundSlash1Bounds) /
            sizeof(kGroundSlash1Bounds[0]),
        .damage = 3,
        .bounds_pointer = MMX_SABER_ATTACK_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_GROUND2,
        .index = 1,
        .visual_animation = 2,
        .facing_xor = 1,
        .startup_ticks = 0,
        .active_ticks = 12,
        .recovery_ticks = 18,
        .total_ticks = 30,
        .chain_open_tick = 12,
        .chain_close_tick = 29,
        .buffer_open_tick = 0,
        .buffer_close_tick = 11,
        .next_index = 2,
        .bounds_segments = kGroundSlash2Bounds,
        .bounds_segment_count = sizeof(kGroundSlash2Bounds) /
            sizeof(kGroundSlash2Bounds[0]),
        .damage = 3,
        .bounds_pointer = MMX_SABER_SLASH2_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_GROUND3,
        .index = 2,
        .visual_animation = 3,
        .facing_xor = 1,
        .startup_ticks = 0,
        .active_ticks = 14,
        .recovery_ticks = 25,
        .total_ticks = 39,
        .chain_open_tick = MMX_SABER_NO_WINDOW,
        .chain_close_tick = MMX_SABER_NO_WINDOW,
        .buffer_open_tick = MMX_SABER_NO_WINDOW,
        .buffer_close_tick = MMX_SABER_NO_WINDOW,
        .next_index = MMX_SABER_NO_WINDOW,
        .bounds_segments = kGroundFinisherBounds,
        .bounds_segment_count = sizeof(kGroundFinisherBounds) /
            sizeof(kGroundFinisherBounds[0]),
        .damage = 8,
        .bounds_pointer = MMX_SABER_FINISHER_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_AIR,
        .index = 0,
        .visual_animation = 4,
        .facing_xor = 1,
        .startup_ticks = 4,
        .active_ticks = 8,
        .recovery_ticks = 6,
        .total_ticks = 18,
        .chain_open_tick = MMX_SABER_NO_WINDOW,
        .chain_close_tick = MMX_SABER_NO_WINDOW,
        .buffer_open_tick = MMX_SABER_NO_WINDOW,
        .buffer_close_tick = MMX_SABER_NO_WINDOW,
        .next_index = MMX_SABER_NO_WINDOW,
        .bounds_segments = kAirSlashBounds,
        .bounds_segment_count = sizeof(kAirSlashBounds) /
            sizeof(kAirSlashBounds[0]),
        .damage = 3,
        .bounds_pointer = MMX_SABER_AIR_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_WALL,
        .index = 0,
        .visual_animation = 5,
        .facing_xor = 1,
        .startup_ticks = 0,
        .active_ticks = 12,
        .recovery_ticks = 8,
        .total_ticks = 20,
        .chain_open_tick = MMX_SABER_NO_WINDOW,
        .chain_close_tick = MMX_SABER_NO_WINDOW,
        .buffer_open_tick = MMX_SABER_NO_WINDOW,
        .buffer_close_tick = MMX_SABER_NO_WINDOW,
        .next_index = MMX_SABER_NO_WINDOW,
        .bounds_segments = kWallSlashBounds,
        .bounds_segment_count = sizeof(kWallSlashBounds) /
            sizeof(kWallSlashBounds[0]),
        .damage = 3,
        .bounds_pointer = MMX_SABER_WALL_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_DASH,
        .index = 0,
        .visual_animation = 6,
        .facing_xor = 1,
        .startup_ticks = 2,
        .active_ticks = 10,
        .recovery_ticks = 18,
        .total_ticks = 30,
        .chain_open_tick = MMX_SABER_NO_WINDOW,
        .chain_close_tick = MMX_SABER_NO_WINDOW,
        .buffer_open_tick = MMX_SABER_NO_WINDOW,
        .buffer_close_tick = MMX_SABER_NO_WINDOW,
        .next_index = MMX_SABER_NO_WINDOW,
        .bounds_segments = kDashSlashBounds,
        .bounds_segment_count = sizeof(kDashSlashBounds) /
            sizeof(kDashSlashBounds[0]),
        .damage = 3,
        .bounds_pointer = MMX_SABER_DASH_BOUNDS_POINTER},
    {
        .kind = SABER_KIND_SABER_LAND,
        .index = 0,
        .visual_animation = 7,
        .facing_xor = 1,
        .startup_ticks = 18,
        .active_ticks = 0,
        .recovery_ticks = 0,
        .total_ticks = 18,
        .chain_open_tick = MMX_SABER_NO_WINDOW,
        .chain_close_tick = MMX_SABER_NO_WINDOW,
        .buffer_open_tick = MMX_SABER_NO_WINDOW,
        .buffer_close_tick = MMX_SABER_NO_WINDOW,
        .next_index = MMX_SABER_NO_WINDOW,
        .bounds_segments = NULL,
        .bounds_segment_count = 0,
        .damage = 0,
        .bounds_pointer = 0}};

typedef struct MmxSaberAttackState {
  MmxSaberPadKind kind;
  uint8_t index;
  MmxSaberPadPhase phase;
  uint8_t tick;
  uint8_t buffer_slot;
  uint8_t facing;
  uint8_t swing_id;
  uint8_t anim_id;
  uint8_t anim_step;
  uint16_t projectile;
  uint16_t hit_slots;
  uint8_t projectile_tag_generation;
  uint8_t projectile_swing_id;
  uint8_t cue;
  bool cue_pending;
  bool previous_grounded;
  bool previous_grounded_valid;
} MmxSaberAttackState;

static MmxSaberAttackState state;
static uint8_t projectile_generation;
static uint8_t *runtime_ram;
static bool ram_reset_pending;
static unsigned cue_count;
static bool native_observation_valid;
static MmxSaberPadKind native_attack_kind;
static MmxSaberPadPhase native_attack_phase;
static uint8_t native_action;
static bool native_grounded;

typedef struct MmxSaberCollisionWindow {
  uint8_t *rom;
  uint8_t bytes[40];
  size_t offset;
  const char *name;
  bool installed;
  bool ready;
  bool warned;
} MmxSaberCollisionWindow;

static MmxSaberCollisionWindow ground_collision = {
    NULL, {0}, 0x37fd8, "$37FD8", false, false, false};
static MmxSaberCollisionWindow air_collision = {
    NULL, {0}, 0x37f40, "$37F40", false, false, false};
static unsigned collision_warnings;
/* The native positive path reads $86:EF37 through the collision ROM. Keep
 * the same ROM view here so a priority-bypassed buster can reproduce that
 * read with the pre-i-frame enemy row. */
static uint8_t *native_collision_rom;
static size_t native_collision_rom_size;

static bool player_grounded(const uint8_t *ram);
static void collision_windows_reset(void);

static void clear_native_observation(void) {
  native_observation_valid = false;
  native_attack_kind = SABER_KIND_NONE;
  native_attack_phase = SABER_PHASE_IDLE;
  native_action = 0;
  native_grounded = false;
}

static const MmxSaberAttack *state_attack(void) {
  return MmxSaberAttackRecord(state.kind, state.index);
}

static unsigned word(const uint8_t *p) {
  return p[0] | ((unsigned)p[1] << 8);
}

static void putword(uint8_t *p, unsigned value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static bool projectile_slot_valid(unsigned d) {
  return d >= 0x1228 && d < 0x1428 && (d & 63) == 0x28;
}

static bool enemy_slot_valid(unsigned d) {
  return d >= 0xe68 && d < 0x1228 && (d & 63) == 0x28;
}

static unsigned clamp_tuned_damage(int damage) {
  if (damage < 0) return 0;
  if (damage > 32) return 32;
  return (unsigned)damage;
}

static MmxSaberTuningDamageKind attack_damage_kind(
    const MmxSaberAttack *attack) {
  if (!attack) return MMX_SABER_TUNING_DAMAGE_COUNT;
  switch (attack->kind) {
    case SABER_KIND_AIR:
      return MMX_SABER_TUNING_DAMAGE_AIR;
    case SABER_KIND_WALL:
      return MMX_SABER_TUNING_DAMAGE_WALL;
    case SABER_KIND_DASH:
      return MMX_SABER_TUNING_DAMAGE_DASH;
    case SABER_KIND_GROUND1:
    case SABER_KIND_GROUND2:
    case SABER_KIND_GROUND3:
      switch (attack->index) {
        case 0: return MMX_SABER_TUNING_DAMAGE_SLASH1;
        case 1: return MMX_SABER_TUNING_DAMAGE_SLASH2;
        default: return MMX_SABER_TUNING_DAMAGE_SLASH3;
      }
    default:
      return MMX_SABER_TUNING_DAMAGE_COUNT;
  }
}

static unsigned attack_tuned_damage(const uint8_t *ram, unsigned enemy,
                                    MmxSaberTuningDamageKind kind) {
  const bool boss = ram && enemy_slot_valid(enemy) &&
      MmxWidePolicy_IsBossEncounter(ram[enemy + 0x0a]);
  const int damage = boss ? MmxSaberTuningBossDamage(kind) :
      MmxSaberTuningNormalDamage(kind);
  return clamp_tuned_damage(damage);
}

static bool priority_buster_class(MmxSaberPriorityClass priority_class) {
  return priority_class == MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL ||
      priority_class == MMX_SABER_PRIORITY_CLASS_CHARGE_FULL ||
      priority_class == MMX_SABER_PRIORITY_CLASS_MAX_SHOT1 ||
      priority_class == MMX_SABER_PRIORITY_CLASS_MAX_SHOT2;
}

static bool priority_saber_class(MmxSaberPriorityClass priority_class) {
  return priority_class >= MMX_SABER_PRIORITY_CLASS_SLASH1 &&
      priority_class <= MMX_SABER_PRIORITY_CLASS_WAVE;
}

static bool saber_owned_damage_class(MmxSaberPriorityClass priority_class) {
  return priority_saber_class(priority_class) ||
      priority_buster_class(priority_class);
}

typedef struct MmxSaberBossProtectionRule {
  uint8_t kind;
  uint8_t timer_offset;
} MmxSaberBossProtectionRule;

/* These are Saber-owned escape rules for native reaction handlers that can
 * restore a live hit state after a priority-token damage callback. Keep the
 * table explicit: other bosses use different timers or different mechanisms.
 */
static const MmxSaberBossProtectionRule kSaberBossProtectionRules[] = {
  {0x02, 0x35}, /* Chill Penguin */
};

static const MmxSaberBossProtectionRule *saber_boss_protection_rule(
    uint8_t kind) {
  for (unsigned i = 0; i < sizeof(kSaberBossProtectionRules) /
                             sizeof(kSaberBossProtectionRules[0]); ++i) {
    if (kSaberBossProtectionRules[i].kind == kind)
      return &kSaberBossProtectionRules[i];
  }
  return NULL;
}

static void clear_boss_protection_for_lethal_saber_damage(
    uint8_t *ram, unsigned enemy,
    const MmxSaberPriorityClassification *classification, unsigned damage) {
  const MmxSaberBossProtectionRule *rule;
  const unsigned hp = ram && enemy_slot_valid(enemy) ?
      ram[enemy + 0x27] & 0x7f : 0;
  const MmxSaberPriorityClass priority_class = classification ?
      classification->priority_class : MMX_SABER_PRIORITY_CLASS_NONE;

  /* Chill Penguin's native state-4 handler gates its HP-zero -> state-$06
   * branch on +$35. The collision/damage seam can run after that handler has
   * advanced to a different action/substate, so matching one visible state is
   * incomplete. A lethal Saber-owned damage event must retire this native
   * protection timer regardless of the current substate; the table remains
   * per-kind because other bosses use different protection mechanisms. */
  rule = ram && enemy_slot_valid(enemy) ?
      saber_boss_protection_rule(ram[enemy + 0x0a]) : NULL;
  if (!ram || !classification ||
      !saber_owned_damage_class(priority_class) || !rule ||
      !enemy_slot_valid(enemy) || !ram[enemy] ||
      !ram[enemy + rule->timer_offset] || !hp || damage < hp)
    return;
  ram[enemy + rule->timer_offset] = 0;
}

static MmxSaberTuningDamageKind priority_damage_kind(
    MmxSaberPriorityClass priority_class) {
  switch (priority_class) {
    case MMX_SABER_PRIORITY_CLASS_SLASH1:
      return MMX_SABER_TUNING_DAMAGE_SLASH1;
    case MMX_SABER_PRIORITY_CLASS_SLASH2:
      return MMX_SABER_TUNING_DAMAGE_SLASH2;
    case MMX_SABER_PRIORITY_CLASS_SLASH3:
      return MMX_SABER_TUNING_DAMAGE_SLASH3;
    case MMX_SABER_PRIORITY_CLASS_AIR:
      return MMX_SABER_TUNING_DAMAGE_AIR;
    case MMX_SABER_PRIORITY_CLASS_WALL:
      return MMX_SABER_TUNING_DAMAGE_WALL;
    case MMX_SABER_PRIORITY_CLASS_DASH:
      return MMX_SABER_TUNING_DAMAGE_DASH;
    case MMX_SABER_PRIORITY_CLASS_X3_FINISHER:
      return MMX_SABER_TUNING_DAMAGE_X3_FINISHER;
    case MMX_SABER_PRIORITY_CLASS_WAVE:
      return MMX_SABER_TUNING_DAMAGE_WAVE;
    default:
      return MMX_SABER_TUNING_DAMAGE_COUNT;
  }
}

static bool native_collision_rom_byte(unsigned address, unsigned *value) {
  const unsigned native_bank = 0x86;
  size_t native_offset;

  address &= 0xffff;
  /* The native table is in the bank's LoROM window. */
  if (address < 0x8000) return false;
  native_offset = (size_t)(native_bank & 0x7f) * 0x8000u +
      (address & 0x7fff);
  if (!native_collision_rom || native_offset >= native_collision_rom_size)
    return false;
  if (value) *value = native_collision_rom[native_offset];
  return true;
}

static bool native_collision_rom_word(unsigned address, unsigned *value) {
  unsigned low, high;
  if (!native_collision_rom_byte(address, &low) ||
      !native_collision_rom_byte(address + 1, &high))
    return false;
  if (value) *value = low | (high << 8);
  return true;
}

static unsigned native_buster_damage(const uint8_t *ram, unsigned enemy,
                                     unsigned projectile,
                                     unsigned native_damage_row) {
  unsigned projectile_class;
  unsigned enemy_row;
  unsigned row_directory;
  unsigned native_y;
  unsigned damage;

  if (!ram || !projectile_slot_valid(projectile) ||
      !enemy_slot_valid(enemy) || !native_collision_rom)
    return 0;

  /* $84:9E15..$9E45 derives Y from the positive-path target row and
   * projectile class: the word directory entry at $86:EF37 + 2*(enemy+$28),
   * plus the low byte of projectile+$0A. A positive response then preserves
   * this Y through $9E58..$9E6E, where the same table address supplies
   * damage. A pending token carries the row captured by the preceding
   * accepted hit when the current i-frame row itself reads zero. $9EA9's
   * projectile+$1A load is only on the negative-response path. */
  projectile_class = ram[projectile + 0x0a];
  enemy_row = native_damage_row ? native_damage_row - 1 : ram[enemy + 0x28];
  if (!native_collision_rom_word(0xef37 + (enemy_row << 1),
                                 &row_directory))
    return 0;
  native_y = (row_directory + projectile_class) & 0xffff;
  if (!native_collision_rom_byte(0xef37 + native_y, &damage)) return 0;
  return damage;
}

static bool upstream_finisher_projectile(const uint8_t *ram,
                                         unsigned projectile) {
  const MmxZeroState zero = MmxZeroGetState();
  return ram && projectile_slot_valid(projectile) && ram[projectile] &&
      projectile == zero.projectile && zero.slash &&
      word(ram + projectile + 0x3e) == 0x5a53;
}

static bool saber_tag_family(unsigned tag) {
  return (tag & MMX_SABER_PROJECTILE_TAG_FAMILY_MASK) ==
      MMX_SABER_PROJECTILE_TAG_FAMILY;
}

static bool current_projectile_tagged(const uint8_t *ram, unsigned d) {
  return ram && projectile_slot_valid(d) && ram[d] &&
      d == state.projectile && state.projectile_tag_generation != 0 &&
      word(ram + d + 0x3e) ==
          (MMX_SABER_PROJECTILE_TAG_FAMILY |
           state.projectile_tag_generation);
}

static bool saber_projectile_owned(const uint8_t *ram, unsigned d) {
  return current_projectile_tagged(ram, d) && state.phase == SABER_PHASE_ACTIVE;
}

static void retire_projectile_slot(uint8_t *ram, unsigned d) {
  bool live;
  if (!ram || !projectile_slot_valid(d)) return;
  live = ram[d] != 0;
  memset(ram + d, 0, 64);
  if (live && ram[0xbdd]) --ram[0xbdd];
}

static void retire_all_tagged(uint8_t *ram) {
  if (!ram) return;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (saber_tag_family(word(ram + d + 0x3e))) retire_projectile_slot(ram, d);
}

static void release_current_projectile(uint8_t *ram) {
  if (ram && current_projectile_tagged(ram, state.projectile))
    retire_projectile_slot(ram, state.projectile);
  state.projectile = 0;
  state.projectile_tag_generation = 0;
  state.projectile_swing_id = 0;
  state.hit_slots = 0;
}

static bool tick_in_window(uint8_t tick, uint8_t open, uint8_t close) {
  return open != MMX_SABER_NO_WINDOW && close != MMX_SABER_NO_WINDOW &&
      tick >= open && tick <= close;
}

static uint8_t facing_for_direction(uint8_t direction, uint8_t fallback) {
  direction &= MMX_SABER_NATIVE_HORIZONTAL_BITS;
  /* Native input maps right to action bit 0 and left to action bit 1.  The
   * renderer/native facing convention is $40 for right, so a single held
   * direction can replace the lock while both/neither preserve it. */
  if (direction == 1) return 0x40;
  if (direction == 2) return 0;
  return fallback & 0x40;
}

static const MmxSaberAttack *next_ground_attack(
    const MmxSaberAttack *attack) {
  if (!attack || attack->next_index == MMX_SABER_NO_WINDOW)
    return NULL;
  return MmxSaberAttackRecord(
      (MmxSaberPadKind)(SABER_KIND_GROUND1 + attack->next_index),
      attack->next_index);
}

static uint8_t animation_step_for_tick(uint8_t animation, uint8_t tick) {
  /* The donor sidecar uses two ticks per step for animations 1, 2, 4, 5, 6,
   * and 7. Animation 3's final three steps are three ticks each. Keeping the
   * resolved step here makes the state query useful before rendering exists. */
  if (animation == 3 && tick >= 30)
    return (uint8_t)(15 + (tick - 30) / 3);
  return (uint8_t)(tick / 2);
}

static void update_animation(void) {
  state.anim_step = state.anim_id ?
      animation_step_for_tick(state.anim_id, state.tick) : 0;
}

static MmxSaberSfxAttackCue attack_cue(const MmxSaberAttack *attack) {
  if (!attack) return MMX_SABER_SFX_ATTACK_COUNT;
  switch (attack->kind) {
    case SABER_KIND_GROUND1:
      return MMX_SABER_SFX_ATTACK_GROUND_SLASH_1;
    case SABER_KIND_GROUND2:
      return MMX_SABER_SFX_ATTACK_GROUND_SLASH_2;
    case SABER_KIND_GROUND3:
      return MMX_SABER_SFX_ATTACK_GROUND_SLASH_3;
    case SABER_KIND_AIR:
      return MMX_SABER_SFX_ATTACK_AIR;
    case SABER_KIND_WALL:
      return MMX_SABER_SFX_ATTACK_WALL;
    case SABER_KIND_DASH:
      return MMX_SABER_SFX_ATTACK_DASH;
    default:
      return MMX_SABER_SFX_ATTACK_COUNT;
  }
}

static void clear_attack(void) {
  state.kind = SABER_KIND_NONE;
  state.index = 0;
  state.phase = SABER_PHASE_IDLE;
  state.tick = 0;
  state.buffer_slot = 0;
  state.facing = 0;
  state.anim_id = 0;
  state.anim_step = 0;
  state.projectile = 0;
  state.projectile_tag_generation = 0;
  state.projectile_swing_id = 0;
  state.hit_slots = 0;
  state.cue = MMX_SABER_SFX_ATTACK_COUNT;
  state.cue_pending = false;
}

static const MmxSaberBoundsSegment *bounds_segment(
    const MmxSaberAttack *attack, uint8_t tick, unsigned *index) {
  if (!attack || !attack->bounds_segments) return NULL;
  for (unsigned i = 0; i < attack->bounds_segment_count &&
                       i < MMX_SABER_MAX_ACTIVE_SEGMENTS; ++i) {
    const MmxSaberBoundsSegment *segment = attack->bounds_segments + i;
    if (tick >= segment->first_tick && tick <= segment->last_tick) {
      if (index) *index = i;
      return segment;
    }
  }
  return NULL;
}

static uint16_t bounds_pointer(const MmxSaberAttack *attack, uint8_t tick) {
  unsigned index = 0;
  if (bounds_segment(attack, tick, &index))
    return (uint16_t)(attack->bounds_pointer + index * 4);
  return attack ? attack->bounds_pointer : MMX_SABER_ATTACK_BOUNDS_POINTER;
}

static void bounds_records_for_window(uint8_t out[40], uint16_t window_pointer) {
  memset(out, 255, 40);
  for (size_t attack_index = 0;
       attack_index < sizeof(kSaberAttacks) / sizeof(kSaberAttacks[0]);
       ++attack_index) {
    const MmxSaberAttack *attack = kSaberAttacks + attack_index;
    if (attack->bounds_pointer < window_pointer ||
        attack->bounds_pointer >= window_pointer + 40) continue;
    unsigned record = (attack->bounds_pointer - window_pointer) / 4;
    for (unsigned segment_index = 0;
         segment_index < attack->bounds_segment_count &&
             segment_index < MMX_SABER_MAX_ACTIVE_SEGMENTS; ++segment_index) {
      const MmxSaberBoundsSegment *segment =
          attack->bounds_segments + segment_index;
      if (record + segment_index >= 10) continue;
      unsigned offset = (record + segment_index) * 4;
      out[offset] = (uint8_t)segment->bounds_x;
      out[offset + 1] = (uint8_t)segment->bounds_y;
      out[offset + 2] = segment->bounds_half_width;
      out[offset + 3] = segment->bounds_half_height;
    }
  }
}

static bool air_collision_attack(const MmxSaberAttack *attack) {
  return attack && (attack->kind == SABER_KIND_AIR ||
                    attack->kind == SABER_KIND_WALL ||
                    attack->kind == SABER_KIND_DASH);
}

static bool collision_ready(const MmxSaberAttack *attack) {
  return air_collision_attack(attack) ? air_collision.ready :
      (attack && attack->bounds_segments && ground_collision.ready);
}

static void collision_window_update(MmxSaberCollisionWindow *window,
                                    uint8_t *rom, size_t size,
                                    const uint8_t expected[40]) {
  uint8_t empty[40];
  if (!window || !expected) return;
  memset(empty, 255, sizeof(empty));
  window->ready = false;

  if (window->installed &&
      (window->rom != rom || !rom || size < window->offset + 40)) {
    if (window->rom &&
        !memcmp(window->rom + window->offset, window->bytes, sizeof(window->bytes)))
      memcpy(window->rom + window->offset, empty, sizeof(empty));
    else if (!window->warned) {
      fprintf(stderr,
          "MMX Saber: collision window %s was modified; attacks disabled\n",
          window->name);
      window->warned = true;
      ++collision_warnings;
    }
    window->installed = false;
    window->rom = NULL;
  }
  if (!rom || size < window->offset + 40) return;

  if (window->installed) {
    if (!memcmp(rom + window->offset, window->bytes, sizeof(window->bytes))) {
      if (MmxZeroActive()) {
        window->ready = true;
      } else {
        memcpy(rom + window->offset, empty, sizeof(empty));
        window->installed = false;
        window->rom = NULL;
        window->warned = false;
      }
      return;
    }
    window->installed = false;
    window->rom = NULL;
    if (!memcmp(rom + window->offset, empty, sizeof(empty))) {
      window->warned = false;
    } else {
      if (!window->warned) {
        fprintf(stderr,
            "MMX Saber: collision window %s was modified; attacks disabled\n",
            window->name);
        window->warned = true;
        ++collision_warnings;
      }
      return;
    }
  }

  if (!MmxZeroActive()) return;
  if (!memcmp(rom + window->offset, expected, sizeof(window->bytes))) {
    memcpy(window->bytes, expected, sizeof(window->bytes));
  } else if (!memcmp(rom + window->offset, empty, sizeof(empty))) {
    memcpy(rom + window->offset, expected, sizeof(window->bytes));
    memcpy(window->bytes, expected, sizeof(window->bytes));
  } else {
    if (!window->warned) {
      fprintf(stderr,
          "MMX Saber: collision window %s is not ours; attacks disabled\n",
          window->name);
      window->warned = true;
      ++collision_warnings;
    }
    return;
  }
  window->rom = rom;
  window->installed = true;
  window->ready = true;
}

void MmxSaberAttackCollisionRom(uint8_t *rom, size_t size) {
  uint8_t ground[40], air[40];
  native_collision_rom = rom;
  native_collision_rom_size = size;
  MmxSaberHitboxDebugSetRom(rom, size);
  bounds_records_for_window(ground, MMX_SABER_ATTACK_BOUNDS_POINTER);
  bounds_records_for_window(air, MMX_SABER_AIR_BOUNDS_POINTER);
  collision_window_update(&ground_collision, rom, size, ground);
  collision_window_update(&air_collision, rom, size, air);
  MmxSaberWaveRuntimeCollisionRom(rom, size);
}

static void collision_window_reset(MmxSaberCollisionWindow *window) {
  uint8_t empty[40];
  if (!window) return;
  memset(empty, 255, sizeof(empty));
  if (window->rom &&
      !memcmp(window->rom + window->offset, window->bytes,
              sizeof(window->bytes))) {
    memcpy(window->rom + window->offset, empty, sizeof(empty));
    window->warned = false;
  }
  window->installed = false;
  window->ready = false;
  window->rom = NULL;
}

static void collision_windows_reset(void) {
  collision_window_reset(&ground_collision);
  collision_window_reset(&air_collision);
}

const MmxSaberAttack *MmxSaberAttackRecord(MmxSaberPadKind kind,
                                            uint8_t index) {
  size_t i;
  for (i = 0; i < sizeof(kSaberAttacks) / sizeof(kSaberAttacks[0]); ++i)
    if (kSaberAttacks[i].kind == kind && kSaberAttacks[i].index == index)
      return &kSaberAttacks[i];
  return NULL;
}

const MmxSaberAttack *MmxSaberAttackTableAt(size_t id) {
  if (!id || id > sizeof(kSaberAttacks) / sizeof(kSaberAttacks[0]))
    return NULL;
  return &kSaberAttacks[id - 1];
}

size_t MmxSaberAttackTableCount(void) {
  return sizeof(kSaberAttacks) / sizeof(kSaberAttacks[0]);
}

MmxSaberPadPhase MmxSaberAttackPhaseForTick(const MmxSaberAttack *attack,
                                            uint8_t tick) {
  if (!attack) return SABER_PHASE_IDLE;
  if (tick < attack->startup_ticks) return SABER_PHASE_STARTUP;
  if (tick < (uint8_t)(attack->startup_ticks + attack->active_ticks))
    return SABER_PHASE_ACTIVE;
  return SABER_PHASE_RECOVERY;
}

void MmxSaberAttackReset(void) {
  uint8_t *target = runtime_ram;
  MmxSaberAttackExit(target, MMX_SABER_ATTACK_EXIT_CONTEXT);
  memset(&state, 0, sizeof(state));
  state.phase = SABER_PHASE_IDLE;
  state.cue = MMX_SABER_SFX_ATTACK_COUNT;
  ram_reset_pending = target == NULL;
  if (target) {
    state.previous_grounded = player_grounded(target);
    state.previous_grounded_valid = true;
  }
  collision_windows_reset();
}

void MmxSaberAttackResetRam(uint8_t *ram) {
  if (ram) runtime_ram = ram;
  MmxSaberAttackReset();
}

void MmxSaberAttackResetCueCount(void) {
  cue_count = 0;
}

void MmxSaberAttackExit(uint8_t *ram, MmxSaberAttackExitReason reason) {
  uint8_t *target = ram ? ram : runtime_ram;
  (void)reason;
  if (target) runtime_ram = target;
  retire_all_tagged(target);
  clear_attack();
  clear_native_observation();
}

static void start_attack(const MmxSaberAttack *attack, uint8_t facing) {
  uint8_t swing_id;
  if (!attack) return;
  swing_id = (uint8_t)(state.swing_id + 1);
  if (!swing_id) swing_id = 1;
  state.kind = attack->kind;
  state.index = attack->index;
  state.phase = MmxSaberAttackPhaseForTick(attack, 0);
  state.tick = 0;
  state.buffer_slot = 0;
  state.facing = facing & 0x40;
  state.swing_id = swing_id;
  state.anim_id = attack->visual_animation;
  state.hit_slots = 0;
  state.cue = attack_cue(attack);
  state.cue_pending = state.cue < MMX_SABER_SFX_ATTACK_COUNT;
  update_animation();
}

void MmxSaberAttackStep(bool saber_pressed, bool grounded, bool playable,
                        uint8_t native_facing,
                        uint8_t horizontal_direction) {
  MmxSaberAttackStepWithWallAndDash(saber_pressed, grounded, false, false,
                                    false, playable, native_facing,
                                    horizontal_direction);
}

void MmxSaberAttackStepWithWall(bool saber_pressed, bool grounded,
                                bool wall_clinging, bool playable,
                                uint8_t native_facing,
                                uint8_t horizontal_direction) {
  MmxSaberAttackStepWithWallAndDash(saber_pressed, grounded, wall_clinging,
                                    false, false, playable, native_facing,
                                    horizontal_direction);
}

void MmxSaberAttackStepWithWallAndDash(bool saber_pressed, bool grounded,
                                       bool wall_clinging, bool dash_active,
                                       bool jump_pressed, bool playable,
                                       uint8_t native_facing,
                                       uint8_t horizontal_direction) {
  const MmxSaberAttack *attack;

  if (!playable) {
    if (state.phase != SABER_PHASE_IDLE || state.projectile || state.hit_slots)
      MmxSaberAttackExit(runtime_ram, MMX_SABER_ATTACK_EXIT_CONTEXT);
    return;
  }

  if (state.phase == SABER_PHASE_IDLE) {
    if (saber_pressed) {
      const MmxSaberPadKind kind = wall_clinging ? SABER_KIND_WALL :
          (!grounded ? SABER_KIND_AIR :
           (dash_active ? SABER_KIND_DASH : SABER_KIND_GROUND1));
      const uint8_t facing = wall_clinging || kind == SABER_KIND_DASH ?
          (native_facing & 0x40) :
          facing_for_direction(horizontal_direction, native_facing);
      start_attack(MmxSaberAttackRecord(kind, 0), facing);
    }
    return;
  }

  attack = state_attack();
  if (!attack || !attack->total_ticks) {
    clear_attack();
    return;
  }

  /* Old src/mmx_saber.c:saber_dash_attack_context only admitted a dash owner
   * while grounded.  A jump is the one voluntary escape for an established
   * dash slash; its acceptance is observed in PlayerEnd after native movement.
   * Startup input is masked by the pure pad before it reaches the native
   * routine. */
  if (attack->kind == SABER_KIND_DASH) {
    if (!grounded) {
      MmxSaberAttackExit(runtime_ram, MMX_SABER_ATTACK_EXIT_CONTEXT);
      return;
    }
  }
  (void)jump_pressed;

  ++state.tick;
  if (state.tick >= attack->total_ticks) {
    MmxSaberAttackExit(runtime_ram, MMX_SABER_ATTACK_EXIT_NATURAL);
    return;
  }

  if (attack->kind == SABER_KIND_GROUND1 ||
      attack->kind == SABER_KIND_GROUND2) {
    if (saber_pressed && tick_in_window(state.tick,
                                        attack->chain_open_tick,
                                        attack->chain_close_tick)) {
      const MmxSaberAttack *next = next_ground_attack(attack);
      if (next) {
        start_attack(next,
                     facing_for_direction(horizontal_direction, state.facing));
        return;
      }
    }

    if (saber_pressed && !state.buffer_slot &&
        tick_in_window(state.tick, attack->buffer_open_tick,
                       attack->buffer_close_tick))
      /* The old state has exactly one pending entry. */
      state.buffer_slot = 1;

    if (state.buffer_slot && state.tick == attack->chain_open_tick) {
      const MmxSaberAttack *next = next_ground_attack(attack);
      /* Facing is sampled at acceptance, not when the early edge was queued. */
      state.buffer_slot = 0;
      if (next) {
        start_attack(next,
                     facing_for_direction(horizontal_direction, state.facing));
        return;
      }
    }
  }

  state.phase = MmxSaberAttackPhaseForTick(attack, state.tick);
  update_animation();
}

static uint8_t next_projectile_generation(void) {
  if (++projectile_generation == 0) ++projectile_generation;
  return projectile_generation;
}

static uint8_t projectile_facing(const MmxSaberAttack *attack) {
  /* The old records use facing_xor=1 for the right-facing donor art and
   * forward-positive collision records.  With native/render facing $40 ==
   * right, this makes a right/open wall use an unmirrored +$11 and a
   * left/open wall use a mirrored +$11. */
  bool mirror = (state.facing != 0) ^ (attack && attack->facing_xor != 0);
  return mirror ? 0x40 : 0;
}

static void anchor_projectile(uint8_t *ram, unsigned d,
                              const MmxSaberAttack *attack) {
  putword(ram + d + 5, word(ram + 0x0bad));
  putword(ram + d + 8, word(ram + 0x0bb0));
  ram[d + 0x11] = projectile_facing(attack);
  putword(ram + d + 0x20,
          collision_ready(attack) ? bounds_pointer(attack, state.tick) : 0);
  ram[d + 0x30] = 0;
}

static unsigned free_projectile(const uint8_t *ram) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (!word(ram + d)) return d;
  return 0;
}

static bool allocate_projectile(uint8_t *ram, const MmxSaberAttack *attack) {
  unsigned d = free_projectile(ram);
  uint8_t generation;
  if (!d) return false;
  generation = next_projectile_generation();
  memset(ram + d, 0, 64);
  ram[d] = 1;
  ram[d + 1] = 2;
  ram[d + 10] = 3;
  ram[d + 0x11] = projectile_facing(attack);
  putword(ram + d + 0x3e,
          MMX_SABER_PROJECTILE_TAG_FAMILY | generation);
  state.projectile = (uint16_t)d;
  state.projectile_tag_generation = generation;
  state.projectile_swing_id = state.swing_id;
  state.hit_slots = 0;
  ++ram[0x0bdd];
  anchor_projectile(ram, d, attack);
  return true;
}

void MmxSaberAttackRuntimeTick(uint8_t *ram) {
  const MmxSaberAttack *attack;
  runtime_ram = ram;
  if (!ram) return;
  attack = state_attack();
  if (state.phase != SABER_PHASE_ACTIVE || !attack ||
      !attack->bounds_segments) {
    if (state.projectile) release_current_projectile(ram);
    return;
  }
  if (state.projectile && state.projectile_swing_id != state.swing_id)
    release_current_projectile(ram);
  if (!state.projectile && !allocate_projectile(ram, attack)) {
    MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_PROJECTILE);
    return;
  }
  if (!saber_projectile_owned(ram, state.projectile)) {
    MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_PROJECTILE);
    return;
  }
  anchor_projectile(ram, state.projectile, attack);
}

static bool player_grounded(const uint8_t *ram) {
  return ram && ((ram[0x0bd3] & 4) || (ram[0x0bd4] & 4));
}

void MmxSaberAttackObservePreNative(uint8_t *ram) {
  runtime_ram = ram;
  if (!ram) {
    clear_native_observation();
    return;
  }
  if (ram_reset_pending) {
    retire_all_tagged(ram);
    ram_reset_pending = false;
  }
  if (!state.previous_grounded_valid) {
    state.previous_grounded = player_grounded(ram);
    state.previous_grounded_valid = true;
  }
  native_observation_valid = true;
  native_attack_kind = state.kind;
  native_attack_phase = state.phase;
  native_action = ram[0x0baa];
  native_grounded = player_grounded(ram);
}

static bool native_wall_clinging(const uint8_t *ram) {
  return ram && ram[0x0baa] == 0x12;
}

static uint8_t settled_wall_facing(const uint8_t *ram) {
  /* Do not fall back to stale $0BB9 here.  The old WF1 path used that fallback
   * on the contact boundary; D-OP-24 requires the native post-movement $0C11
   * publication, which is the settled open-side wall facing. */
  return ram ? (uint8_t)(ram[0x0c11] & 0x40) : 0;
}

static void emit_pending_cue(void) {
  const MmxSaberAttack *attack;
  if (!state.cue_pending) return;
  attack = state_attack();
  state.cue_pending = false;
  if (!attack || state.cue >= MMX_SABER_SFX_ATTACK_COUNT)
    return;
  MmxSaberSfxPlayForAttack((MmxSaberSfxAttackCue)state.cue);
  ++cue_count;
}

static bool cancel_phase_open(MmxSaberPadPhase phase) {
  return phase == SABER_PHASE_ACTIVE || phase == SABER_PHASE_RECOVERY;
}

static bool native_jump_accepted(const uint8_t *ram) {
  const bool grounded = player_grounded(ram);
  const unsigned action = ram ? ram[0x0baa] : 0;

  if (!ram || !native_grounded) return false;
  /* The generated native callback observes the accepted jump one native
   * subroutine before the final grounded-bit publication.  In that window
   * the native action is already one of the ordinary jump/fall actions from
   * the donor whitelist ($04/$06/$08); the settled grounded edge remains the
   * stronger observation when it is available. */
  return !grounded || action == 0x04 || action == 0x06 || action == 0x08;
}

static bool native_movement_accepted(const uint8_t *ram) {
  const bool grounded = player_grounded(ram);
  const unsigned action = ram ? ram[0x0baa] : 0;

  if (!ram || !native_observation_valid ||
      !cancel_phase_open(native_attack_phase))
    return false;

  /* These are the post-native observations used by the donor at
   * oldsaber/src/mmx_saber.c:1197-1210.  The old movement whitelist records
   * ordinary jump/fall/landing as $00/$02/$04/$06/$08/$0A/$20, dash as $14,
   * and wall cling as $12; ladder/ordinary $10 is not an ownership-loss
   * result (oldsaber/src/mmx_saber.c:1052-1065). */
  switch (native_attack_kind) {
    case SABER_KIND_GROUND1:
    case SABER_KIND_GROUND2:
    case SABER_KIND_GROUND3:
      /* Native jump acceptance is the grounded -> airborne publication. */
      if (native_jump_accepted(ram)) return true;
      /* Native dash acceptance is the transition into action $14. */
      return action == 0x14 && native_action != 0x14;

    case SABER_KIND_AIR:
      /* X1 has no accepted ordinary midair jump.  Following the old air
       * context rule, entering wall cling ($12) retires the air owner, while
       * a wall jump's resulting airborne $10 remains playable and does not
       * cancel the air slash (oldsaber/src/mmx_saber.c:1141-1145,
       * 2362-2369). */
      if (action == 0x12 && native_action != 0x12) return true;
      /* Keep parity with the old common observation for a native action-$14
       * transition if a future/native route accepts an air dash. */
      return action == 0x14 && native_action != 0x14;

    case SABER_KIND_WALL:
      /* Native wall jump/loss is the settled action transition away from
       * cling $12.  This is the old context exit, now routed through the same
       * post-native cancel path. */
      return native_action == 0x12 && action != 0x12;

    case SABER_KIND_DASH:
      /* Dash slash accepts only native jump: grounded -> airborne. */
      return native_jump_accepted(ram);

    default:
      return false;
  }
}

void MmxSaberAttackPlayerEnd(uint8_t *ram) {
  bool grounded;
  bool landed;
  bool movement_accepted;
  const MmxSaberAttack *attack;

  if (ram) runtime_ram = ram;
  if (!ram) return;

  movement_accepted = native_movement_accepted(ram);
  clear_native_observation();
  grounded = player_grounded(ram);
  landed = state.previous_grounded_valid && !state.previous_grounded && grounded;
  state.previous_grounded = grounded;
  state.previous_grounded_valid = true;

  /* Landing ends an AIR owner immediately.  The central exit retires the
   * tagged slot, clears the hit mask, and leaves native movement/pose alone. */
  if (landed && state.kind == SABER_KIND_AIR &&
      state.phase != SABER_PHASE_IDLE) {
    MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_LANDING);
    return;
  }

  if (movement_accepted && state.phase != SABER_PHASE_IDLE) {
    /* Native has already accepted the legal movement.  Retire only Saber
     * ownership; MmxZero remains the sole owner of movement and charge. */
    MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_CONTEXT);
    return;
  }

  attack = state_attack();
  if (state.phase != SABER_PHASE_IDLE && attack &&
      attack->kind == SABER_KIND_WALL) {
    /* Old src/mmx_saber.c ended a wall owner at the central post-native
     * context boundary when wall action $12 was lost (wall jump/fall-off).
     * Native owns the movement and velocity; this branch only retires Saber. */
    if (!native_wall_clinging(ram)) {
      MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_CONTEXT);
      return;
    }
    /* Native wall collision has now settled the side. Keep both donor art and
     * the live collision object on that facing for every post-native frame. */
    state.facing = settled_wall_facing(ram);
    if (saber_projectile_owned(ram, state.projectile))
      anchor_projectile(ram, state.projectile, attack);
  }

  emit_pending_cue();
}

uint8_t MmxSaberAttackFacing(void) {
  return state.facing & 0x40;
}

MmxSaberPadSaber MmxSaberAttackPadState(bool release_pending) {
  return (MmxSaberPadSaber){state.phase, state.kind,
                            false,
                            MmxZeroGetState().slash != 0,
                            release_pending};
}

MmxSaberAttackSnapshot MmxSaberAttackSnapshotGet(void) {
  return (MmxSaberAttackSnapshot){state.kind, state.index, state.phase,
                                  state.tick, state.anim_id, state.anim_step,
                                  state.facing & 0x40};
}

MmxSaberAttackSnapshot MmxSaberAttackGetSnapshot(void) {
  return MmxSaberAttackSnapshotGet();
}

unsigned MmxSaberAttackWeaponTick(uint8_t *ram, unsigned projectile,
                                  unsigned value) {
  if (MmxSaberWaveRuntimeOwns(ram, projectile))
    return MmxSaberWaveRuntimeWeaponTick(ram, projectile, value);
  if (!ram || !projectile_slot_valid(projectile) || !ram[projectile] ||
      !saber_tag_family(word(ram + projectile + 0x3e)))
    return value;
  if (saber_projectile_owned(ram, projectile)) return 0;
  retire_projectile_slot(ram, projectile);
  if (projectile == state.projectile) {
    MmxSaberAttackExit(ram, MMX_SABER_ATTACK_EXIT_PROJECTILE);
  }
  return 0;
}

static unsigned saber_attack_damage_core(uint8_t *ram, unsigned enemy,
                                          unsigned projectile, unsigned value) {
  unsigned bit;
  const MmxSaberAttack *attack;
  if (MmxSaberWaveRuntimeOwns(ram, projectile))
    return MmxSaberWaveRuntimeDamage(ram, enemy, projectile, value);
  /* The upstream X3 finisher owns the $5A53 tag and its hit mask.  The
   * extension is installed only for Saber, so this positive-path filter can
   * replace upstream's 16 without changing plain Zero. */
  if (upstream_finisher_projectile(ram, projectile)) {
    if (!enemy_slot_valid(enemy) || !value || (value & 128)) return value;
    return attack_tuned_damage(ram, enemy,
                               MMX_SABER_TUNING_DAMAGE_X3_FINISHER);
  }
  if (!saber_projectile_owned(ram, projectile) || !enemy_slot_valid(enemy) ||
      !value || (value & 128))
    return value;
  bit = 1u << ((enemy - 0xe68) / 64);
  if (state.hit_slots & bit) return 0;
  state.hit_slots |= (uint16_t)bit;
  attack = state_attack();
  return attack_tuned_damage(ram, enemy, attack_damage_kind(attack));
}

unsigned MmxSaberAttackDamage(uint8_t *ram, unsigned enemy,
                              unsigned projectile, unsigned value) {
  MmxSaberPriorityClassification classification;
  unsigned damage;

  /* A response sentinel is the only route that supplies a zero table value
   * to this callback. Consume its exact identity once and replace the zero
   * with the accepted Saber/buster damage. */
  if (MmxSaberPriorityConsumePending(ram, enemy, projectile,
                                     &classification)) {
    if (value & 128) return value;
    if (priority_buster_class(classification.priority_class)) {
      damage = native_buster_damage(ram, enemy, projectile,
                                    classification.native_damage_row);
    } else if (classification.priority_class == MMX_SABER_PRIORITY_CLASS_WAVE &&
               MmxSaberWaveRuntimeOwns(ram, projectile)) {
      /* The native row supplied zero, but the wave owner needs a positive
       * value to enter its ordinary first-contact/pulse state machine. */
      damage = MmxSaberWaveRuntimeDamage(ram, enemy, projectile, 1);
    } else {
      damage = attack_tuned_damage(
          ram, enemy, priority_damage_kind(classification.priority_class));
    }
    if (enemy_slot_valid(enemy))
      MmxSaberPriorityRecord(ram, enemy, &classification,
                             MmxSaberPriorityCurrentFrame());
    /* The token is admitted only for the exposed Armadillo row. Keep this
     * second gate beside the restore bypass so armor cannot clear +$38 even
     * if a future response-table value is positive without bit 7. */
    if (damage > 0 && enemy_slot_valid(enemy) && ram[enemy] != 0 &&
        (ram[enemy + 0x27] & 0x7f) != 0 && ram[enemy + 0x0a] == 0x14 &&
        ram[enemy + 0x28] == 0x0b &&
        ram[enemy + 0x30] == 0)
      ram[enemy + 0x38] = 0;
    clear_boss_protection_for_lethal_saber_damage(ram, enemy, &classification,
                                                  damage);
    return damage;
  }

  const bool classified = MmxSaberPriorityClassify(
      ram, projectile, &classification);
  damage = saber_attack_damage_core(ram, enemy, projectile, value);
  if (value && !(value & 128) && classified)
    clear_boss_protection_for_lethal_saber_damage(ram, enemy, &classification,
                                                  damage);
  if (value && !(value & 128) && classified)
    MmxSaberPriorityRecord(ram, enemy, &classification,
                           MmxSaberPriorityCurrentFrame());
  return damage;
}

unsigned MmxSaberAttackHitbox(const uint8_t *ram, unsigned enemy,
                              unsigned projectile, unsigned value) {
  if (MmxSaberWaveRuntimeOwns(ram, projectile))
    return MmxSaberWaveRuntimeHitbox(ram, enemy, projectile, value);
  if (saber_projectile_owned(ram, projectile) && enemy_slot_valid(enemy) &&
      (state.hit_slots & (1u << ((enemy - 0xe68) / 64))))
    return 0;
  return value;
}

uint16_t MmxSaberAttackHitSlots(void) {
  return state.hit_slots;
}

unsigned MmxSaberAttackCollisionWarningCount(void) {
  return collision_warnings;
}

unsigned MmxSaberAttackCueCount(void) {
  return cue_count;
}

typedef struct MmxSaberAttackSavedState {
  MmxSaberAttackState state;
  uint8_t projectile_generation;
  bool ram_reset_pending;
  unsigned cue_count;
  bool native_observation_valid;
  MmxSaberPadKind native_attack_kind;
  MmxSaberPadPhase native_attack_phase;
  uint8_t native_action;
  bool native_grounded;
} MmxSaberAttackSavedState;

size_t MmxSaberAttackStateSize(void) {
  return sizeof(MmxSaberAttackSavedState);
}

void MmxSaberAttackStateSave(uint8_t *out) {
  MmxSaberAttackSavedState saved;
  memset(&saved, 0, sizeof(saved));
  saved.state = state;
  saved.projectile_generation = projectile_generation;
  saved.ram_reset_pending = ram_reset_pending;
  saved.cue_count = cue_count;
  saved.native_observation_valid = native_observation_valid;
  saved.native_attack_kind = native_attack_kind;
  saved.native_attack_phase = native_attack_phase;
  saved.native_action = native_action;
  saved.native_grounded = native_grounded;
  memcpy(out, &saved, sizeof(saved));
}

void MmxSaberAttackStateLoad(const uint8_t *in) {
  MmxSaberAttackSavedState saved;
  memcpy(&saved, in, sizeof(saved));
  state = saved.state;
  projectile_generation = saved.projectile_generation;
  ram_reset_pending = saved.ram_reset_pending;
  cue_count = saved.cue_count;
  native_observation_valid = saved.native_observation_valid;
  native_attack_kind = saved.native_attack_kind;
  native_attack_phase = saved.native_attack_phase;
  native_action = saved.native_action;
  native_grounded = saved.native_grounded;
}
