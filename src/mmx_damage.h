#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Multipliers use hundredths: 50 = 0.5x, 100 = 1x, 200 = 2x.
 * Configuration is fixed for a session; the mod identity carries it for netplay. */
enum { MMX_DAMAGE_MIN = 10, MMX_DAMAGE_NORMAL = 100, MMX_DAMAGE_MAX = 1000 };
unsigned MmxDamageParseMultiplier(const char *value);
void MmxDamageSetDealt(unsigned hundredths);
void MmxDamageSetReceived(unsigned hundredths);
void MmxDamageReset(void);
typedef struct MmxDamageEnemy {
  uint8_t fraction, kind, hp, active;
} MmxDamageEnemy;
typedef struct MmxDamageState {
  MmxDamageEnemy enemies[15];
  uint8_t received[2], hp[2], hp_valid, stage, stage_valid;
} MmxDamageState;
void MmxDamageClearState(void);
void MmxDamageFrame(const uint8_t *ram);
MmxDamageState MmxDamageGetState(void);
bool MmxDamageValidState(const MmxDamageState *state);
void MmxDamageSetState(const MmxDamageState *state);
/* Ghost world hits are rolled back, while actual partner hurt is retained. */
void MmxDamageRestoreWorld(const MmxDamageState *state);
bool MmxDamageDealtEnabled(void);
unsigned MmxDamageDealt(const uint8_t *ram, unsigned enemy, unsigned damage);
unsigned MmxDamageReceived(const uint8_t *ram, unsigned damage, bool lethal);
