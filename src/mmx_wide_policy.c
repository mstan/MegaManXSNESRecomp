#include "mmx_wide_policy.h"

bool MmxWidePolicy_IsStageScene(const uint8_t ram[0x20000]) {
  /* $80:997B: level setup, arrival, play, death and stage-clear all retain
   * the stage view. The next state ($0A) changes to the password/menu flow.
   * The weapon menu suspends the HUD task ($1F10=8) and owns HDMA channel 7.
   * Neither condition alone identifies it: cutscenes hide the HUD, while
   * Spark's moving lights also use that HDMA channel during gameplay. */
  return ram && ram[0xd1] == 2 && ram[0xd2] == 4 && ram[0xd3] <= 8 && !(ram[0xd3] & 1) &&
      !(ram[0x1f10] == 8 && (ram[0xc3] & 0x80));
}

bool MmxWidePolicy_IsCollectible(uint8_t object_id) {
  /* Health/weapon-energy pickups, Sub Tanks, and Heart Tanks. Other kind-0
   * records include vehicles and mechanisms and retain native timing. */
  return (object_id >= 1 && object_id <= 5) || object_id == 0x0b;
}

bool MmxWidePolicy_RescanSpawnRecord(uint8_t stage, uint8_t kind, uint8_t object_id) {
  /* A vertical climb can bring an authored lift into view after its column
   * passed both horizontal cursors. Rescan Kuwanger's rideable lift ($16),
   * alongside pickups, using DCDB's existing live flags. Its attached cannon
   * ($17) is allocated by the lift itself, never by this recovery pass. */
  return (kind == 0 && MmxWidePolicy_IsCollectible(object_id)) ||
      (stage == 7 && kind == 3 && object_id == 0x16);
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

static bool unstarted_streaker(const uint8_t ram[0x20000], uint16_t object) {
  if (!ram || ram[0x1f7a] != 6 || object < 0xe68 || object > 0x1228 ||
      (object & 63) != 0x28 || !ram[object] || ram[object + 10] != 0x37 ||
      ram[object + 1] != 2 || ram[object + 2] || ram[object + 3] || !ram[object + 0x27])
    return false;
  return true;
}

void MmxWidePolicy_StreakerEntrance(uint8_t ram[0x20000], uint16_t object, unsigned margin) {
  if (!margin || !unstarted_streaker(ram, object)) return;
  /* Run once after $87:A527 has chosen the original flight direction.
   * The native event starts the encounter, then the moving actor enters
   * from beyond the wide edge instead of appearing in its middle. */
  int lead = (int)margin + 32;
  int x = read_word(ram, object + 5) + (ram[object + 0xb] ? -lead : lead);
  write_word(ram, object + 5, (uint16_t)x);
  write_word(ram, object + 0x22, (uint16_t)x);
}

bool MmxWidePolicy_RecoverParkedStreaker(uint8_t ram[0x20000], uint16_t object, uint16_t authored_x) {
  if (!MmxWidePolicy_IsStageScene(ram) || !unstarted_streaker(ram, object)) return false;
  if (ram[object + 0x27] != 2 || read_word(ram, object + 5) != authored_x) return false;
  unsigned column = read_word(ram, object + 5) & ~31u;
  unsigned camera_column = read_word(ram, 0x1e4d) & ~31u;
  unsigned flag = read_word(ram, object + 0xc);
  unsigned owner = ram[object + 0x2d];
  if (column <= camera_column + 256 || flag < 0xfa00 || flag > 0xfffb ||
      ram[flag] != 1 || (owner != 0x40 && owner != 0x80)) return false;
  /* Older spike saves contain early, parked actors ahead of the untouched
   * native event cursor. Mirror $87:A94F / $82:8387 so that cursor can
   * allocate them at the proper time. Never resurrect an attacked actor. */
  ram[owner == 0x40 ? 0xaa1 : 0xaaf] = 0;
  ram[owner == 0x40 ? 0xaa8 : 0xab6] = 0;
  ram[0x1f2c] &= (uint8_t)~owner;
  if (!ram[0x1f2c]) ram[0xc9] = 0;
  ram[flag] = 0;
  write_word(ram, object, 0); write_word(ram, object + 2, 0); write_word(ram, object + 0xe, 0);
  return true;
}

bool MmxWidePolicy_PresentationCull(const uint8_t ram[0x20000], uint16_t object,
                                    uint16_t distance, unsigned margin, bool custom) {
  bool traffic = ram[0x1f7a] == 0 && ram[(uint16_t)(object + 10)] == 0x21;
  bool armor = custom && object == 0xe18;
  bool enemy = custom && object >= 0xe68 && object < 0x1228 && (object & 63) == 0x28;
  /* The grinder and Kuwanger elevator already have widened lifetimes, but
   * draw through $82:808F's separate horizontal presentation test. The
   * elevator's boarding test ($87:AF10) and movement remain guest-owned. */
  bool platform = enemy && (ram[object + 10] == 0x2c || ram[object + 10] == 0x3d);
  if (!traffic && !armor && !platform) margin = 0;
  return (uint16_t)(distance + margin) >= (uint16_t)(0x1c0 + 2 * margin);
}

uint16_t MmxWidePolicy_ChainPlatformLine(const uint8_t ram[0x20000], uint16_t object,
                                      uint16_t line, unsigned margin) {
  /* $81:F97A parameters 3/4 only create/remove the airport chain platforms;
   * the other parameters move the camera, palette or unrelated mechanisms. */
  if (!ram || ram[0x1f7a] != 5 || object < 0x1d08 || object >= 0x1e08 ||
      (object & 15) != 8 || ram[object + 0xa] != 4) return line;
  if (ram[object + 0xb] == 3) return (uint16_t)(line - margin);
  if (ram[object + 0xb] == 4) return (uint16_t)(line + margin);
  return line;
}

bool MmxWidePolicy_IsBossEncounter(uint8_t object_id) {
  /* The eight Mavericks and Bospider call the shared defeated-boss guard
   * $84:AADD during initialization. The other fortress encounters have
   * dedicated intro controllers. Classify the family once, independent of
   * stage, so rematches obey the same native event scan as first encounters.
   * These initializers can seize the camera/player before drawing a sprite. */
  switch (object_id) {
    case 0x02: /* Chill Penguin */
    case 0x05: /* Boomer Kuwanger */
    case 0x07: /* Launch Octopus */
    case 0x0a: /* Sting Chameleon */
    case 0x0c: /* Flame Mammoth */
    case 0x14: /* Armored Armadillo */
    case 0x31: /* Spark Mandrill */
    case 0x52: /* Storm Eagle */
    case 0x5d: /* Rangda Bangda controller */
    case 0x62: /* D-Rex controller */
    case 0x63: /* Bospider */
    case 0x65: /* Sigma / Velguarder encounter controller */
    case 0x03: /* Thunder Slimer */
    case 0x22: /* Bee Blader */
      return true;
    default:
      return false;
  }
}

bool MmxWidePolicy_SpawnRecordAllowed(uint8_t stage, uint8_t kind,
                                      uint8_t object_id, bool native_pass) {
  kind &= 0x0f;

  /* Highway's moving traffic is kind 1 presentation work. It is allowed in
   * both passes: the wide pass makes it enter naturally, while the native pass
   * lets legacy saves catch up and the guest live flag keeps it idempotent. */
  if (stage == 0x00 && kind == 1 && object_id == 0x21)
    return true;

  if (kind == 0 && MmxWidePolicy_IsCollectible(object_id)) return true;

  /* Boss records belong to the native scan, just like camera/door events.
   * Its independent cursor reaches them at the authored arena boundary.
   * Streakers also retain their room timing, with a moving offscreen entry. */
  if (kind == 3 && (MmxWidePolicy_IsBossEncounter(object_id) || object_id == 0x37))
    return native_pass;

  return native_pass ? kind != 3 : kind == 3;
}
