#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Simulation state is fixed-width and pointer-free. Art is a local ROM cache;
 * none of it participates in rollback or a native in-game password. */
typedef struct MmxArmorState {
  uint16_t anim_offset, charge, recovery_ticks, barrier_ticks;
  uint16_t energy, health_before, shot_mask, reserved_word;
  uint8_t anim_timer, anim_pose, anim_flags, anim_valid;
  uint8_t gold, dash_used, dash_ticks, dash_up;
  uint8_t dash_facing, dash_jump, stage, valid;
  uint8_t stored_shot, fire_held, barrier_level, barrier_delay;
  uint8_t tracer, utility_held, reserved[2];
} MmxArmorState;

bool MmxArmorLoad(const char *path, unsigned game);
void MmxArmorDisable(void);
bool MmxArmorEnabled(void);
unsigned MmxArmorGame(void);
MmxArmorState MmxArmorGetState(void);
bool MmxArmorValidState(const MmxArmorState *value);
void MmxArmorSetState(MmxArmorState value);
void MmxArmorResetState(void);
unsigned MmxArmorUpgradeBits(unsigned pc, unsigned original);
void MmxArmorPrePlayer(uint8_t *ram);
void MmxArmorPlayerEnd(uint8_t *ram);
void MmxArmorPlayerMotion(uint8_t *ram, unsigned object);
void MmxArmorAnimationStart(unsigned object, unsigned sequence);
void MmxArmorAnimationAdvance(unsigned object);
/* Called only by the original secret capsule's award instruction. */
void MmxArmorHadoukenAward(const uint8_t *ram);
/* The original positive player-damage path has already rejected immunity,
 * contact knockback and instant-death cases. `object` is the attacker. */
void MmxArmorTakeDamage(uint8_t *ram, unsigned object);

enum { MMX_ARMOR_CANVAS = 128 };
/* The caller owns pixels/palette so both co-op seats have stable snapshots.
 * Acquired parts retain their real X1 inventory bits. */
bool MmxArmorCompose(const uint8_t *ram, const MmxArmorState *snapshot, bool is_x, bool menu,
                     uint8_t pixels[128*128], uint16_t palette[128]);
