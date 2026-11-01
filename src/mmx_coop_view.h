#pragma once
#include "mmx_coop.h"

/* The active independent-world policy is shared by local split and online
 * independent cameras. Selecting a presentation seat never changes it. */
typedef struct MmxCoopView { int x, y; unsigned player; } MmxCoopView;
void MmxCoopViewsSetActive(bool active);
bool MmxCoopViewsActive(void);
void MmxCoopViewsSetLocalSplit(bool enabled);
bool MmxCoopViewsLocalSplit(void);
void MmxCoopViewsSetIndependent(bool independent);
bool MmxCoopViewsIndependent(void);

/* Per-seat copies of the native $81:F6DC camera-region commands. These
 * affect culling and pit checks, so they belong to the rollback state. */
typedef struct MmxCoopViewBoundsState {
  uint16_t limits[2][4]; /* left, right, top, bottom */
  uint8_t valid, stage;
  /* Native current limits ease toward the authored targets above. Camera
   * history advances once per simulation frame, never during presentation. */
  uint16_t current[2][4], camera[2][2];
  uint8_t camera_valid;
} MmxCoopViewBoundsState;
void MmxCoopViewsSetRom(const uint8_t *rom, size_t size);
MmxCoopViewBoundsState MmxCoopViewsGetBoundsState(void);
bool MmxCoopViewsValidBoundsState(const MmxCoopViewBoundsState *bounds);
void MmxCoopViewsSetBoundsState(const MmxCoopViewBoundsState *bounds);
void MmxCoopViewsUpdateBounds(const uint8_t *ram, const MmxCoopState *state);
void MmxCoopViewsEndFrame(const uint8_t *ram, const MmxCoopState *state);
/* Before a remote player takes a native door, transfer its authored view to
 * the shared script camera while both independent views still exist. */
void MmxCoopViewsAdoptPlayer(uint8_t *ram, const MmxCoopState *state, unsigned seat);
void MmxCoopViewsPlayerLimits(const uint8_t *ram, const MmxCoopState *state,
                            unsigned seat, int limits[4]);
MmxCoopView MmxCoopViewWithBounds(const uint8_t *ram, const MmxCoopState *state,
    unsigned seat, const MmxCoopViewBoundsState *bounds);
typedef struct MmxCoopViewWorldState {
  uint8_t seen[256],stage,initialized;
  /* Balanced native world/contact continuations must survive rollback. */
  uint8_t actor_return,contact_player;
} MmxCoopViewWorldState;
MmxCoopViewWorldState MmxCoopViewsGetWorldState(void);
void MmxCoopViewsSetWorldState(const MmxCoopViewWorldState *state);
void MmxCoopViewsResetWorld(void);
bool MmxCoopViewsSeen(unsigned flag);
void MmxCoopViewsMark(unsigned flag,bool seen);
void MmxCoopViewsBeginStage(unsigned stage);
void MmxCoopViewsActorReturn(unsigned player_plus_one);
void MmxCoopViewsContactPlayer(unsigned player);
MmxCoopView MmxCoopViewForPlayer(const uint8_t *ram, const MmxCoopState *state, unsigned seat);
bool MmxCoopViewContains(const uint8_t *ram, const MmxCoopState *state,
                         int x, int y, int left, int right, int top, int bottom, int margin);
