#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MmxSaberTuningDamageKind {
  MMX_SABER_TUNING_DAMAGE_SLASH1 = 0,
  MMX_SABER_TUNING_DAMAGE_SLASH2,
  MMX_SABER_TUNING_DAMAGE_SLASH3,
  MMX_SABER_TUNING_DAMAGE_X3_FINISHER,
  MMX_SABER_TUNING_DAMAGE_AIR,
  MMX_SABER_TUNING_DAMAGE_WALL,
  MMX_SABER_TUNING_DAMAGE_DASH,
  MMX_SABER_TUNING_DAMAGE_WAVE,
  MMX_SABER_TUNING_DAMAGE_COUNT
} MmxSaberTuningDamageKind;

typedef enum MmxSaberTuningPriorityKind {
  MMX_SABER_TUNING_PRIORITY_SLASH1 = 0,
  MMX_SABER_TUNING_PRIORITY_SLASH2,
  MMX_SABER_TUNING_PRIORITY_SLASH3,
  MMX_SABER_TUNING_PRIORITY_AIR,
  MMX_SABER_TUNING_PRIORITY_WALL,
  MMX_SABER_TUNING_PRIORITY_DASH,
  MMX_SABER_TUNING_PRIORITY_CHARGE_SMALL,
  MMX_SABER_TUNING_PRIORITY_CHARGE_FULL,
  MMX_SABER_TUNING_PRIORITY_MAX_SHOT1,
  MMX_SABER_TUNING_PRIORITY_MAX_SHOT2,
  MMX_SABER_TUNING_PRIORITY_X3_FINISHER,
  MMX_SABER_TUNING_PRIORITY_WAVE,
  MMX_SABER_TUNING_PRIORITY_COUNT
} MmxSaberTuningPriorityKind;

typedef struct MmxSaberTuning {
  int normal_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
  int boss_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
  int priority[MMX_SABER_TUNING_PRIORITY_COUNT];
  int priority_window_frames;
  int finisher_window_frames;
  int saber_swing_volume;
  bool show_hitboxes;
} MmxSaberTuning;

/* The reader returns true when it supplied a value for option_id. */
typedef bool (*MmxSaberTuningOptionReader)(const char *option_id,
                                           char *value, size_t value_size,
                                           void *context);

/* Read all manifest tuning options into the Saber-owned cache. A NULL reader
 * resets the cache to the manifest defaults. */
void MmxSaberTuningLoad(MmxSaberTuningOptionReader reader, void *context);

const MmxSaberTuning *MmxSaberTuningGet(void);
int MmxSaberTuningNormalDamage(MmxSaberTuningDamageKind kind);
int MmxSaberTuningBossDamage(MmxSaberTuningDamageKind kind);
int MmxSaberTuningPriority(MmxSaberTuningPriorityKind kind);
int MmxSaberTuningPriorityWindowFrames(void);
int MmxSaberTuningFinisherWindowFrames(void);
int MmxSaberTuningSaberSwingVolume(void);
bool MmxSaberTuningShowHitboxes(void);

/* Short name for call sites that already establish the Saber context. */
int MmxSaberTuningSwingVolume(void);

#ifdef __cplusplus
}
#endif
