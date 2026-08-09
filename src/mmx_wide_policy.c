#include "mmx_wide_policy.h"

bool MmxWidePolicy_IsBossDoorRow(const uint16_t tiles[4], int row_index) {
  if (!tiles || (unsigned)row_index >= 4)
    return false;

  /* The cap varies with its facing, while the three animated body metatiles
   * are the stable signature shared by the back-to-back columns. */
  return tiles[1] == 0x0172 && tiles[2] == 0x0173 &&
         tiles[3] == 0x0174;
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
