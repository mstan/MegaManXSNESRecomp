#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* X2/X3 Mavericks for Boss Rush. Their packs hold the user's validated source
 * ROM in memory only; nothing derived from it is written or distributed.
 * A boss is offered to new runs only when its game's pack is loaded and its
 * body is implemented, so X1-only sessions keep the original roster. */
bool MmxBossRushForeignLoad(unsigned game,const char *rom,char *error,size_t error_size);
void MmxBossRushForeignUnload(void);
bool MmxBossRushForeignReady(unsigned game);
const uint8_t *MmxBossRushForeignRom(unsigned game,size_t *size);
/* Roster ids (8..23) whose bodies exist in this build. */
uint32_t MmxBossRushForeignImplemented(void);
/* Body controller built in a cart extension bank for a roster id, or 0.
 * Registering one marks that body implemented; 0 removes it. */
bool MmxBossRushForeignSetController(unsigned id,uint32_t controller);
uint32_t MmxBossRushForeignController(unsigned id);
/* Body states (+$01) that mean the entrance finished and that it is dying;
 * X1 bodies use 4 and 6. Defaults match X1 until a body registers its own. */
bool MmxBossRushForeignSetStates(unsigned id,uint8_t fighting,uint8_t dying);
void MmxBossRushForeignStates(unsigned id,uint8_t *fighting,uint8_t *dying);
/* Controllers for the boss's own projectile classes (enemy-projectile list). */
enum { MMX_RUSH_FOREIGN_CHILDREN=4 };
bool MmxBossRushForeignSetChild(unsigned id,uint8_t kind,uint32_t controller);
uint32_t MmxBossRushForeignChild(unsigned id,uint8_t kind);
/* Shim addresses standing in for the source game's sound-effect calls
 * (plain and positional); the runtime hooks them to queue cached clips. */
void MmxBossRushForeignSetSoundShims(uint32_t plain,uint32_t positional);
void MmxBossRushForeignSoundShims(uint32_t *plain,uint32_t *positional);
/* Weaknesses of a relocated body against one weapon (page 0 X1, 1 X2, 2 X3;
 * see MmxWeaponsShotIdentity). `damage` picks the X1 damage column in its
 * row; `reaction` is the class its own weakness test sees in $1F1D. */
typedef struct MmxBossRushWeakness {
  uint8_t page,weapon,damage[2],reaction[2]; /* [normal, charged] */
} MmxBossRushWeakness;
bool MmxBossRushForeignSetWeaknesses(unsigned id,const MmxBossRushWeakness *rules,unsigned count);
const MmxBossRushWeakness *MmxBossRushForeignWeakness(unsigned id,unsigned page,unsigned weapon);
/* Relocated palette helpers Rush runs privately per seat: one rotates the
 * body's colors, the other fades them back (carry set while unfinished). */
void MmxBossRushForeignSetPaletteHooks(uint32_t rotate,uint32_t fade);
void MmxBossRushForeignPaletteHooks(uint32_t *rotate,uint32_t *fade);
/* Relocated `LDA $05`/`EOR $05` reads where an X2/X3 body tests which half of its
 * room it is in: their rooms start on 256-pixel boundaries, so bit 7 of the
 * X low byte is the midline. Rush answers them relative to its arena. */
void MmxBossRushForeignSetMidlineReads(const uint32_t *pcs,unsigned count);
unsigned MmxBossRushForeignMidlineReads(const uint32_t **pcs);
/* X2/X3 bodies keep a count of their live children and assume every spawn
 * succeeds; in their own games the pool never runs out, but in Rush two
 * bosses share X1's eight projectile slots. A guard at a relocated `BNE`
 * after a slot allocation takes one child out of field `counter` when the
 * allocation failed. A guard at a relocated `STA counter` (a count stored
 * after a spawn loop) subtracts field `remaining`, the children the loop
 * could not spawn, from the stored value. */
typedef struct MmxBossRushSpawnGuard {
  uint32_t pc;
  uint8_t counter, remaining;
} MmxBossRushSpawnGuard;
void MmxBossRushForeignSetSpawnGuards(const MmxBossRushSpawnGuard *guards,unsigned count);
unsigned MmxBossRushForeignSpawnGuards(const MmxBossRushSpawnGuard **guards);
/* X3 has a "caught" player state that X1 lacks: relocated bodies see X1's
 * ordinary hurt state in its place, and the caught player stays tangible.
 * A child guard ties such code to the child that does the catching (a live
 * child of `kind` owned by the body, whose field `field` holds `value`):
 * on a relocated `LDA player-state` caught test it reports "not caught"
 * (A=0) without that child; on a relocated `JSR spawn` it skips the call
 * while that child is already alive, so one catch spawns one child. */
typedef struct MmxBossRushChildGuard {
  uint32_t pc;
  uint8_t kind, field, value;
} MmxBossRushChildGuard;
void MmxBossRushForeignSetChildGuards(const MmxBossRushChildGuard *guards,unsigned count);
unsigned MmxBossRushForeignChildGuards(const MmxBossRushChildGuard **guards);
/* Rebuild every relocated body for the loaded source ROMs into the cart's
 * patch windows (mmx_boss_rush_relocate.c). Restores the cart first. */
bool MmxBossRushForeignBuild(char *error,size_t error_size);
/* After a pack loads or unloads, rebuild or restore the cart once the game
 * image exists. Called from the host frame; idempotent. */
void MmxBossRushForeignSync(void);
/* Bosses the next run may draw from: X1 plus every loaded body that is
 * finished (art, sound, weaknesses). Preview also offers unfinished bodies. */
uint32_t MmxBossRushForeignOffered(void);
void MmxBossRushForeignSetPreview(bool enabled);
