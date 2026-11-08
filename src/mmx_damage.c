#include "mmx_damage.h"
#include "mmx_coop.h"

static unsigned dealt = MMX_DAMAGE_NORMAL, received = MMX_DAMAGE_NORMAL;

static unsigned bound(unsigned value) {
  return value < MMX_DAMAGE_MIN ? MMX_DAMAGE_MIN :
      value > MMX_DAMAGE_MAX ? MMX_DAMAGE_MAX : value;
}

unsigned MmxDamageParseMultiplier(const char *value) {
  /* Locale-independent fixed point; reject signs, exponents, NaN, infinity,
   * trailing text and excessive precision rather than converting to float. */
  if (!value || *value < '0' || *value > '9') return MMX_DAMAGE_NORMAL;
  unsigned whole = 0, fraction = 0, digits = 0;
  while (*value >= '0' && *value <= '9') {
    if (whole > 100) return MMX_DAMAGE_NORMAL;
    whole = whole * 10 + (unsigned)(*value++ - '0');
  }
  if (*value == '.') {
    ++value;
    while (*value >= '0' && *value <= '9') {
      if (++digits > 2) return MMX_DAMAGE_NORMAL;
      fraction = fraction * 10 + (unsigned)(*value++ - '0');
    }
    if (!digits) return MMX_DAMAGE_NORMAL;
  }
  if (*value || whole > 10) return MMX_DAMAGE_NORMAL;
  if (digits == 1) fraction *= 10;
  return bound(whole * 100 + fraction);
}

void MmxDamageSetDealt(unsigned value) { dealt = bound(value); }
void MmxDamageSetReceived(unsigned value) { received = bound(value); }
void MmxDamageReset(void) { dealt = received = MMX_DAMAGE_NORMAL; }
bool MmxDamageDealtEnabled(void) { return dealt != MMX_DAMAGE_NORMAL; }

static unsigned scale(unsigned damage, unsigned multiplier) {
  if (!damage || multiplier == MMX_DAMAGE_NORMAL) return damage;
  /* HP uses seven bits. Bound the input before multiplying and round half up.
   * A positive hit stays positive; immunity and suppressed repeat hits stay 0. */
  if (damage > 127) damage = 127;
  unsigned result = (damage * multiplier + 50) / 100;
  return result < 1 ? 1 : result > 127 ? 127 : result;
}

unsigned MmxDamageDealt(unsigned damage) {
  return dealt != MMX_DAMAGE_NORMAL && MmxCoopActorIsX() ? scale(damage, dealt) : damage;
}
unsigned MmxDamageReceived(unsigned damage, bool lethal) {
  /* Spikes, pits and crushers retain their native fatal result, including
   * the armor-reduced operand. Learner Mode's zero damage remains zero. */
  return lethal ? damage : scale(damage, received);
}
