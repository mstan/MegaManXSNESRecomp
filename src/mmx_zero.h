#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MMX_ZERO_WIDTH = 128, MMX_ZERO_HEIGHT = 128, MMX_ZERO_POSES = 152, MMX_ZERO_CHARGE_POSES = 66,
       MMX_ZERO_ANIMATION_BYTES = 0x474, MMX_ZERO_MUZZLE_BYTES = 196,
       MMX_ZERO_LEGACY_STATE_SIZE = 12, MMX_ZERO_ANIMATION_STATE_SIZE = 18,
       MMX_ZERO_COMBAT_STATE_SIZE = 30, MMX_ZERO_SWAP_STATE_SIZE = 36,
       MMX_ZERO_HEALTH_STATE_SIZE = 40 };
typedef struct MmxZeroModernState {
  uint8_t enabled, jump_used, dash_used, dash_ticks;
  uint8_t dash_facing, slash_buffer, hit_phase, reserved;
} MmxZeroModernState;
typedef struct MmxZeroLegacyIntent {
  bool held, pressed, released;
} MmxZeroLegacyIntent;
typedef struct MmxZeroExtension {
  void (*pre_player)(uint8_t *ram);                 /* $815C, before SlideTick */
  void (*player_end)(uint8_t *ram);                 /* $8165, after MmxZeroPlayerEnd */
  unsigned (*weapon_tick)(uint8_t *ram, unsigned d, unsigned value);
  unsigned (*damage)(uint8_t *ram, unsigned d, unsigned x, unsigned value);
  unsigned (*hitbox)(const uint8_t *ram, unsigned d, unsigned x, unsigned value);
  bool (*legacy_intent)(const uint8_t *ram, MmxZeroLegacyIntent *intent); /* true = override */
  unsigned (*charge_cap)(void);                       /* 0 = no cap */
  void (*collision_rom)(uint8_t *rom, size_t size);
  void (*state_reset)(uint8_t *ram);
  bool (*legacy_slash_request)(const uint8_t *ram);
  unsigned (*response)(uint8_t *ram, unsigned enemy, unsigned projectile,
                       unsigned value);
  int (*burst_origin_y)(const uint8_t *ram, unsigned shot_index,
                        int native_y, int paired_y);
  /* Savestate and rollback: the owner's host-side state as a fixed blob.
   * Load must ignore a blob it did not write (e.g. all zeros). */
  void (*state_save)(uint8_t out[]);
  void (*state_load)(const uint8_t in[]);
} MmxZeroExtension;
enum { MMX_ZERO_EXTENSION_STATE_BYTES = 2048 };
typedef struct MmxZeroState {
  uint16_t charge, slash, projectile;
  /* Reuses the formerly unused cooldown byte without changing save layout. */
  uint8_t combo, charge_phase, air, facing;
  uint16_t hit_slots;
  uint16_t anim_offset;
  uint8_t anim_timer, anim_pose, anim_flags, anim_valid;
  uint16_t burst_offset;
  uint8_t burst_timer, burst, burst_end, saber_ready, shot_mask, held_gravity;
  uint16_t held_vy;
  uint8_t burst_transition, burst_fired;
  /* Zero remains the default for legacy saves and initial mod activation. */
  uint8_t active_x, swap_phase, swap_tick, swap_fraction;
  int16_t swap_y;
  uint8_t hp[2], hp_valid, hp_max; /* Index 0 = Zero, 1 = X; shared maximum. */
  MmxZeroModernState modern;
} MmxZeroState;
/* The extension belongs to its owner and survives MmxZeroDisable() and
 * MmxZeroResetState(); the owner must clear it with MmxZeroSetExtension(NULL)
 * during its own reset. */
void MmxZeroSetExtension(const MmxZeroExtension *ext);
/* Co-op runs the player routine once per seat. The gate says whether the
 * current seat owns the extension; NULL means it always does. Per-frame and
 * per-hit callbacks are skipped while the gate is false, leaving the owner's
 * state untouched for its own seat. */
void MmxZeroSetExtensionGate(bool (*gate)(void));
void MmxZeroExtPrePlayer(uint8_t *ram);
/* Zero-filled when no extension is set. Load runs after MmxZeroSetState. */
void MmxZeroExtSaveState(uint8_t out[MMX_ZERO_EXTENSION_STATE_BYTES]);
void MmxZeroExtLoadState(const uint8_t in[MMX_ZERO_EXTENSION_STATE_BYTES]);
void MmxZeroExtPlayerEnd(uint8_t *ram);
bool MmxZeroLoad(const char *path);
void MmxZeroDisable(void);
bool MmxZeroEnabled(void);
bool MmxZeroActive(void);
bool MmxZeroSwapping(void);
void MmxZeroHealthSync(const uint8_t ram[0x20000]);
void MmxZeroHealthRespawn(const uint8_t ram[0x20000]);
/* Called after native NMI input polling; true suspends the game scheduler. */
bool MmxZeroSwapTick(uint8_t ram[0x20000]);
unsigned MmxZeroSwapPose(const MmxZeroState *snapshot);
const uint8_t *MmxZeroTeleportPose(unsigned pose);
int MmxZeroPoseOffsetY(const uint8_t ram[0x20000]);
const uint8_t *MmxZeroPose(const uint8_t ram[0x20000], const MmxZeroState *snapshot);
const uint8_t *MmxZeroBlade(const MmxZeroState *snapshot);
const uint16_t *MmxZeroColors(void);
int MmxZeroChargeFlashPaletteIndex(const MmxZeroState *snapshot);
const uint16_t *MmxZeroBodyColors(const MmxZeroState *snapshot);
const uint8_t *MmxZeroChargePose(const MmxZeroState *snapshot);
bool MmxZeroHasChargeArt(void);
void MmxZeroDeathOrbSpawn(uint8_t ram[0x20000], unsigned source, unsigned orb);
bool MmxZeroDeathOrbRed(const uint8_t ram[0x20000], unsigned orb);
bool MmxZeroNativeChargeObject(unsigned object, unsigned kind);
const uint8_t *MmxZeroMenuPose(void);
/* Original X3 BGR555 badge pixel; -2 is transparent, -1 retains native art. */
int MmxZeroHudColor(unsigned x, unsigned y);
void MmxZeroSetCollisionRom(uint8_t *rom, size_t size);
/* Terrain solidity for wall/ceiling probes (not one-way floors or slopes). */
void MmxZeroSetTerrainQuery(bool (*solid)(const uint8_t *ram, int x, int y));
/* True at a dash exit ($81:898E/8999/8965) when Zero must keep sliding. */
bool MmxZeroSlideHold(uint8_t ram[0x20000], unsigned pc);
/* Before the player state dispatch: re-enter the dash if Zero cannot stand. */
void MmxZeroSlideTick(uint8_t ram[0x20000]);
unsigned MmxZeroUpgradeBits(unsigned pc, unsigned original);
void MmxZeroPlayerTick(uint8_t ram[0x20000]);
void MmxZeroPlayerEnd(uint8_t ram[0x20000]);
unsigned MmxZeroChargeTier(const MmxZeroState *snapshot);
void MmxZeroAnimationStart(unsigned object, unsigned sequence);
void MmxZeroAnimationAdvance(unsigned object);
unsigned MmxZeroMuzzle(const uint8_t ram[0x20000], unsigned object,
                      unsigned native_index, unsigned axis, unsigned original);
unsigned MmxZeroWeaponOrigin(const uint8_t ram[0x20000], unsigned object,
                             unsigned axis, unsigned original);
unsigned MmxZeroWeaponTick(uint8_t ram[0x20000], unsigned object, unsigned active);
unsigned MmxZeroResponse(uint8_t *ram, unsigned enemy, unsigned projectile,
                         unsigned original);
unsigned MmxZeroDamage(uint8_t ram[0x20000], unsigned enemy, unsigned projectile, unsigned original);
unsigned MmxZeroHitbox(const uint8_t ram[0x20000], unsigned enemy, unsigned projectile, unsigned original);
MmxZeroState MmxZeroGetState(void);
bool MmxZeroValidState(const MmxZeroState *state);
void MmxZeroSetState(MmxZeroState state);
/* A co-op seat exchange: as MmxZeroSetState, but the extension keeps its
 * state, which follows the seat it belongs to rather than the live seat. */
void MmxZeroSelectState(MmxZeroState state);
void MmxZeroResetState(void);
/* Start new sessions as X instead of Zero; both remain exchangeable. */
void MmxZeroSetStartCharacter(bool x);
/* Shared launcher preference; X3 behavior remains the default. */
void MmxZeroSetModern(bool enabled);
bool MmxZeroModern(void);
void MmxZeroMovementTick(uint8_t ram[0x20000]);
void MmxZeroPlayerMotion(uint8_t ram[0x20000], unsigned object);
void MmxZeroCancel(uint8_t ram[0x20000]);
void MmxZeroRegisterHooks(void);
