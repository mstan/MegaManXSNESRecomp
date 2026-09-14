#include "mmx_wide_policy.h"

bool MmxWidePolicy_IsStageScene(const uint8_t ram[0x20000]) {
  /* $80:997B: level setup, arrival, play, death and stage-clear all retain
   * the stage view. The next state ($0A) changes to the password/menu flow. */
  return ram && ram[0xd1] == 2 && ram[0xd2] == 4 && ram[0xd3] <= 8 && !(ram[0xd3] & 1);
}

static uint16_t read_word(const uint8_t *ram, unsigned a) {
  return (uint16_t)(ram[a] | (ram[a + 1] << 8));
}
static void write_word(uint8_t *ram, unsigned a, uint16_t value) {
  ram[a] = (uint8_t)value; ram[a + 1] = (uint8_t)(value >> 8);
}
uint16_t MmxWidePolicy_FlyerLeash(unsigned margin) {
  /* $83:DF71 limits id-$36's travel from its spawn, even while homing.
   * Give an early-spawned flyer enough travel to reach the native arena. */
  return (uint16_t)(0xa0 + margin);
}
bool MmxWidePolicy_RideArmorCull(uint16_t distance, unsigned margin) {
  /* $83:8948 has its own cam-128..cam+383 horizontal lifetime window. */
  return (uint16_t)(distance + margin) >= 0x200 + 2 * margin;
}
bool MmxWidePolicy_PrematureRideArmor(const uint8_t ram[0x20000]) {
  /* Compatibility with early spike saves: the empty Chill Penguin armor
   * was initialized, then culled before its first animation/physics update.
   * Require that exact untouched spawn signature; used/damaged/destroyed
   * armor must never be recreated. This is checked only after a state load. */
  return MmxWidePolicy_IsStageScene(ram) && ram[0x1f7a] == 8 &&
      read_word(ram, 0xe18) == 0 && read_word(ram, 0xe1a) == 0 &&
      read_word(ram, 0xe1d) == 0x1220 && read_word(ram, 0xe20) == 0x390 &&
      ram[0xe2e] == 0x4a && ram[0xe2f] == 0 && ram[0xe3f] == 0x10 &&
      read_word(ram, 0xe38) == 0xbb4c && read_word(ram, 0xbad) < 0x1200;
}
bool MmxWidePolicy_RecoverRideArmor(uint8_t ram[0x20000], unsigned margin) {
  if (!MmxWidePolicy_PrematureRideArmor(ram)) return false;
  uint16_t dx = (uint16_t)(read_word(ram, 0xe1d) - read_word(ram, 0x1e4d) + 0x80);
  uint16_t dy = (uint16_t)(read_word(ram, 0xe20) - read_word(ram, 0x1e50) + 0x80);
  if (MmxWidePolicy_RideArmorCull(dx, margin) || dy >= 0x1e0) return false;
  ram[0xe18] = 1; /* Let the original initialization/physics resume. */
  return true;
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

  /* Heart Tanks are persistent collectibles, not camera/encounter events. */
  if (kind == 0 && object_id == 0x0b) return true;

  /* Spark Mandrill's Thunder Slimer mid-boss controller is authored as kind
   * 3 even though it is an encounter trigger, not an ordinary margin enemy.
   * Spawning it early lets it tear itself down before the arena boundary and
   * the native pass then refuses it, leaving the barrier permanently closed. */
  /* Highway's Bee Blader also starts an arena camera push in its init,
   * before its separate descent state. It must not initialize in a margin.
   * Chill Penguin must wait until X has crossed the second boss door.
   * Spark's id-$37 light controllers also retain their authored room entry. */
  if ((stage == 0x06 && kind == 3 && (object_id == 0x03 || object_id == 0x37)) ||
      (stage == 0x00 && kind == 3 && object_id == 0x22) ||
      (stage == 0x08 && kind == 3 && object_id == 0x02))
    return native_pass;

  return native_pass ? kind != 3 : kind == 3;
}
