#include "mmx_zero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *poses;
static uint8_t *charge_poses;
static uint16_t charge_colors[3][16];
static uint16_t colors[128];
static uint8_t saber_bounds[40];
static uint8_t hud_tiles[128];
static uint16_t hud_colors[16];
static uint8_t animation[MMX_ZERO_ANIMATION_BYTES];
static uint8_t muzzle[MMX_ZERO_MUZZLE_BYTES];
static MmxZeroState state;
static const MmxZeroExtension *extension;
static bool (*extension_gate)(void);
_Static_assert(offsetof(MmxZeroState, anim_offset) == MMX_ZERO_LEGACY_STATE_SIZE,
               "Keep the v4 combat-state prefix readable");
_Static_assert(offsetof(MmxZeroState, burst_offset) == MMX_ZERO_ANIMATION_STATE_SIZE,
               "Keep the v5 animation-state prefix readable");
_Static_assert(offsetof(MmxZeroState, active_x) == MMX_ZERO_COMBAT_STATE_SIZE,
               "Keep the v6 combat-state prefix readable");
_Static_assert(offsetof(MmxZeroState, hp) == MMX_ZERO_SWAP_STATE_SIZE,
               "Keep the v7 character/swap-state prefix readable");
_Static_assert(offsetof(MmxZeroState, modern) == MMX_ZERO_HEALTH_STATE_SIZE,
               "Keep the v8-v14 Zero state prefix readable");
static unsigned word(const uint8_t *p) { return p[0] | p[1] << 8; }
static void putword(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
MmxZeroState MmxZeroGetState(void) { return state; }
/* The selected starting character, also shown on title and menus until the
 * first exchange. Co-op never selects it and keeps Zero. */
static bool start_x;
static bool modern_behavior;
void MmxZeroSetModern(bool enabled) { modern_behavior = enabled; }
bool MmxZeroModern(void) { return MmxZeroActive() && modern_behavior; }
void MmxZeroSetStartCharacter(bool x) { start_x = x; }
void MmxZeroSetExtension(const MmxZeroExtension *ext) { extension = ext; }
void MmxZeroSetExtensionGate(bool (*gate)(void)) { extension_gate = gate; }
/* Per-frame and per-hit callbacks belong to the seat that owns the extension.
 * Lifecycle callbacks (state_reset, collision_rom) are not seat-owned. */
static const MmxZeroExtension *live_extension(void) {
  return extension && (!extension_gate || extension_gate()) ? extension : NULL;
}
void MmxZeroExtPrePlayer(uint8_t *ram) {
  const MmxZeroExtension *ext = live_extension();
  if (ext && ext->pre_player) ext->pre_player(ram);
}
void MmxZeroExtPlayerEnd(uint8_t *ram) {
  const MmxZeroExtension *ext = live_extension();
  if (ext && ext->player_end) ext->player_end(ram);
}
void MmxZeroExtSaveState(uint8_t out[MMX_ZERO_EXTENSION_STATE_BYTES]) {
  memset(out, 0, MMX_ZERO_EXTENSION_STATE_BYTES);
  if (extension && extension->state_save) extension->state_save(out);
}
void MmxZeroExtLoadState(const uint8_t in[MMX_ZERO_EXTENSION_STATE_BYTES]) {
  if (extension && extension->state_load) extension->state_load(in);
}
static void clear_state(void) {
  memset(&state, 0, sizeof(state)); state.active_x = start_x;
  state.modern.enabled = modern_behavior;
}
void MmxZeroResetState(void) {
  clear_state();
  if (extension && extension->state_reset) extension->state_reset(NULL);
}
bool MmxZeroValidState(const MmxZeroState *value) {
  if (!value) return false;
  MmxZeroState s = *value;
  return s.combo <= 2 && s.slash <= 44 && s.charge <= 201 &&
      s.modern.enabled <= 1 && s.modern.jump_used <= 1 && s.modern.dash_used <= 1 &&
      s.modern.dash_ticks <= 18 && (s.modern.dash_facing == 0 || s.modern.dash_facing == 64) &&
      s.modern.slash_buffer <= 6 && s.modern.hit_phase <= 1 && !s.modern.reserved &&
      s.active_x <= 1 && s.swap_phase <= 6 && s.swap_tick <= 30 && s.swap_y <= 0 && s.swap_y >= -320 &&
      s.hp_valid <= 1 && s.hp_max <= 32 && s.hp[0] <= 32 && s.hp[1] <= 32 &&
      (!s.hp_valid || (s.hp_max >= 16 && s.hp[0] <= s.hp_max && s.hp[1] <= s.hp_max)) &&
      s.air <= 1 && (s.facing == 0 || s.facing == 64) && s.anim_valid <= 1 &&
      s.burst <= 2 && s.burst_end <= 1 && s.saber_ready <= 1 && s.burst_transition <= 1 && s.burst_fired <= 1 &&
      (!s.burst || (s.burst_offset >= 272 && s.burst_offset + 3 <= sizeof(animation) &&
                    s.burst_timer && animation[s.burst_offset + 2] < 117)) &&
      (!s.anim_valid || (s.anim_offset >= 272 && s.anim_offset + 3 <= sizeof(animation) &&
                        s.anim_timer && s.anim_pose < 117)) &&
      (!s.projectile || (s.projectile >= 0x1228 && s.projectile < 0x1428 && (s.projectile & 63) == 0x28));
}
static void assign_state(MmxZeroState s) {
  clear_state();
  if (poses && MmxZeroValidState(&s)) state = s;
  if (state.modern.enabled != modern_behavior) {
    /* Old saves start with fresh aerial actions. A changed ruleset cannot
     * inherit the other ruleset's charge combo or attack recovery. */
    MmxZeroCancel(NULL);
    memset(&state.modern, 0, sizeof(state.modern));
    state.modern.enabled = modern_behavior;
  }
}
void MmxZeroSetState(MmxZeroState s) {
  assign_state(s);
  if (extension && extension->state_reset) extension->state_reset(NULL);
}
void MmxZeroSelectState(MmxZeroState s) { assign_state(s); }
unsigned MmxZeroChargeTier(const MmxZeroState *s) {
  return !s || s->charge < 21 ? 0 : s->charge < 81 ? 4 :
         s->charge < 141 ? 6 : s->charge < 201 ? 8 : 10;
}

static unsigned legacy_charge_cap(void) {
  const MmxZeroExtension *ext = live_extension();
  if (ext && ext->charge_cap) {
    unsigned cap = ext->charge_cap();
    if (cap) return cap;
  }
  return 201;
}

bool MmxZeroEnabled(void) { return poses != NULL; }
bool MmxZeroActive(void) { return poses && !state.active_x; }
bool MmxZeroSwapping(void) { return poses && state.swap_phase; }
void MmxZeroHealthSync(const uint8_t r[0x20000]) {
  if (!poses || !r || r[0xd1] != 2 || r[0x1f9a] < 16 || r[0x1f9a] > 32) return;
  unsigned max = r[0x1f9a], hp = r[0xbcf] & 127;
  if (hp > max) hp = max;
  /* Older saves have one pool. Seed both from it exactly once. All normal
   * damage, pickups and subtanks continue to affect only native active HP. */
  if (!state.hp_valid) state.hp[0] = state.hp[1] = (uint8_t)hp;
  state.hp_valid = 1; state.hp_max = (uint8_t)max;
  state.hp[state.active_x] = (uint8_t)hp;
  if (state.hp[state.active_x ^ 1] > max) state.hp[state.active_x ^ 1] = (uint8_t)max;
}
void MmxZeroHealthRespawn(const uint8_t r[0x20000]) {
  if (!poses || !r || r[0x1f9a] < 16 || r[0x1f9a] > 32) return;
  if (extension && extension->state_reset) extension->state_reset((uint8_t *)r);
  /* Called at the original stage/checkpoint HP initialization, not whenever
   * a pool happens to reach zero. Death and life loss remain native. */
  state.hp_valid = 1; state.hp_max = r[0x1f9a];
  state.hp[0] = state.hp[1] = state.hp_max;
}
void MmxZeroDisable(void) {
  MmxZeroResetState();
  free(poses); poses = NULL;
  free(charge_poses); charge_poses = NULL;
}
bool MmxZeroLoad(const char *path) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return false;
  uint8_t header[20], palette[256], bounds[40], hud[160], anim[MMX_ZERO_ANIMATION_BYTES];
  uint8_t emission[MMX_ZERO_MUZZLE_BYTES];
  size_t size = (size_t)MMX_ZERO_POSES * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
  uint8_t *data = NULL, *particles = NULL, flash[96];
  bool modern = false;
  bool ok = fread(header, 1, sizeof(header), f) == sizeof(header) &&
      ((modern = !memcmp(header, "MMXZERO7", 8)) || !memcmp(header, "MMXZERO6", 8)) && word(header + 8) == MMX_ZERO_WIDTH &&
      word(header + 10) == MMX_ZERO_HEIGHT && word(header + 12) == 64 &&
      word(header + 14) == 64 && word(header + 16) == 117 && word(header + 18) == 35 &&
      fread(palette, 1, sizeof(palette), f) == sizeof(palette) &&
      fread(bounds, 1, sizeof(bounds), f) == sizeof(bounds) &&
      fread(hud, 1, sizeof(hud), f) == sizeof(hud) &&
      fread(anim, 1, sizeof(anim), f) == sizeof(anim) &&
      fread(emission, 1, sizeof(emission), f) == sizeof(emission);
  if (ok) { data = malloc(size); ok = data && fread(data, 1, size, f) == size; }
  size_t charge_size = (size_t)MMX_ZERO_CHARGE_POSES * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
  if (ok && modern) {
    particles = malloc(charge_size);
    ok = particles && fread(flash, 1, sizeof(flash), f) == sizeof(flash) &&
        fread(particles, 1, charge_size, f) == charge_size;
    if (ok) for (size_t i=0;i<charge_size;++i) if (particles[i]>=16) { ok=false; break; }
  }
  if (ok) ok = fgetc(f) == EOF;
  fclose(f);
  if (ok) for (unsigned i = 0; i < sizeof(bounds); i += 4)
    if (!bounds[i + 2] || bounds[i + 2] > 64 || !bounds[i + 3] || bounds[i + 3] > 64) { ok = false; break; }
  if (ok) for (size_t i = 0; i < size; ++i) if (data[i] >= 128) { ok = false; break; }
  if (ok) for (unsigned i = 0; i < 136; ++i) {
    unsigned p = word(anim + i * 2);
    if (p < 272 || p + 3 > sizeof(anim) || !anim[p] || anim[p + 2] >= 117) { ok = false; break; }
  }
  if (ok) for (unsigned i = 0; i < 117; ++i)
    if ((emission[i] & 1) || emission[i] > 74) { ok = false; break; }
  if (!ok) { free(data); free(particles); return false; }
  MmxZeroDisable(); poses = data; charge_poses = particles;
  if (modern) for (unsigned i=0;i<48;++i) charge_colors[i/16][i%16]=(uint16_t)(word(flash+i*2)&0x7fff);
  memcpy(saber_bounds, bounds, sizeof(bounds));
  memcpy(animation, anim, sizeof(animation));
  memcpy(muzzle, emission, sizeof(muzzle));
  memcpy(hud_tiles, hud, sizeof(hud_tiles));
  for (unsigned i = 0; i < 16; ++i) hud_colors[i] = (uint16_t)(word(hud + 128 + 2 * i) & 0x7fff);
  for (unsigned i = 0; i < 128; ++i) colors[i] = (uint16_t)(word(palette + 2 * i) & 0x7fff);
  return true;
}
const uint16_t *MmxZeroColors(void) { return colors; }
bool MmxZeroHasChargeArt(void) { return charge_poses != NULL; }
int MmxZeroChargeFlashPaletteIndex(const MmxZeroState *s) {
  if (!charge_poses || !s || s->active_x || s->swap_phase || s->slash ||
      (s->charge < 25 && !(s->combo && s->saber_ready)) || (s->charge_phase & 2)) return -1;
  return s->saber_ready || s->charge >= 201 ? 2 : s->charge >= 141 ? 1 : 0;
}
const uint16_t *MmxZeroBodyColors(const MmxZeroState *s) {
  int index = MmxZeroChargeFlashPaletteIndex(s);
  return index < 0 ? colors + 16 : charge_colors[index];
}
const uint8_t *MmxZeroChargePose(const MmxZeroState *s) {
  if (!charge_poses || !s || s->active_x || s->swap_phase || s->slash || s->burst || s->combo || s->charge < 21) return NULL;
  unsigned group = s->charge < 81 ? 0 : s->charge < 141 ? 1 : 2;
  return charge_poses + (size_t)(group * 22 + s->charge_phase % 22) * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
}
bool MmxZeroNativeChargeObject(unsigned object, unsigned kind) {
  /* $82:82ED allocates any of twelve small actors, not just $0C98. */
  return object >= 0xc98 && object < 0xe18 && ((object - 0xc98) % 32) == 0 && kind == 1;
}
void MmxZeroDeathOrbSpawn(uint8_t r[0x20000], unsigned source, unsigned orb) {
  if (!poses || !r || source != 0xba8 || orb < 0x1928 || orb >= 0x1d08 ||
      (orb - 0x1928) % 32 || r[orb + 10] != 14) return;
  /* Effect $0E never uses its secondary state byte ($02). Store its owner
   * color there at allocation, before co-op projects the surviving actor.
   * This follows native snapshots and resets explicitly on X's allocation. */
  r[orb + 2] = state.active_x ? 0 : 0x5a;
}
bool MmxZeroDeathOrbRed(const uint8_t r[0x20000], unsigned orb) {
  return poses && r && orb >= 0x1928 && orb < 0x1d08 && !((orb - 0x1928) % 32) &&
      r[orb] && r[orb + 10] == 14 && r[orb + 2] == 0x5a;
}
const uint8_t *MmxZeroMenuPose(void) { return poses; }
static void animation_record(unsigned offset) {
  if (offset < 272 || offset + 3 > sizeof(animation) || !animation[offset] || animation[offset + 2] >= 117) {
    state.anim_valid = 0; return;
  }
  state.anim_offset = (uint16_t)offset;
  state.anim_timer = animation[offset]; state.anim_flags = animation[offset + 1];
  state.anim_pose = animation[offset + 2]; state.anim_valid = 1;
}
static unsigned sequence_offset(unsigned sequence) {
  /* X1 group 0 -> original X3 group $4A. Aliases are kept: firing overlays
   * resume at interior records, not at the beginning of a movement cycle.
   * X1's two Hadouken actions use Zero's forward buster/recovery poses. */
  static const uint8_t map[] = {
    0x00,0x01,0x02,0x03,0x05,0x07,0x08,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,
    0x13,0x14,0x15,0x16,0x17,0x1a,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
    0x28,0x29,0x50,0x51,0x52,0x53,0x55,0x57,0x58,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,
    0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x6a,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,
    0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x82,0x83,0x84,0x30,
    0x34
  };
  return sequence < sizeof(map) ? word(animation + map[sequence] * 2) : 0;
}
void MmxZeroAnimationStart(unsigned object, unsigned sequence) {
  if (!poses || object != 0xba8) return;
  animation_record(sequence_offset(sequence));
}
void MmxZeroAnimationAdvance(unsigned object) {
  if (!poses || object != 0xba8 || !state.anim_valid) return;
  if (--state.anim_timer) return;
  unsigned next = state.anim_offset + 3;
  if (state.anim_flags & 128) {
    if (next + 2 > sizeof(animation)) { state.anim_valid = 0; return; }
    next = (unsigned)((int)next + (int16_t)word(animation + next));
  }
  animation_record(next);
}
static unsigned burst_sequence_for(unsigned which, bool air) {
  return which == 1 ? (air ? 0x43 : 0x30) : (air ? 0x49 : 0x36);
}
static bool burst_emission_y(unsigned which, bool air, int *value) {
  if (!value || (which != 1 && which != 2)) return false;
  unsigned offset = word(animation + burst_sequence_for(which, air) * 2);
  for (unsigned i = 0; i < sizeof(animation) / 3; ++i) {
    if (offset < 272 || offset + 3 > sizeof(animation)) return false;
    unsigned flags = animation[offset + 1];
    if (flags & 128) return false;
    if (flags & 64) {
      unsigned pose = animation[offset + 2];
      unsigned muzzle_offset = pose < 117 ? muzzle[pose] : 0;
      if (!muzzle_offset) return false;
      *value = (int8_t)(muzzle[120 + muzzle_offset] - 8);
      return true;
    }
    offset += 3;
  }
  return false;
}
unsigned MmxZeroMuzzle(const uint8_t r[0x20000], unsigned object,
                      unsigned native_index, unsigned axis, unsigned original) {
  if (!MmxZeroActive() || !r || object < 0x1228 || object >= 0x1428 ||
      (object & 63) != 0x28 || axis > 1) return original;
  unsigned pose = state.burst ? animation[state.burst_offset + 2] :
      state.anim_valid ? state.anim_pose : r[0xbbf] & 127;
  unsigned offset = pose < 117 ? muzzle[pose] : 0;
  if (!offset) {
    /* Delayed/formation projectiles can initialize after the firing overlay
     * ends. X1 retains its firing sequence index in the projectile's $3C. */
    unsigned record = sequence_offset(native_index / 2);
    if (!record) return original;
    pose = animation[record + 2]; offset = muzzle[pose];
  }
  if (!offset) return original;
  /* X3 stores signed Y then left-facing X. X1's native helpers mirror a
   * positive X and sign-extend Y. Keep their later spread/trajectory offsets. */
  if (axis) {
    unsigned result = (unsigned)(uint8_t)(muzzle[120 + offset] - 8);
    const MmxZeroExtension *ext = live_extension();
    if (state.burst && ext && ext->burst_origin_y) {
      int paired_y;
      unsigned other = state.burst == 1 ? 2 : 1;
      if (burst_emission_y(other, state.air, &paired_y))
        result = (unsigned)(uint8_t)ext->burst_origin_y(
            r, state.burst - 1, (int8_t)result, paired_y);
    }
    return result;
  }
  return (unsigned)(uint8_t)(-(int8_t)muzzle[121 + offset]);
}
int MmxZeroHudColor(unsigned x, unsigned y) {
  /* Original X3 tile/palette data, independent of body visibility. */
  if (!MmxZeroEnabled() || x >= 16 || y >= 16) return -1;
  unsigned tile = (y / 8) * 2 + x / 8, shift = 7 - (x & 7);
  const uint8_t *p = hud_tiles + tile * 32 + (y & 7) * 2;
  unsigned pixel = ((p[0] >> shift) & 1) | (((p[1] >> shift) & 1) << 1) |
      (((p[16] >> shift) & 1) << 2) | (((p[17] >> shift) & 1) << 3);
  return pixel ? hud_colors[pixel] : -2;
}
unsigned MmxZeroWeaponOrigin(const uint8_t r[0x20000], unsigned object,
                             unsigned axis, unsigned original) {
  if (!MmxZeroActive() || !r || object < 0x1228 || object >= 0x1428 ||
      (object & 63) != 0x28) return original;
  /* These retail routines bypass the shared muzzle table. Apply the offset
   * where they assign the real object origin, so art and collision agree.
   * Tornado remains centered vertically; Boomerang keeps its return arc. */
  unsigned kind = r[object + 10];
  int delta = axis == 0 && kind == 0x0b ? (r[object + 17] & 64 ? 6 : -6) :
              axis == 1 && kind == 0x0d ? -8 :
              axis == 1 && kind == 0x12 ? -6 : 0;
  return (original + delta) & 0xffff;
}
static unsigned slash_pose(const MmxZeroState *s) {
  /* Vanilla X3 group $4B actions $00/$0E: duration, frame. */
  static const uint8_t duration[] = {3,3,1,2,3,3,6,16,3,3,3};
  static const uint8_t pose[] = {0,1,2,2,3,4,5,6,4,1,0};
  unsigned age = s->slash;
  for (unsigned i = 0; i < sizeof(duration); ++i) {
    if (age < duration[i]) return pose[i] + (s->air ? 7 : 0);
    age -= duration[i];
  }
  return 0;
}
int MmxZeroPoseOffsetY(const uint8_t ram[0x20000]) {
  /* X3's kneeling art ends ten pixels below its ordinary standing origin.
   * Align its feet with X1's authored kneeling ground line. */
  return ram && ram[0xbbe] == 0x66 && (ram[0xbbf] & 127) != 4 ? -18 : -8;
}
const uint8_t *MmxZeroPose(const uint8_t ram[0x20000], const MmxZeroState *s) {
  /* Visibility belongs to the submitted sprite list, not this RAM snapshot.
   * During invulnerability the next update can hide the player while OAM
   * still contains the preceding visible frame. The compositor owns blinking. */
  if (!poses || !ram || (s && s->active_x)) return NULL;
  /* $81:91F1's Highway electric restraint starts native sequence $49.
   * That sequence is an attack alias in the ordinary X3 movement map.
   * Keep Zero in his original staggered hurt pose until the rescue starts;
   * group $66 below owns the subsequent fall, kneeling and dialogue. */
  if (ram[0x1f7a] == 0 && ram[0xbaa] == 0x32 && ram[0xbab] <= 2)
    return poses + (size_t)0x33 * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
  /* Action $36 is the ground-shock stun (Flame Mammoth's stomp). Its X1
   * frames $4B/$4C have no X3 movement record, so the native frame index
   * named an empty pose and Zero vanished for the whole stun. Hold the same
   * staggered hurt pose. */
  if (ram[0xbaa] == 0x36)
    return poses + (size_t)0x33 * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
  /* X1's Vile capture/rescue uses group $66, not the normal body group.
   * Its five poses are kneeling/blinking, then suspended. Use original X3
   * kneeling/hurt art instead of reading these as running/firing sequences. */
  if (ram[0xbbe] == 0x66) {
    unsigned pose = (ram[0xbbf] & 127) == 4 ? 0x34 : 0x49;
    return poses + (size_t)pose * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
  }
  /* Old saves without mirrored animation state use the shared pose vocabulary
   * until the next native animation start. New saves retain the exact phase. */
  unsigned pose = s && s->anim_valid ? s->anim_pose : ram[0xbbf] & 127;
  if (s && s->burst && s->burst_offset + 3 <= sizeof(animation)) pose = animation[s->burst_offset + 2];
  if (pose >= 117) pose = 0;
  if (s && s->slash) pose = 117 + slash_pose(s);
  return poses + (size_t)pose * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
}
const uint8_t *MmxZeroBlade(const MmxZeroState *s) {
  if (!poses || !s || s->slash < 7 || s->slash > 31) return NULL;
  static const uint8_t duration[] = {3,3,3,3,7,3,3};
  unsigned age = s->slash - 7, pose = 0;
  while (pose < 6 && age >= duration[pose]) age -= duration[pose++];
  return poses + (size_t)(138 + pose + (s->air ? 7 : 0)) * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT;
}
void MmxZeroSetCollisionRom(uint8_t *rom, size_t size) {
  if (!rom || size < 0x37fd8) return;
  /* X3 $86:B40E / B422, translated -8px vertically into X1's player origin.
   * This preserves Zero's full body dimensions and aligns both games' feet.
   * First four bytes are damage bounds; last six are terrain-probe geometry.
   * Dash terrain uses X1's standing height (center -1, half-height 17) so
   * Zero can dash through passages X walks through. */
  static const uint8_t normal[10] = {0, 0xfb, 6, 18, 0, 0, 0xfb, 6, 21, 8};
  static const uint8_t dash[10] = {0, 2, 6, 11, 0, 0, 0xff, 6, 17, 8};
  static const uint8_t old_normal[10] = {0,255,6,14,0,0,255,7,17,8};
  static const uint8_t old_dash[10] = {0,5,6,8,0,0,255,9,17,8};
  /* Original X3 ground/air arc bounds in verified X1 $FF padding. */
  uint8_t empty[40]; memset(empty, 255, sizeof(empty));
  if ((!memcmp(rom + 0x32552, old_normal, 10) || !memcmp(rom + 0x32552, normal, 10)) &&
      (!memcmp(rom + 0x33b38, old_dash, 10) || !memcmp(rom + 0x33b38, dash, 10)) &&
      (!memcmp(rom + 0x37fb0, empty, 40) || !memcmp(rom + 0x37fb0, saber_bounds, 40))) {
    memcpy(rom + 0x32552, MmxZeroActive() ? normal : old_normal, 10);
    memcpy(rom + 0x33b38, MmxZeroActive() ? dash : old_dash, 10);
    memcpy(rom + 0x37fb0, MmxZeroActive() ? saber_bounds : empty, 40);
  }
  if (extension && extension->collision_rom)
    extension->collision_rom(rom, size);
}
/* Zero's standing box is eight pixels taller than X's, so a passage X walks
 * through only fits Zero's dash. As in the later games' slide, Zero stays in
 * X1's native dash state, box and animation until he has room to stand. */
static bool (*terrain_query)(const uint8_t *, int, int);
void MmxZeroSetTerrainQuery(bool (*solid)(const uint8_t *ram, int x, int y)) {
  terrain_query = solid;
}
static bool standing_blocked(const uint8_t *r) {
  /* Rows only the standing box reaches: wall top (-18, $84:92E1) and
   * ceiling (-26, $84:91F5), at the center and both six-pixel edges. */
  int x = (int)word(r + 0xbad), y = (int)word(r + 0xbb0);
  for (int dx = -6; dx <= 6; dx += 6)
    if (terrain_query(r, x + dx, y - 18) || terrain_query(r, x + dx, y - 26)) return true;
  return false;
}
static bool slide_blocked(const uint8_t *r) {
  return MmxZeroActive() && r && terrain_query && r[0xba9] == 2 &&
      (r[0xbd3] & 4) && !r[0x1f0c] && standing_blocked(r);
}
/* X1's dash speed ($0375) in the current facing, for a slide with none. */
static unsigned slide_speed(const uint8_t *r) {
  return r[0xc11] & 64 ? 0x0375 : 0x10000 - 0x0375;
}
bool MmxZeroSlideHold(uint8_t r[0x20000], unsigned pc) {
  if (!slide_blocked(r) || r[0xbaa] != 0x14) return false;
  switch (pc & 0x7fffff) {
    case 0x01898e: {
      /* Releasing dash, a wall ahead or the reverse direction would stand
       * Zero up. Reverse turns the slide around, so dead ends can be left. */
      unsigned reverse = r[0xc11] & 64 ? 2 : 1, ahead = reverse ^ 3;
      unsigned speed = word(r + 0xbc2);
      if (r[0xbdf] & reverse) {
        r[0xc11] ^= 64;
        putword(r + 0xbc2, speed ? (0x10000 - speed) & 0xffff : slide_speed(r));
      } else if (!speed && (r[0xbdf] & ahead)) {
        /* Releasing the direction stops X1's dash, and nothing restarted
         * it, so a stopped slide could only turn back, never continue
         * (Armored Armadillo's low passage to the Heart Tank). */
        putword(r + 0xbc2, slide_speed(r));
      }
      return true;
    }
    case 0x018999: r[0xbfa] = 0; return true; /* Timer expiry rechecks next frame. */
    case 0x018965: return true; /* No jump without headroom. */
    default: return false;
  }
}
void MmxZeroSlideTick(uint8_t r[0x20000]) {
  /* Recovery or landing into a low passage stands Zero up; restart the dash
   * through X1's own initializer ($81:8917) instead. */
  if (!slide_blocked(r) || r[0xbaa] != 0 || word(r + 0xbc8) != 0xa552) return;
  r[0xbaa] = 0x14; r[0xbab] = 0;
}
unsigned MmxZeroUpgradeBits(unsigned pc, unsigned original) {
  if (!MmxZeroActive()) return original;
  switch (pc & 0x7fffff) {
    case 0x01971c: case 0x019793: case 0x0198fc: return original | 8;
    default: return original;
  }
}

static bool own_projectile(const uint8_t *r, unsigned d) {
  return d >= 0x1228 && d < 0x1428 && (d & 63) == 0x28 &&
      r[d] && word(r + d + 0x3e) == 0x5a53;
}
static void release_projectile(uint8_t *r) {
  unsigned d = state.projectile;
  if (own_projectile(r, d)) {
    memset(r + d, 0, 64);
    if (r[0xbdd]) --r[0xbdd];
  }
  state.projectile = 0;
}
static void clear_charge(uint8_t *r);
void MmxZeroCancel(uint8_t ram[0x20000]) {
  if (ram && (state.charge || state.combo || state.burst || state.slash)) clear_charge(ram);
  if (ram) release_projectile(ram);
  /* Combat cancellation (hurt, weapon switch, menus) must not reset the
   * independent body animation. Full reset/load uses MmxZeroResetState. */
  memset(&state, 0, MMX_ZERO_LEGACY_STATE_SIZE);
  memset((uint8_t *)&state + MMX_ZERO_ANIMATION_STATE_SIZE, 0,
         MMX_ZERO_COMBAT_STATE_SIZE - MMX_ZERO_ANIMATION_STATE_SIZE);
  state.modern.slash_buffer = state.modern.hit_phase = 0;
}
static unsigned free_projectile(const uint8_t *r) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (!word(r + d)) return d;
  return 0;
}
static void sound(uint8_t *r, unsigned command) {
  /* Same guest-owned ring as $80:88CD. The ordinary SPC driver consumes it. */
  unsigned index = r[0xba3] & 0x1e;
  r[0xb72 + index] = (uint8_t)command; r[0xb73 + index] = 0;
  r[0xba3] = (uint8_t)((index + 2) & 0x1e);
}
static void clear_charge(uint8_t *r) {
  /* $81:9890 stops the looping charge voice before clearing charge state.
   * Bypassing that release path without $17 leaves the SPC voice playing. */
  if (r[0xc2f] & 64) { sound(r,0x17); r[0xc2f] &= (uint8_t)~64; }
  memset(r + 0xbff, 0, 5);
  r[0xc2a] = 0;
  for (unsigned d=0xc98;d<0xe18;d+=32)
    if (MmxZeroNativeChargeObject(d,r[d+10])) memset(r+d,0,32);
}
unsigned MmxZeroSwapPose(const MmxZeroState *s) {
  if (!s || !s->swap_phase || s->swap_phase == 3) return 255;
  if (s->swap_phase == 2 || s->swap_phase == 4) return 0x3c;
  if (s->swap_phase == 6) return 0;
  /* Retail X1 $49/$48 and X3 $7F/$7E: five one-frame body morphs
   * and a two-frame energy ball. The traveling column is pose $3C. */
  if (s->swap_phase == 1) return s->swap_tick < 5 ? 0x3e + s->swap_tick : 0x3d;
  return s->swap_tick < 2 ? 0x3d : 0x44 - s->swap_tick;
}
const uint8_t *MmxZeroTeleportPose(unsigned pose) {
  return poses && (pose == 0 || (pose >= 0x3c && pose <= 0x42)) ?
      poses + (size_t)pose * MMX_ZERO_WIDTH * MMX_ZERO_HEIGHT : NULL;
}
bool MmxZeroSwapTick(uint8_t r[0x20000]) {
  if (!poses || !r) return false;
  MmxZeroHealthSync(r);
  if (!state.swap_phase) {
    /* Native NMI edge latch: Select=$2000. A grounded idle player owns
     * control; transitions, menus, hurt, ladders and ride armor do not. */
    if (!(r[0xac] & 0x20) || r[0xd1] != 2 || r[0xd2] != 4 ||
        r[0xba9] != 2 || r[0xbaa] != 0 || r[0xbab] != 2 ||
        !(r[0xbd3] & 4) || !r[0xbb6] || !(r[0xbcf] & 127) ||
        r[0x1f0c] || r[0x1f10] || r[0x1f23] || r[0x1f48] ||
        word(r + 0xbc2) || state.burst || state.slash) return false;
    MmxZeroCancel(r); clear_charge(r);
    state.swap_phase = 1; state.swap_tick = state.swap_fraction = 0; state.swap_y = 0;
    sound(r,0x0f); /* X1's original teleport-out sound ($81:8D05). */
  } else switch (state.swap_phase) {
    case 1:
      if (++state.swap_tick == 7) { state.swap_phase = 2; state.swap_tick = 0; }
      break;
    case 2: {
      /* Original X1 outgoing speed $0AA6, incoming speed $0800. */
      int fixed = state.swap_y * 256 + state.swap_fraction - 0x0aa6;
      state.swap_y = (int16_t)((fixed - 255) / 256);
      state.swap_fraction = (uint8_t)(fixed - state.swap_y * 256);
      int screen_y = (int16_t)(word(r + 0xbb0) - word(r + 0x1e50));
      if (screen_y + state.swap_y < -40) {
        state.active_x ^= 1; state.anim_valid = 0;
        if (state.hp_valid) r[0xbcf] = state.hp[state.active_x] | 128;
        state.swap_phase = 3; state.swap_tick = 0;
      }
      break;
    }
    case 3:
      if (++state.swap_tick == 30) { /* Original X3 exchange delay $1E. */
        state.swap_phase = 4; state.swap_tick = state.swap_fraction = 0;
        sound(r,0x0e); /* Native X1 arrival ($81:8A18). */
      }
      break;
    case 4:
      state.swap_y += 8;
      if (state.swap_y >= 0) { state.swap_y = 0; state.swap_phase = 5; }
      break;
    case 5:
      if (++state.swap_tick == 7) { state.swap_phase = 6; state.swap_tick = 0; }
      break;
    case 6:
      state.swap_phase = state.swap_tick = state.swap_fraction = 0; state.swap_y = 0;
      /* Resume idle through its native initializer, with no buffered presses. */
      r[0xbab] = 0; r[0xab] = r[0xac] = r[0xbe2] = r[0xbe3] = 0;
      return false;
  }
  /* The scheduler normally acknowledges NMI. Keep DMA/input/SPC alive while
   * every task (including stage scripts and enemy animation) is suspended. */
  r[0xb9d] = r[0xba0] = 0;
  return true;
}
static unsigned burst_sequence(void) {
  return burst_sequence_for(state.burst, state.air);
}
static void burst_record(unsigned sequence) {
  state.burst_offset = (uint16_t)word(animation + sequence * 2);
  state.burst_timer = animation[state.burst_offset];
}
static void start_burst(uint8_t *r, unsigned which) {
  state.burst = (uint8_t)which; state.burst_end = 0;
  state.burst_transition = state.burst_fired = 0;
  state.air = !(r[0xbd3] & 4); state.facing = r[0xc11] & 64;
  burst_record(burst_sequence());
  if (which == 2 && state.air) ++state.burst_timer; /* X3's separate setup frame. */
  clear_charge(r);
}
static void emit_burst(uint8_t *r) {
  unsigned d = free_projectile(r);
  if (!d) return;
  memset(r + d,0,64); r[d] = 1; r[d + 10] = 3;
  ++r[0xbdd]; r[0x1f0d] = 4;
  sound(r,2); /* Native full-buster release sound ($81:A015), once per shot. */
  state.shot_mask |= (uint8_t)(1u << ((d - 0x1228) / 64));
}
static void advance_burst(uint8_t *r) {
  unsigned flags = animation[state.burst_offset + 1];
  if (state.burst_transition) {
    burst_record(burst_sequence() + (flags & 7));
    state.burst_transition = 0;
    flags = animation[state.burst_offset + 1];
  }
  if (flags & 128) { state.burst_end = 1; return; }
  if ((flags & 64) && !state.burst_fired) {
    emit_burst(r); state.burst_fired = 1;
  }
  bool air = !(r[0xbd3] & 4);
  if (air != !!state.air) {
    /* X3 changes action first, then resumes the equivalent numbered phase
     * on the following update. The transition itself retains the old pose. */
    state.air = air;
    state.burst_transition = 1;
    return;
  }
  if (!--state.burst_timer) {
    unsigned next = state.burst_offset + 3;
    if (next + 3 > sizeof(animation) || !animation[next] || animation[next + 2] >= 117) {
      MmxZeroCancel(r); return;
    }
    state.burst_offset = (uint16_t)next;
    state.burst_timer = animation[state.burst_offset];
  }
}
static void track_burst_shots(const uint8_t *r) {
  unsigned alive = 0;
  for (unsigned i = 0; i < 8; ++i) {
    unsigned d = 0x1228 + i * 64;
    if ((state.shot_mask & (1u << i)) && r[d] && r[d + 10] == 3 &&
        !own_projectile(r,d)) alive |= 1u << i;
  }
  /* X3 requires both the charged projectile and its effects to retire. The
   * retained X1 beam executes its disappearance animation in the SAME slot
   * ($81:A3CE..A40D); C25 is decremented only afterwards. Wait for that real
   * lifetime, including impact/offscreen recovery, with no guessed delay. */
  state.shot_mask = (uint8_t)alive;
}
static bool burst_holds_air(void) {
  return state.burst && state.air && (state.burst_end ||
      (state.burst == 2 && state.burst_offset == word(animation + 0x49 * 2) && state.burst_timer == 2));
}
static bool movement_playable(const uint8_t *r) {
  unsigned a = r[0xbaa];
  return r[0xd1] == 2 && r[0xd2] == 4 && r[0xba9] == 2 &&
      (r[0xbcf] & 127) && !r[0x1f0c] && !r[0x1f23] && !r[0x1f48] &&
      (a <= 0x0a || a == 0x10 || a == 0x12 || a == 0x14 || a == 0x20);
}
static void end_air_dash(uint8_t *r) {
  if (!state.modern.dash_ticks) return;
  state.modern.dash_ticks = 0;
  r[0xbfd] = 0; r[0xc06] &= (uint8_t)~4;
  if (r[0xbaa] == 0x14) { r[0xbaa] = 8; r[0xbab] = 0; }
}
void MmxZeroMovementTick(uint8_t r[0x20000]) {
  if (!MmxZeroModern() || !r) return;
  if (!movement_playable(r)) { end_air_dash(r); return; }
  bool grounded = (r[0xbd3] & 4) != 0;
  unsigned action = r[0xbaa];
  /* Wall contact restores aerial options, like landing. A menu, weapon
   * change or attack cancellation never grants another midair action. */
  if (grounded || action == 0x10) {
    end_air_dash(r);
    state.modern.jump_used = state.modern.dash_used = 0;
  }
  bool air = !grounded && (action == 6 || action == 8 || state.modern.dash_ticks);
  if (air && (r[0xbe3] & 128) && !state.modern.jump_used &&
      (!terrain_query || !standing_blocked(r))) {
    end_air_dash(r); MmxZeroCancel(r);
    state.modern.jump_used = 1;
    r[0xbaa] = 6; r[0xbab] = 0; r[0xbfd] = 0;
    /* Same initial jump speed/gravity as X1/X3's native action-6 row at
     * $86:B9C3. The native initializer retains water and ceiling handling. */
    putword(r + 0xbc4, 0x0553); r[0xbc6] = 0x40;
    putword(r + 0xc04, 0x0178);
    r[0xbe3] &= (uint8_t)~128;
    return;
  }
  if (state.modern.dash_ticks && (!(r[0xbde] & 128) ||
      (r[0xbdf] & (state.modern.dash_facing ? 2 : 1)) || (r[0xc06] & 3)))
    end_air_dash(r);
  if (air && !state.modern.dash_ticks && !state.modern.dash_used &&
      (r[0xbe2] & 128)) {
    MmxZeroCancel(r);
    state.modern.dash_used = 1; state.modern.dash_ticks = 18;
    state.modern.dash_facing = (r[0xbdf] & 3) == 1 ? 64 :
        (r[0xbdf] & 3) == 2 ? 0 : r[0xc11] & 64;
    r[0xbaa] = 0x14; r[0xbab] = 0;
  }
  if (state.modern.dash_ticks) {
    /* Run the native dash controller and real terrain probes. Its vertical
     * position is stationary; only its ground precondition is synthesized. */
    r[0xc06] |= 4; r[0xc26] |= 64; r[0xbde] |= 128;
    r[0xbe3] &= (uint8_t)~128;
    r[0xc11] = state.modern.dash_facing;
    putword(r + 0xbc4, 0);
  }
}
void MmxZeroPlayerMotion(uint8_t r[0x20000], unsigned object) {
  if (!MmxZeroModern() || !r || object != 0xba8 || !state.modern.dash_ticks) return;
  putword(r + 0xc04, 0x0375);
  putword(r + 0xbc2, state.modern.dash_facing ? 0x0375 : -0x0375);
  r[0xbfa] = 0x20;
}
static bool start_slash(uint8_t *r) {
  if (modern_behavior && (r[0xbd3] & 4) && terrain_query && standing_blocked(r)) return false;
  unsigned d = free_projectile(r);
  if (!d) return false;
  end_air_dash(r);
  clear_charge(r); state.combo = state.saber_ready = 0; state.slash = 1;
  state.air = !(r[0xbd3] & 4); state.facing = r[0xc11] & 64;
  state.hit_slots = 0; state.projectile = (uint16_t)d;
  state.modern.slash_buffer = state.modern.hit_phase = 0;
  memset(r + d, 0, 64); r[d] = 1; r[d + 1] = 2;
  r[d + 10] = 3; r[d + 0x11] = r[0xbb9];
  putword(r + d + 0x3e, 0x5a53); ++r[0xbdd];
  return true;
}
static MmxZeroLegacyIntent legacy_intent_from_mapped(const uint8_t *r) {
  MmxZeroLegacyIntent intent = {
    (r[0xbdf] & 64) != 0,
    (r[0xbe3] & 64) != 0,
    (r[0xbdf] & 64) == 0,
  };
  const MmxZeroExtension *ext = live_extension();
  if (ext && ext->legacy_intent) {
    MmxZeroLegacyIntent override = intent;
    if (ext->legacy_intent(r, &override)) intent = override;
  }
  return intent;
}
void MmxZeroPlayerTick(uint8_t r[0x20000]) {
  if (!MmxZeroActive() || !r) return;
  unsigned action = r[0xbaa];
  bool stage = r[0xd1] == 2 && r[0xd2] == 4 && r[0xba9] == 2 &&
      r[0xd3]<10 && (r[0xbcf] & 127) && !r[0x1f0c] && !r[0xbdb];
  bool playable = stage &&
      /* Action $0A is the ordinary four-frame landing recovery, with native
       * fire/charge input still active ($81:8609..865A). */
      (action <= 0x0a || action == 0x10 || action == 0x12 || action == 0x14 || action == 0x20);
  /* As for X1, a hit ($0E) does not drop a held charge. It neither grows
   * nor fires during the knockback; the hold/release logic resumes after. */
  if (!playable && stage && action == 0x0e && state.charge &&
      !state.combo && !state.burst && !state.slash) return;
  if (!playable) { MmxZeroCancel(r); return; }
  bool pressed = (r[0xbe3] & 64) != 0;
  if (modern_behavior) {
    /* Movement takes priority over an ongoing direct swing. Preserve the
     * native jump and ladder inputs instead of masking them into a forced
     * pose. Air-dash/double-jump cancellation happens in MovementTick. */
    if(state.slash && ((r[0xbdf]&12) || ((r[0xbd3]&4) &&
        ((r[0xbe3]&128) || (r[0xbe2]&128))))) {
      MmxZeroCancel(r);
      r[0xbdf]&=(uint8_t)~64;r[0xbe3]&=(uint8_t)~64;
      return;
    }
    /* Direct saber uses the same ground/air art and collision arc. Retain
     * every pose, shortening only the long follow-through hold. */
    state.charge = state.charge_phase = state.combo = state.saber_ready = 0;
    state.burst = state.burst_end = 0;
    if (state.slash && pressed && state.slash >= 32) state.modern.slash_buffer = 6;
    if (state.slash) {
      if (++state.slash == 23) state.slash = 26;
      else if (state.slash == 32) state.slash = 37;
      if (state.slash > 44) {
        bool queued = state.modern.slash_buffer || pressed;
        MmxZeroCancel(r);
        if (r[0xbd3] & 4) r[0xbab] = 0;
        if (queued) start_slash(r);
      } else if (state.modern.slash_buffer) --state.modern.slash_buffer;
    } else if (pressed) start_slash(r);
    clear_charge(r);
    r[0xbdf] &= (uint8_t)~64; r[0xbe3] &= (uint8_t)~64;
    goto slash_update;
  }
  MmxZeroLegacyIntent intent = legacy_intent_from_mapped(r);
  if (state.charge >= 21 || (state.combo && state.saber_ready))
    state.charge_phase = (uint8_t)((state.charge_phase + 1) % 88);
  else state.charge_phase = 0;
  track_burst_shots(r);
  if (state.burst_end) {
    state.burst = state.burst_end = 0;
    if (r[0xbd3] & 4) r[0xbab] = 0; /* Restart the ordinary idle action. */
    else if (r[0xbaa] == 8) MmxZeroAnimationStart(0xba8,5 + r[0xc17]);
  }
  bool legacy_slash_started = false;
  const MmxZeroExtension *ext = live_extension();
  if (!state.slash && ext && ext->legacy_slash_request &&
      ext->legacy_slash_request(r)) {
    state.burst = state.burst_end = 0;
    legacy_slash_started = start_slash(r);
  }
  if (state.slash && !legacy_slash_started) {
    if (++state.slash > 44) {
      MmxZeroCancel(r);
      if (r[0xbd3] & 4) r[0xbab] = 0;
      return;
    }
  } else if (!state.burst && state.combo && intent.pressed) {
    if (state.combo == 1) {
      if (free_projectile(r) && !r[0x1f0d]) {
        state.combo = state.saber_ready ? 2 : 0;
        start_burst(r,2);
      }
    } else if (state.saber_ready && !state.shot_mask && !r[0xc25]) {
      start_slash(r);
    }
  } else if (!state.burst && !state.combo) {
    if (intent.held) {
      if (state.charge < legacy_charge_cap()) ++state.charge;
      /* Feed X1's ordinary charging visuals at Zero's measured thresholds.
       * Its release command is selected explicitly below; X1 special weapons
       * never enter this path and retain their own charge/upgrade rules. */
      if (state.charge == 21) r[0xbff] = 0x97;
      else if (state.charge > 21 && state.charge < 81) r[0xbff] = 0x90;
      else if (state.charge == 81) r[0xbff] = 0x51;
      else if (state.charge > 81) r[0xbff] = 0x40;
      if (state.charge >= 141) { r[0xc03] = 1; r[0xc2a] = 4; }
    } else if (intent.released) {
      unsigned tier = MmxZeroChargeTier(&state);
      if (tier >= 8 && free_projectile(r) && !r[0x1f0d]) {
        state.combo = 1; state.saber_ready = tier == 10;
        start_burst(r,1);
      } else if (tier >= 4) {
        /* X1 $81:94BF indexes $86:BA76 by command/2: command 2
         * is class 1 (green half-shot); command 6 is class 2. */
        clear_charge(r); r[0xc01] = tier >= 6 ? 8 : 2;
      }
      state.charge = 0;
    }
  }
  if (state.burst) {
    state.held_vy = (uint16_t)word(r + 0xbc4);
    advance_burst(r);
    clear_charge(r);
    /* The ground firing sequence plants Zero's feet; jumping still transfers
     * to its airborne counterpart. Native air control/gravity/collision run. */
    if (!state.air) {
      r[0xbde] = r[0xbe2] = 0;
      if (action != 0) { r[0xbaa] = 0; r[0xbab] = 0; }
    } else {
      /* X3's firing action keeps integrating through the apex, instead of
       * entering X1's ordinary fall initializer (which resets VY to zero). */
      r[0xbdf] |= 128;
      if (r[0xbaa] == 6 && (int16_t)word(r + 0xbc4) < 0) {
        r[0xbaa] = 8; r[0xbab] = 2;
        MmxZeroAnimationStart(0xba8,4 + r[0xc17]);
      }
      /* Second-shot initialization and the terminal action-return frame do
       * not integrate motion in X3.
       * Keep native terrain probes at the unchanged position, then restore
       * the velocity after the native update for next frame's continuation. */
      if (burst_holds_air()) {
        state.held_vy = (uint16_t)word(r + 0xbc4); state.held_gravity = r[0xbc6];
        putword(r + 0xbc4,0); r[0xbc6] = 0; r[0xbde] = r[0xbe2] = 0;
      }
    }
    r[0xc11] = state.facing;
  }
  if (state.combo || state.burst) {
    r[0xbdf] &= (uint8_t)~64; r[0xbe3] &= (uint8_t)~64;
  }
slash_update:
  if (state.slash) {
    if (modern_behavior) {
      state.air = !(r[0xbd3] & 4);
      /* Two contact windows, nine ticks apart. Native enemy/boss immunity
       * and reactions still decide whether the second contact can land. */
      if (state.slash >= 16 && !state.modern.hit_phase) {
        state.modern.hit_phase = 1; state.hit_slots = 0;
      }
    }
    r[0xbde] = 0; r[0xbe2] = r[0xbe3] = 0;
    if (modern_behavior && state.air) {
      /* Keep native left/right air control and jump-height input. The jump
       * controller rebuilds VX from these inputs at $81:9982; masking them
       * plants Zero horizontally even if his previous VX is preserved. */
      r[0xbdf] &= 131;
    } else {
      r[0xbdf] &= 128;
      r[0xbc2] = r[0xbc3] = 0;
    }
    r[0xc11] = state.facing; clear_charge(r);
    if (!state.air && action != 0) { r[0xbaa] = 0; r[0xbab] = 0; }
    unsigned d = state.projectile;
    if (own_projectile(r, d)) {
      putword(r + d + 5, word(r + 0xbad));
      putword(r + d + 8, word(r + 0xbb0));
      unsigned age = state.slash >= 7 ? state.slash - 7 : 99;
      unsigned phase = age < 12 ? age / 3 : 4;
      putword(r + d + 0x20, age < 19 ? 0xffb0 + (state.air ? 20 : 0) + phase * 4 : 0);
      r[d + 0x30] = 0;
    }
  }
}
void MmxZeroPlayerEnd(uint8_t r[0x20000]) {
  if (MmxZeroModern() && r && state.slash && own_projectile(r,state.projectile)) {
    /* Air steering has now moved/turned the body. Keep the invisible melee
     * object attached to the position and facing that will actually render,
     * before the native enemy collision pass. BB9's facing is copied from
     * C11 later at $81:8194, so use C11 directly here. */
    unsigned d = state.projectile;
    state.facing = r[0xc11] & 64; state.air = !(r[0xbd3] & 4);
    putword(r + d + 5, word(r + 0xbad));
    putword(r + d + 8, word(r + 0xbb0));
    r[d + 0x11] = (r[d + 0x11] & (uint8_t)~64) | state.facing;
    unsigned age = state.slash >= 7 ? state.slash - 7 : 99;
    unsigned phase = age < 12 ? age / 3 : 4;
    putword(r + d + 0x20, age < 19 ? 0xffb0 + (state.air ? 20 : 0) + phase * 4 : 0);
  }
  if (MmxZeroModern() && r && state.modern.dash_ticks) {
    r[0xc06] &= (uint8_t)~4;
    if (r[0xbaa] != 0x14 || (r[0xbd3] & 4) || state.modern.dash_ticks == 1)
      end_air_dash(r);
    else --state.modern.dash_ticks;
  }
  if (poses && r && state.burst) {
    if (!state.air || burst_holds_air()) putword(r + 0xbc4,state.held_vy);
    if (burst_holds_air()) r[0xbc6] = state.held_gravity;
  }
}
unsigned MmxZeroWeaponTick(uint8_t r[0x20000], unsigned d, unsigned active) {
  unsigned value = active;
  if (own_projectile(r, d)) {
    if (!poses || d != state.projectile || !state.slash) {
      memset(r + d, 0, 64); if (r[0xbdd]) --r[0xbdd];
    }
    value = 0; /* Our transient melee object has no native projectile update. */
  }
  const MmxZeroExtension *ext = live_extension();
  return ext && ext->weapon_tick ?
      ext->weapon_tick(r, d, value) : value;
}
unsigned MmxZeroResponse(uint8_t *r, unsigned enemy, unsigned projectile,
                         unsigned original) {
  const MmxZeroExtension *ext = live_extension();
  return ext && ext->response ?
      ext->response(r, enemy, projectile, original) : original;
}
unsigned MmxZeroDamage(uint8_t r[0x20000], unsigned enemy, unsigned projectile, unsigned original) {
  unsigned value = original;
  if (poses && own_projectile(r, projectile) && projectile == state.projectile &&
      enemy >= 0xe68 && enemy < 0x1228 && (enemy & 63) == 0x28 && original && !(original & 128)) {
    unsigned bit = 1u << ((enemy - 0xe68) / 64);
    if (state.hit_slots & bit) value = 0;
    else {
      state.hit_slots |= (uint16_t)bit;
      value = modern_behavior ? 3 : 16;
    }
  }
  const MmxZeroExtension *ext = live_extension();
  return ext && ext->damage ?
      ext->damage(r, enemy, projectile, value) : value;
}
unsigned MmxZeroHitbox(const uint8_t r[0x20000], unsigned enemy, unsigned projectile, unsigned original) {
  unsigned value = original;
  if (poses && own_projectile(r, projectile) && projectile == state.projectile &&
      enemy >= 0xe68 && enemy < 0x1228 && (enemy & 63) == 0x28 &&
      (state.hit_slots & (1u << ((enemy - 0xe68) / 64)))) value = 0;
  const MmxZeroExtension *ext = live_extension();
  return ext && ext->hitbox ?
      ext->hitbox(r, enemy, projectile, value) : value;
}
