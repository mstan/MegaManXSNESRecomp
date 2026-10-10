#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  MMX_SABER_PRIORITY_ENEMY_FIRST = 0x0e68,
  MMX_SABER_PRIORITY_ENEMY_END = 0x1228,
  MMX_SABER_PRIORITY_PROJECTILE_FIRST = 0x1228,
  MMX_SABER_PRIORITY_PROJECTILE_END = 0x1428,
  MMX_SABER_PRIORITY_SLOT_BYTES = 0x40,
  MMX_SABER_PRIORITY_ENEMY_SLOT_COUNT = 15,
  MMX_SABER_PRIORITY_PROJECTILE_SLOT_COUNT = 8
};

typedef enum MmxSaberPriorityClass {
  MMX_SABER_PRIORITY_CLASS_NONE = 0,
  MMX_SABER_PRIORITY_CLASS_SLASH1,
  MMX_SABER_PRIORITY_CLASS_SLASH2,
  MMX_SABER_PRIORITY_CLASS_SLASH3,
  MMX_SABER_PRIORITY_CLASS_AIR,
  MMX_SABER_PRIORITY_CLASS_WALL,
  MMX_SABER_PRIORITY_CLASS_DASH,
  MMX_SABER_PRIORITY_CLASS_X3_FINISHER,
  MMX_SABER_PRIORITY_CLASS_WAVE,
  MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL,
  MMX_SABER_PRIORITY_CLASS_CHARGE_FULL,
  MMX_SABER_PRIORITY_CLASS_MAX_SHOT1,
  MMX_SABER_PRIORITY_CLASS_MAX_SHOT2,
  MMX_SABER_PRIORITY_CLASS_COUNT
} MmxSaberPriorityClass;

typedef struct MmxSaberPriorityClassification {
  MmxSaberPriorityClass priority_class;
  uint8_t priority;
  /* Positive-path enemy row plus one, captured with the accepted
   * lower-priority hit; zero means that the current RAM row should be used. */
  uint8_t native_damage_row;
} MmxSaberPriorityClassification;

/* slot is the guest enemy-slot address, not the compact history index. A zero
 * slot means that no matching history record exists. */
typedef struct MmxSaberPriorityHistory {
  uint16_t slot;
  uint8_t kind;
  uint8_t generation;
  uint8_t stage;
  uint8_t priority;
  uint8_t native_damage_row;
  uint32_t frame;
} MmxSaberPriorityHistory;

/* Classify a live projectile slot. The result is false and is set to NONE/0
 * when the slot is not owned by a Saber priority class. */
bool MmxSaberPriorityClassify(const uint8_t *ram, unsigned projectile_slot,
                              MmxSaberPriorityClassification *result);

/* Collision-response seam. Exact native zero can arm a token generally. The
 * only nonzero admission is a protected positive response from Armadillo's
 * exposed row $0B for a strictly higher-priority Saber follow-up; bit-7
 * responses remain unchanged. */
unsigned MmxSaberPriorityResponse(uint8_t *ram, unsigned enemy_slot,
                                  unsigned projectile_slot, unsigned original);

/* Consume the one-shot response token at the damage seam. The classification
 * returned here is the one captured at response time, so a later projectile
 * or attack-state change cannot retarget the bypass. */
bool MmxSaberPriorityConsumePending(
    const uint8_t *ram, unsigned enemy_slot, unsigned projectile_slot,
    MmxSaberPriorityClassification *classification);

/* Configuration lookup kept public for tests and future response wiring. */
int MmxSaberPriorityTuned(MmxSaberPriorityClass priority_class);

/* Non-serialized lifecycle state. */
void MmxSaberPriorityReset(void);

/* Observe the existing player boundaries. These snapshots are the only
 * emission provenance source for native buster slots. */
void MmxSaberPriorityObservePrePlayer(const uint8_t *ram,
                                      uint8_t shot_mask, uint8_t burst);
void MmxSaberPriorityObservePlayerEnd(const uint8_t *ram,
                                      uint8_t shot_mask, uint8_t burst);
uint32_t MmxSaberPriorityCurrentFrame(void);

/* Enemy history. Record/lookup/eligible are RAM-driven so a caller can use
 * them from either the compiled collision path or the interpreter. */
bool MmxSaberPriorityHistoryRecord(
    const uint8_t *ram, unsigned enemy_slot,
    const MmxSaberPriorityClassification *candidate, uint32_t frame);
bool MmxSaberPriorityHistoryLookup(const uint8_t *ram, unsigned enemy_slot,
                                   MmxSaberPriorityHistory *history);
bool MmxSaberPriorityHistoryEligible(const uint8_t *ram, unsigned enemy_slot,
                                     unsigned candidate_priority,
                                     uint32_t frame);

/* Short aliases for response/damage owners that already establish Saber
 * context. They intentionally preserve the same argument order and rules. */
bool MmxSaberPriorityRecord(
    const uint8_t *ram, unsigned enemy_slot,
    const MmxSaberPriorityClassification *candidate, uint32_t frame);
bool MmxSaberPriorityLookup(const uint8_t *ram, unsigned enemy_slot,
                            MmxSaberPriorityHistory *history);
bool MmxSaberPriorityEligible(const uint8_t *ram, unsigned enemy_slot,
                              unsigned candidate_priority, uint32_t frame);

/* Useful for diagnostics and synthetic tests; it also synchronizes the
 * dead->live generation transition for the requested enemy slot. */
uint8_t MmxSaberPriorityEnemyGeneration(const uint8_t *ram,
                                        unsigned enemy_slot);

#ifdef __cplusplus
}
#endif
