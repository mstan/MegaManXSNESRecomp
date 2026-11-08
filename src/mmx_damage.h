#pragma once
#include <stdbool.h>

/* Multipliers use hundredths: 50 = 0.5x, 100 = 1x, 200 = 2x.
 * Configuration is fixed for a session; the mod identity carries it for netplay. */
enum { MMX_DAMAGE_MIN = 10, MMX_DAMAGE_NORMAL = 100, MMX_DAMAGE_MAX = 1000 };
unsigned MmxDamageParseMultiplier(const char *value);
void MmxDamageSetDealt(unsigned hundredths);
void MmxDamageSetReceived(unsigned hundredths);
void MmxDamageReset(void);
bool MmxDamageDealtEnabled(void);
unsigned MmxDamageDealt(unsigned damage);
unsigned MmxDamageReceived(unsigned damage, bool lethal);
