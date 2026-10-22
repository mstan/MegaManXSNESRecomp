#pragma once
#include <stdbool.h>
#include <stdint.h>
struct CpuState;

enum { MMX_VS_OFF, MMX_VS_SELECT, MMX_VS_LOADING, MMX_VS_ENTRANCE,
       MMX_VS_FIGHT, MMX_VS_RESULT };
enum { MMX_VS_IDLE, MMX_VS_PRIMARY, MMX_VS_SPECIAL, MMX_VS_ABILITY,
       MMX_VS_JUMP, MMX_VS_CEILING, MMX_VS_TELEPORT };
enum { MMX_VS_NO_WINNER=2, MMX_VS_HP=32 };
typedef struct MmxVsFighter {
  uint16_t input, pressed, ticks, invulnerable, stun, lockout;
  int16_t target_x, target_y, jump_peak, ground_y, vx, vy;
  uint16_t effect_ticks, capture_ticks, motion_ticks;
  uint8_t boss, hp, action, native_action, native_sub, started;
  uint8_t facing, grounded, flying, attached, armor_broken, captured;
  uint8_t last_damage, last_class;
} MmxVsFighter;
/* No pointers or host presentation choices: the complete match is replayable. */
typedef struct MmxVsState {
  uint32_t score[2], round;
  MmxVsFighter fighters[2];
  uint16_t input[2], release[2], ticks;
  uint8_t phase, first, picker, locked, cursor[2], winner, paused, pause_choice, advance, shake_ticks, exit_requested;
  uint8_t projected, proxy, proxy_seat, proxy_hp, proxy_row;
  uint16_t proxy_source;
  uint8_t attack_class[128];
  uint8_t saved_player[0x90], saved_proxy[64];
} MmxVsState;

void MmxVsSetEnabled(bool enabled);
bool MmxVsEnabled(void);
bool MmxVsActive(void);
void MmxVsReset(void);
void MmxVsStart(void);
MmxVsState MmxVsGetState(void);
bool MmxVsValidState(const MmxVsState *state);
bool MmxVsSetState(const MmxVsState *state);
bool MmxVsChoose(unsigned seat, unsigned boss);
void MmxVsResolveRound(unsigned hp1, unsigned hp2);
void MmxVsNextRound(void);
/* X1 damage class belongs to the particular attack, never the boss body. */
unsigned MmxVsAttackClass(unsigned boss, unsigned action, bool projectile);
bool MmxVsPickVisible(const MmxVsState *state, unsigned viewer, unsigned seat, bool online);

/* ROM runtime. true freezes the guest world for selection/results/pause. */
bool MmxVsFrame(uint8_t ram[0x20000], uint16_t p1, uint16_t p2);
void MmxVsAfterFrame(uint8_t ram[0x20000]);
void MmxVsHostFrame(void);
void MmxVsActorBegin(uint8_t ram[0x20000], unsigned seat);
void MmxVsActorEnd(uint8_t ram[0x20000], unsigned seat);
void MmxVsPrepareArena(uint8_t ram[0x20000]);
void MmxVsSpawned(uint8_t ram[0x20000], unsigned seat);
void MmxVsChildAllocated(const uint8_t ram[0x20000], unsigned object, unsigned parent, unsigned seat);
bool MmxVsNativeHook(struct CpuState *cpu, uint32_t pc);
const char *MmxVsBossName(unsigned boss);
