#include "mmx_wide_policy.h"

bool MmxWidePolicy_IsBossDoorBody(const uint16_t words[3][4], int row_index) {
  if (!words || (unsigned)row_index >= 3)
    return false;

  enum { kHFlip = 0x4000, kVFlip = 0x8000 };
  const uint16_t *top = words[0];
  const uint16_t *middle = words[1];
  const uint16_t *bottom = words[2];

  return top[1] == (uint16_t)(top[0] ^ kHFlip) &&
         top[3] == (uint16_t)(top[2] ^ kHFlip) &&
         middle[1] == (uint16_t)(middle[0] ^ kHFlip) &&
         middle[2] == (uint16_t)(middle[0] ^ kVFlip) &&
         middle[3] == (uint16_t)(middle[0] ^ kHFlip ^ kVFlip) &&
         bottom[0] == (uint16_t)(top[2] ^ kVFlip) &&
         bottom[1] == (uint16_t)(top[3] ^ kVFlip) &&
         bottom[2] == (uint16_t)(top[0] ^ kVFlip) &&
         bottom[3] == (uint16_t)(top[1] ^ kVFlip) &&
         (top[0] & 0x03ff) != (top[2] & 0x03ff) &&
         (middle[0] & 0x03ff) != (top[0] & 0x03ff) &&
         (middle[0] & 0x03ff) != (top[2] & 0x03ff);
}

uint16_t MmxWidePolicy_BeginWideSpawnPass(MmxWideSpawnCursor *cursor,
                                          uint16_t native_cursor) {
  if (!cursor)
    return native_cursor;
  if (!cursor->valid) {
    cursor->wide = native_cursor;
    cursor->valid = true;
  }
  return cursor->wide;
}

void MmxWidePolicy_EndWideSpawnPass(MmxWideSpawnCursor *cursor,
                                    uint16_t wide_cursor) {
  if (!cursor)
    return;
  cursor->wide = wide_cursor;
  cursor->valid = true;
}

bool MmxWidePolicy_SpawnRecordAllowed(uint8_t stage, uint8_t kind,
                                      uint8_t object_id, bool native_pass) {
  kind &= 0x0f;

  /* Highway's moving traffic is kind 1 presentation work. It is allowed in
   * both passes: the wide pass makes it enter naturally, while the native pass
   * lets legacy saves catch up and the guest live flag keeps it idempotent. */
  if (stage == 0x00 && kind == 1 && object_id == 0x21)
    return true;

  /* Spark Mandrill's Thunder Slimer mid-boss controller is authored as kind
   * 3 even though it is an encounter trigger, not an ordinary margin enemy.
   * Spawning it early lets it tear itself down before the arena boundary and
   * the native pass then refuses it, leaving the barrier permanently closed. */
  if (stage == 0x06 && kind == 3 && object_id == 0x03)
    return native_pass;

  return native_pass ? kind != 3 : kind == 3;
}
