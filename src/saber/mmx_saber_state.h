#pragma once

#include <stddef.h>
#include <stdint.h>

/* Savestate/rollback snapshots of each Saber module's host-side gameplay
 * state. Saber's projectiles and waves live in guest RAM, which the save
 * already carries; these are the host fields that decide what they do next.
 * Configuration (tuning, ROM-derived collision records, loaded assets) is
 * not state and is never restored from a save. Each Save writes exactly
 * Size() bytes and each Load reads exactly that many. */
size_t MmxSaberAttackStateSize(void);
void MmxSaberAttackStateSave(uint8_t *out);
void MmxSaberAttackStateLoad(const uint8_t *in);

size_t MmxSaberComboStateSize(void);
void MmxSaberComboStateSave(uint8_t *out);
void MmxSaberComboStateLoad(const uint8_t *in);

size_t MmxSaberPriorityStateSize(void);
void MmxSaberPriorityStateSave(uint8_t *out);
void MmxSaberPriorityStateLoad(const uint8_t *in);

size_t MmxSaberWaveRuntimeStateSize(void);
void MmxSaberWaveRuntimeStateSave(uint8_t *out);
void MmxSaberWaveRuntimeStateLoad(const uint8_t *in);
