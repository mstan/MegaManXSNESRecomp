#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MmxWeaponShot {
  int32_t x, y; /* 16.8 world coordinates. */
  int16_t vx, vy;
  uint16_t age, animation, hit_slots, born;
  int16_t origin_x, origin_y;
  uint8_t page, weapon, group, pose, timer, flags, facing, variant;
  uint8_t charged, radius, active, reserved;
  uint8_t tether_pose, muzzle_pose;
} MmxWeaponShot;
/* X1 has integer HP; retain thirds so source ratios do not round every ray
 * up or turn every sub-buster contact into a full buster hit. */
typedef struct MmxWeaponDamageState {
  uint8_t remainder, kind, hp, active;
} MmxWeaponDamageState;
#define MMX_WEAPON_COMBAT_LEGACY_SIZE 328u
#define MMX_WEAPON_COMBAT_DAMAGE_SIZE 388u
typedef struct MmxWeaponCombatState {
  MmxWeaponShot shots[8];
  uint16_t tick;
  uint8_t stage, valid, held, pressed, direction, reserved;
  MmxWeaponDamageState enemies[15];
  MmxWeaponShot effects[16]; /* Particles/utility victims do not consume native hit slots. */
} MmxWeaponCombatState;
MmxWeaponCombatState MmxWeaponsGetCombatState(void);
bool MmxWeaponsValidCombatState(const MmxWeaponCombatState *state);
void MmxWeaponsSetCombatState(MmxWeaponCombatState state);
/* Optional second actor; the caller owns this stable snapshot. NULL restores
 * single-player behavior. World enemy captures/time effects consider both. */
void MmxWeaponsPartnerCombat(const MmxWeaponCombatState *state);
/* Derived per-owner view for imported projectile lifetimes. NULL is native. */
void MmxWeaponsCameraQuery(unsigned (*query)(const uint8_t *,bool));
unsigned MmxWeaponsTimePhase(const MmxWeaponCombatState *state);
/* Live X1 terrain, available even when no imported weapon pack is enabled. */
unsigned MmxWeaponsTerrainClass(const uint8_t ram[0x20000],int x,int y);
bool MmxWeaponsTerrainSolid(const uint8_t ram[0x20000],int x,int y,bool floor,int *surface);
bool MmxWeaponsCombatActive(void);
void MmxWeaponsPlayerTick(uint8_t ram[0x20000]);
void MmxWeaponsPlayerMotion(uint8_t ram[0x20000],unsigned object);
bool MmxWeaponsFrameTick(uint8_t ram[0x20000]);
bool MmxWeaponsTimeActive(void);
bool MmxWeaponsFrozenEnemy(const MmxWeaponCombatState *state,unsigned object);
const MmxWeaponShot *MmxWeaponsMovedEnemy(const MmxWeaponCombatState *state,unsigned object);
unsigned MmxWeaponsEnemyActive(const uint8_t ram[0x20000],unsigned object,unsigned active);
void MmxWeaponsTimeRipple(const MmxWeaponCombatState *state,int16_t lines[224]);
void MmxWeaponsTerrainEnd(uint8_t ram[0x20000],unsigned object);
void MmxWeaponsMarkShot(uint8_t ram[0x20000], unsigned slot);
void MmxWeaponsSelectShot(const uint8_t ram[0x20000], unsigned slot);
unsigned MmxWeaponsProjectileTick(uint8_t ram[0x20000], unsigned slot, unsigned active);
void MmxWeaponsCancelShots(uint8_t ram[0x20000]);
void MmxWeaponsCollisionRom(uint8_t *rom, size_t size);
/* Per-contact native table index, or the separately published reaction class.
 * The actual projectile ID, graphics, hit cadence and movement stay intact. */
unsigned MmxWeaponsContactClass(const uint8_t *ram,unsigned enemy,unsigned projectile,unsigned original,bool reaction);
/* Which weapon fired a player shot: page 0 X1 (Homing Torpedo = 1, X1 menu
 * order), 1 X2, 2 X3 by native weapon id. False for buster or no shot. */
bool MmxWeaponsShotIdentity(const uint8_t *ram,unsigned shot,unsigned *page,unsigned *weapon,unsigned *charged);
unsigned MmxWeaponsDamage(uint8_t ram[0x20000], unsigned enemy, unsigned projectile, unsigned original);
unsigned MmxWeaponsHitbox(const uint8_t ram[0x20000], unsigned enemy, unsigned projectile, unsigned original);
