#pragma once
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "mmx_weapon_combat.h"
#include <string.h>

enum { MMX_COOP_X, MMX_COOP_ZERO };
enum { MMX_COOP_ABSENT, MMX_COOP_ALIVE, MMX_COOP_FALLEN };

/* Native routines keep their original addresses. Only these owned ranges are
 * projected into WRAM when a different player runs; world/task RAM stays live.
 * $1F83..86 are shared subtanks, not projected. Weapons start at $1F87. */
typedef struct MmxCoopPlayer {
  uint8_t body[0x90];                /* $0BA8..0C37 */
  uint8_t auxiliaries[0x1e0];        /* armor/charge objects $0C38..0E17 */
  uint8_t shots[0x200];              /* $1228..1427 */
  uint8_t energy[16];               /* $1F87..96, shared unlock bits excluded */
  MmxZeroState zero;
  MmxWeaponsState weapons;
  MmxWeaponCombatState combat;
  uint16_t input, pressed;          /* engine's 12-bit seat input */
  uint8_t character, status;
  uint8_t shot_command, hud_state;  /* $1F0D and $1F12 */
} MmxCoopPlayer;

typedef struct MmxCoopState {
  MmxCoopPlayer players[2];
  uint8_t initialized, current, controller_pass, stage;
  /* Post-P1 registers survive P2's controller pass, including a save taken
   * during an interpreter deadline. Guest cycles are never rolled back. */
  uint16_t return_a, return_x, return_y, return_s;
  uint8_t return_p, return_db;
  uint8_t time_tick, effect_return; /* Previous seat + 1 during a dash-effect update; formerly reserved. */
  uint16_t object_a, object_x, object_y, object_s, object_d, object_entry;
  uint8_t object_p, object_db, object_pass, object_reserved;
  uint16_t contact_a, contact_x, contact_y, contact_s, contact_d, contact_entry;
  uint8_t contact_p, contact_db, contact_pass, platform_riders; /* Pending .2C bits; formerly reserved. */
  uint8_t enrolled, select_hold, select_armed, stage_pending;
  uint8_t menu_owner, menu_last, p1_select_hold, p1_select_armed; /* Last two bytes were reserved. */
  uint8_t pickup_owner[16]; /* Native item slots $1628 + index*$30. */
  uint16_t pickup_s, pickup_d;
  /* Learner Mode: frames P2 has spent off P1's screen; LEARNER_RESCUE after
   * a fatal hazard. Formerly reserved. */
  uint8_t pickup_pass, learner_away, pickup_reserved[2];
  uint8_t anchor, solo_death[2], respawn_pending; /* Seat bits; formerly reserved. */
  uint8_t death_flags[2][8]; /* Native freeze flags around a partner's death. */
  uint8_t scene_owner, scene_phase, door_pass, scene_flags; /* MMX_COOP_SCENE_*; formerly reserved. */
  uint16_t door_s, door_d, door_entry;
  uint16_t slime_p2; /* Capture owner bits for $1428 + slot*$40; formerly reserved. */
} MmxCoopState;

/* scene_flags, meaningful only while scene_owner is set. LIVE transport
 * (Learner Mode) teleports without freezing the world; WITHDRAW ends in the
 * absent state instead of a return. */
enum { MMX_COOP_SCENE_LIVE = 1, MMX_COOP_SCENE_WITHDRAW = 2 };
/* Learner Mode returns P2 after this long off P1's screen (learner_away). */
enum { MMX_COOP_LEARNER_AWAY_FRAMES = 120 };

enum { MMX_COOP_LEGACY_STATE_SIZE = 4648 };
/* v14 saves / v13 captures predate Modern's per-player extension. Keep
 * every original body, weapon and scheduler byte at its corresponding field. */
static inline void MmxCoopImportLegacy(MmxCoopState *out, const uint8_t *bytes) {
  const size_t extra = sizeof(MmxZeroModernState);
  const size_t old_player = sizeof(MmxCoopPlayer) - extra;
  const size_t prefix = offsetof(MmxCoopPlayer, zero) + MMX_ZERO_HEALTH_STATE_SIZE;
  memset(out, 0, sizeof(*out));
  for (unsigned i = 0; i < 2; ++i) {
    uint8_t *p = (uint8_t *)&out->players[i];
    memcpy(p, bytes + i * old_player, prefix);
    memcpy(p + prefix + extra, bytes + i * old_player + prefix, old_player - prefix);
  }
  memcpy(&out->initialized, bytes + old_player * 2,
      MMX_COOP_LEGACY_STATE_SIZE - old_player * 2);
}

/* The trusted co-op plugin prepares the owner-supplied X3 ROM, then enables
 * the chosen roster. This mode excludes single-player character exchange. */
bool MmxCoopEnable(unsigned p1_character);
void MmxCoopDisable(void);
bool MmxCoopEnabled(void);
/* Learner Mode: P2 cannot lose health or die, never drives the world, and
 * returns beside P1 after a fatal hazard or falling behind. Configuration,
 * like the roster: it is not serialized; the mod set carries it. */
void MmxCoopSetLearner(bool active);
bool MmxCoopLearner(void);
/* Includes the body projected for a native object/contact replay. */
bool MmxCoopActorIsX(void);
/* Owned body for a character's paused action. The caller may edit it only
 * while holding the scheduler; shared world and another seat stay untouched. */
uint8_t *MmxCoopCharacterBody(uint8_t *ram,unsigned character);
void MmxCoopReset(void);
MmxCoopState MmxCoopGetState(void);
bool MmxCoopValidState(const MmxCoopState *state);
void MmxCoopSetState(const MmxCoopState *state);
void MmxCoopInitialize(uint8_t ram[0x20000]);
void MmxCoopCapture(uint8_t ram[0x20000]);
/* Developer mod opt-in; host-only logging, outside save/netplay state. */
bool MmxCoopDiagnosticsEnabled(void);
void MmxCoopSetDiagnosticsEnabled(bool active);
void MmxCoopDiagnosticFrame(const uint8_t ram[0x20000]);
bool MmxCoopSelect(uint8_t ram[0x20000], unsigned player);
void MmxCoopPoll(uint16_t p1, uint16_t p2);
void MmxCoopApplyInput(uint8_t ram[0x20000]);
/* The live seat's physical X held and Y newly pressed, in joypad terms. */
void MmxCoopSeatButtons(bool *x_held, bool *y_pressed);
bool MmxCoopFrameTick(uint8_t ram[0x20000]);
bool MmxCoopTransitionActive(void);
bool MmxCoopFindLanding(const uint8_t ram[0x20000],uint16_t *x,uint16_t *y);
/* Caller must first validate the landing space. This does not grant re-entry
 * to a fallen player, and must not become the public join path by itself. */
bool MmxCoopPlacePartner(uint8_t ram[0x20000], uint16_t x, uint16_t y);
void MmxCoopRegisterHooks(void);
/* Host side, before each frame: ROM-dependent hook setup. */
void MmxCoopHostFrame(void);
void MmxCoopLiftCarry(uint8_t *ram);
void MmxCoopSyncPriority(uint8_t *ram);
/* Rush's end-of-frame recovery for a hurt reaction interrupted by a refill
 * or another player action. Includes the live body in solo mode. */
void MmxCoopResumeHurtTimers(uint8_t *ram);
/* End-of-frame hook for --coop-trace; no effect unless tracing. */
void MmxCoopTraceFrame(const uint8_t ram[0x20000]);
