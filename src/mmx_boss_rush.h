#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Ids 0..7 are X1's native Mavericks; 8..15 are X2's and 16..23 X3's, each
 * game ordered by its native weapon id. Only X1 bosses have native bodies. */
enum { MMX_RUSH_BOSSES=8, MMX_RUSH_ROSTER=24, MMX_RUSH_SEATS=2, MMX_RUSH_HP=32 };
enum { MMX_RUSH_X1_MASK=0xff, MMX_RUSH_X2_MASK=0xff00, MMX_RUSH_X3_MASK=0xff0000 };
enum { MMX_RUSH_OFF, MMX_RUSH_LOADING, MMX_RUSH_PLAYING, MMX_RUSH_FINISHED,
  MMX_RUSH_PREPARING };
enum { MMX_RUSH_EMPTY, MMX_RUSH_ENTERING, MMX_RUSH_FIGHTING, MMX_RUSH_DYING };
enum { MMX_RUSH_NOT_LOADED, MMX_RUSH_ARENA_LOADED, MMX_RUSH_MUSIC_PENDING };
/* $87:9252 opens Mammoth's camera another $70 pixels. The encounter trigger
 * and Penguin's hook share the center of the full 368-pixel chamber. */
enum { MMX_RUSH_MAMMOTH_SCROLL=0x70,
  MMX_RUSH_ARENA_CENTER_X=(256+MMX_RUSH_MAMMOTH_SCROLL)/2 };
typedef struct MmxBossRushBoss {
  uint16_t object, ticks;
  /* reserved: frames of a relocated boss's own death sequence that precede
   * Rush's explosion (0 = none). */
  uint8_t id, phase, health, reserved;
  uint32_t generation;
  int16_t death_x, death_y;
  uint8_t globals[40];
} MmxBossRushBoss;
/* Fixed-width, pointer-free state: snapshots/rollback own the entire queue,
 * object ownership and pending actor projection, including interrupted calls. */
typedef struct MmxBossRushState {
  uint32_t random, defeated, generation;
  MmxBossRushBoss bosses[2];
  uint8_t queue[8], queued, mode, coop, loaded;
  uint8_t stage_started, return_title;
  uint16_t input, menu_input;
  uint8_t menu, selection, result_selection, actor;
  uint16_t actor_object, actor_s;
  uint8_t globals[40], occupied[128], owner[128];
  uint16_t empty_tile, solid_tile, camera_x, camera_y;
} MmxBossRushState;
/* Chameleon's native fades have history, independent of the arena CGRAM.
 * Keep it outside the original Rush ABI so older snapshots remain readable. */
typedef struct MmxBossRushVisualState {
  uint32_t generation[2];
  uint16_t colors[2][16];
  uint8_t initialized[2], reserved[2];
} MmxBossRushVisualState;
MmxBossRushVisualState MmxBossRushVisualGetState(void);
bool MmxBossRushVisualValidState(const MmxBossRushVisualState *state);
bool MmxBossRushVisualSetState(const MmxBossRushVisualState *state);
/* direction: 0 initializes black, +1 restores colors, -1 fades to black. */
void MmxBossRushVisualFade(unsigned owner, uint32_t generation,
    const uint16_t target[16], int direction);
/* Work RAM shared by a relocated boss's objects (its scratch window, see
 * MmxBossRushForeignSetScratch): each seat's private bytes, and X1's own
 * bytes while a seat's copy is swapped in (`active` = seat + 1). */
typedef struct MmxBossRushScratchState {
  uint8_t seat[2][16], world[16];
  uint8_t active, reserved[3];
} MmxBossRushScratchState;
MmxBossRushScratchState MmxBossRushScratchGetState(void);
bool MmxBossRushScratchValidState(const MmxBossRushScratchState *state);
bool MmxBossRushScratchSetState(const MmxBossRushScratchState *state);
/* Rotate colors 1..15 forward by one (15 wraps to 1), as Tunnel Rhino's
 * drill charge does to its OBJ palette; initialized from target. */
void MmxBossRushVisualRotate(unsigned owner,uint32_t generation,const uint16_t target[16]);
/* The legacy queue in MmxBossRushState only holds X1 ids. Runs started by
 * this build shuffle this roster instead; the legacy fields stay zero so the
 * original Rush ABI, snapshots and render captures remain readable. The
 * available set is fixed when a run starts, so launcher edits cannot change
 * a run in progress or diverge between netplay peers. */
typedef struct MmxBossRushRosterState {
  uint32_t available;
  uint8_t queue[MMX_RUSH_ROSTER], queued, reserved[3];
} MmxBossRushRosterState;
MmxBossRushRosterState MmxBossRushRosterGetState(void);
bool MmxBossRushRosterValidState(const MmxBossRushRosterState *state);
bool MmxBossRushRosterSetState(const MmxBossRushRosterState *state);
/* Snapshots from before the roster carry only the legacy X1 queue. */
MmxBossRushRosterState MmxBossRushRosterFromLegacy(const MmxBossRushState *state);
/* An active run needs a roster that contains both of its present bosses. */
bool MmxBossRushRosterMatches(const MmxBossRushState *state,const MmxBossRushRosterState *roster);
typedef struct MmxBossRushDefinition {
  const char *name;
  uint32_t controller;  /* body controller in its source game's ROM */
  uint8_t kind, stage;  /* source class; X1 resource stage (X1 bosses only) */
  uint8_t game;         /* 1, 2 or 3 */
} MmxBossRushDefinition;
extern const MmxBossRushDefinition kMmxBossRushBosses[MMX_RUSH_ROSTER];
static inline bool MmxBossRushNative(unsigned id) {return id<MMX_RUSH_BOSSES;}
/* Session mod selection controls title-menu access. An already active run
 * remains snapshot-owned until it ends or the game resets. */
void MmxBossRushSetEnabled(bool enabled);
bool MmxBossRushEnabled(void);
/* Bosses offered to the next run: X1 always, plus each X2/X3 boss whose
 * pack is enabled with its source ROM and whose body is implemented. */
void MmxBossRushSetOffered(uint32_t mask);
uint32_t MmxBossRushOffered(void);
void MmxBossRushReset(void);
bool MmxBossRushActive(void);
bool MmxBossRushCoop(void);
void MmxBossRushStart(bool coop, uint32_t seed);
MmxBossRushState MmxBossRushGetState(void);
bool MmxBossRushValidState(const MmxBossRushState *state);
bool MmxBossRushSetState(const MmxBossRushState *state);
/* The arena belt runs only while a living Mammoth is present. */
bool MmxBossRushConveyorRunning(const MmxBossRushState *state);
int MmxBossRushNextBoss(void);
bool MmxBossRushAssign(unsigned slot, unsigned id, uint16_t object);
bool MmxBossRushDefeat(unsigned slot);
void MmxBossRushFinish(void);
int MmxBossRushOwner(unsigned object);
void MmxBossRushSetOwner(unsigned object, int owner);
/* Runtime integration is USA-only; the state machine itself needs no ROM. */
void MmxBossRushHostFrame(void);
uint16_t MmxBossRushFilterInput(uint16_t input);
void MmxBossRushFrame(uint8_t ram[0x20000], uint16_t input);
void MmxBossRushAfterFrame(uint8_t ram[0x20000]);
void MmxBossRushDraw(uint32_t *pixels,int width,int extra,const MmxBossRushState *state);
