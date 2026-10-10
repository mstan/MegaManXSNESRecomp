#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mmx_saber_input.h"

enum {
  MMX_SABER_NO_WINDOW = 0xff,
  MMX_SABER_MAX_ACTIVE_SEGMENTS = 4,
  MMX_SABER_PROJECTILE_TAG_FAMILY = 0x5300,
  MMX_SABER_PROJECTILE_TAG_FAMILY_MASK = 0xff00,
  MMX_SABER_ATTACK_BOUNDS_POINTER = 0xffd8,
  MMX_SABER_SLASH2_BOUNDS_POINTER = 0xffe8,
  MMX_SABER_FINISHER_BOUNDS_POINTER = 0xfff4,
  MMX_SABER_AIR_BOUNDS_POINTER = 0xff40,
  MMX_SABER_WALL_BOUNDS_POINTER = 0xff50,
  MMX_SABER_DASH_BOUNDS_POINTER = 0xff5c
};

typedef struct MmxSaberBoundsSegment {
  uint8_t first_tick;
  uint8_t last_tick;
  int8_t bounds_x;
  int8_t bounds_y;
  uint8_t bounds_half_width;
  uint8_t bounds_half_height;
} MmxSaberBoundsSegment;

/* This is the ported donor table.  The records are ordered by donor
 * animation/attack id: ground 1/2/3, air, wall, dash, and SaberLand. */
typedef struct MmxSaberAttack {
  MmxSaberPadKind kind;
  uint8_t index;
  uint8_t visual_animation;
  uint8_t facing_xor;
  uint8_t startup_ticks;
  uint8_t active_ticks;
  uint8_t recovery_ticks;
  uint8_t total_ticks;
  uint8_t chain_open_tick;
  uint8_t chain_close_tick;
  uint8_t buffer_open_tick;
  uint8_t buffer_close_tick;
  uint8_t next_index;
  const MmxSaberBoundsSegment *bounds_segments;
  uint8_t bounds_segment_count;
  uint8_t damage;
  uint16_t bounds_pointer;
} MmxSaberAttack;

typedef struct MmxSaberAttackSnapshot {
  MmxSaberPadKind kind;
  uint8_t index;
  MmxSaberPadPhase phase;
  uint8_t tick;
  uint8_t anim_id;
  uint8_t anim_step;
  /* Native/render facing: $40 is right/open-side on a left wall.  The
   * donor record's facing_xor is applied separately by the renderer and by
   * the native collision mirror. */
  uint8_t facing;
} MmxSaberAttackSnapshot;

typedef enum MmxSaberAttackExitReason {
  MMX_SABER_ATTACK_EXIT_NATURAL,
  MMX_SABER_ATTACK_EXIT_HURT,
  MMX_SABER_ATTACK_EXIT_DEATH,
  MMX_SABER_ATTACK_EXIT_CONTEXT,
  MMX_SABER_ATTACK_EXIT_PROJECTILE,
  MMX_SABER_ATTACK_EXIT_LANDING
} MmxSaberAttackExitReason;

const MmxSaberAttack *MmxSaberAttackRecord(MmxSaberPadKind kind,
                                            uint8_t index);
const MmxSaberAttack *MmxSaberAttackTableAt(size_t id);
size_t MmxSaberAttackTableCount(void);
MmxSaberPadPhase MmxSaberAttackPhaseForTick(const MmxSaberAttack *attack,
                                            uint8_t tick);

void MmxSaberAttackReset(void);
void MmxSaberAttackResetRam(uint8_t *ram);
void MmxSaberAttackResetCueCount(void);
void MmxSaberAttackExit(uint8_t *ram, MmxSaberAttackExitReason reason);

/* Advance one pre-player frame.  A press is the physical Y edge; it is not
 * derived from the current attack phase, so a held Y cannot create another
 * start.  Native-facing is the current $0C11/$0BB9 facing convention;
 * horizontal_direction uses native direction bits (right=1, left=2). */
void MmxSaberAttackStep(bool saber_pressed, bool grounded, bool playable,
                        uint8_t native_facing,
                        uint8_t horizontal_direction);
/* Wall-aware form used by the frame bridge.  wall_clinging is the old native
 * wall action ($0BAA == $12), sampled before the native player routine; the
 * wall owner samples settled $0C11 in MmxSaberAttackPlayerEnd instead. */
void MmxSaberAttackStepWithWall(bool saber_pressed, bool grounded,
                                bool wall_clinging, bool playable,
                                uint8_t native_facing,
                                uint8_t horizontal_direction);
/* Full native-context form.  dash_active is the old native action gate
 * ($0BAA == $14); jump_pressed is retained for the frame-bridge ABI but
 * cancellation is decided from the post-native observation below.
 * Priority is wall > air > dash > ground. */
void MmxSaberAttackStepWithWallAndDash(bool saber_pressed, bool grounded,
                                       bool wall_clinging, bool dash_active,
                                       bool jump_pressed, bool playable,
                                       uint8_t native_facing,
                                       uint8_t horizontal_direction);

/* Snapshot the native movement publication immediately before the native
 * player routine.  PlayerEnd compares this snapshot with the settled native
 * action/ground state; a button edge alone is never a Saber cancel. */
void MmxSaberAttackObservePreNative(uint8_t *ram);

/* Locked native/render facing for the currently published swing. */
uint8_t MmxSaberAttackFacing(void);

MmxSaberPadSaber MmxSaberAttackPadState(bool release_pending);
MmxSaberAttackSnapshot MmxSaberAttackSnapshotGet(void);
/* Alias kept concise for callers that treat the snapshot as the query API. */
MmxSaberAttackSnapshot MmxSaberAttackGetSnapshot(void);

/* Native projectile/collision bridge owned entirely by Saber. */
void MmxSaberAttackRuntimeTick(uint8_t *ram);
void MmxSaberAttackPlayerEnd(uint8_t *ram);
void MmxSaberAttackCollisionRom(uint8_t *rom, size_t size);
unsigned MmxSaberAttackWeaponTick(uint8_t *ram, unsigned projectile,
                                  unsigned value);
unsigned MmxSaberAttackDamage(uint8_t *ram, unsigned enemy,
                              unsigned projectile, unsigned value);
unsigned MmxSaberAttackHitbox(const uint8_t *ram, unsigned enemy,
                              unsigned projectile, unsigned value);
uint16_t MmxSaberAttackHitSlots(void);
unsigned MmxSaberAttackCollisionWarningCount(void);
unsigned MmxSaberAttackCueCount(void);
