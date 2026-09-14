#include "mmx_wide_policy.h"

static uint16_t read_word(const uint8_t *ram, unsigned a) {
  return (uint16_t)(ram[a] | (ram[a + 1] << 8));
}
static void write_word(uint8_t *ram, unsigned a, uint16_t value) {
  ram[a] = (uint8_t)value; ram[a + 1] = (uint8_t)(value >> 8);
}
uint16_t MmxWidePolicy_BeeEntrance(uint8_t ram[0x20000], uint16_t object,
                                  uint16_t distance) {
  if (!ram || ram[0x1f7a] != 0 || object < 0xe68 || object > 0x1228 ||
      (object & 63) != 0x28 || !ram[object] || ram[object + 10] != 0x22 || ram[object + 1] != 2)
    return distance;
  unsigned x = read_word(ram, object + 5);
  unsigned camera = read_word(ram, 0x1e4d);
  uint16_t lock = (uint16_t)(x - 0xf0);
  uint16_t old_left = read_word(ram, object + 0x3b), old_right = read_word(ram, object + 0x31);
  /* DCDB indexes 32-pixel columns at camera+$100. B8E6 saves the old
   * limits, then sets both to objectX-$F0. Existing saves can already have
   * that lock latched hundreds of pixels early; do not move X or the camera. */
  if (camera + 0x100 < (x & ~31u)) {
    if (read_word(ram, 0x1e5e) == lock && read_word(ram, 0x1e60) == lock) {
      write_word(ram, 0x1e5e, old_left); write_word(ram, 0x1e60, old_right);
    }
    return 0;
  }
  if (read_word(ram, 0x1e5e) == old_left && read_word(ram, 0x1e60) == old_right) {
    write_word(ram, 0x1e5e, lock); write_word(ram, 0x1e60, lock);
  }
  return distance;
}

uint8_t MmxWidePolicy_CrusherTileBase(const uint8_t ram[0x20000], uint16_t object, uint8_t base) {
  if (!ram || ram[0x1f7a] != 0 || (object & 63) != 0x28 || ram[(uint16_t)(object + 10)] != 9)
    return base;
  uint16_t parent = (uint16_t)(ram[(uint16_t)(object + 12)] | (ram[(uint16_t)(object + 13)] << 8));
  return (parent & 63) == 0x28 && ram[(uint16_t)(parent + 10)] == 15 ? ram[(uint16_t)(parent + 24)] : base;
}

bool MmxWidePolicy_ForceNativeSpawnTiming(uint8_t stage, uint16_t camera,
                                         unsigned lookahead) {
  unsigned start = lookahead < 0x900 ? 0x900 - lookahead : 0;
  return stage == 9 && camera >= start && camera <= 0xa80;
}

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
  /* Highway's Bee Blader also starts an arena camera push in its init,
   * before its separate descent state. It must not initialize in a margin. */
  if ((stage == 0x06 && kind == 3 && object_id == 0x03) ||
      (stage == 0x00 && kind == 3 && object_id == 0x22))
    return native_pass;

  return native_pass ? kind != 3 : kind == 3;
}
