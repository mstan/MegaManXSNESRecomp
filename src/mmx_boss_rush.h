#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { MMX_RUSH_BOSSES=8, MMX_RUSH_SEATS=2, MMX_RUSH_HP=32 };
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
  uint8_t stage_started, return_title, versus;
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
typedef struct MmxBossRushDefinition {
  const char *name;
  uint32_t controller;
  uint8_t kind, stage;
} MmxBossRushDefinition;
extern const MmxBossRushDefinition kMmxBossRushBosses[8];
/* Session mod selection controls title-menu access. An already active run
 * remains snapshot-owned until it ends or the game resets. */
void MmxBossRushSetEnabled(bool enabled);
bool MmxBossRushEnabled(void);
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
