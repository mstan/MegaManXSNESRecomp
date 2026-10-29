#ifndef MMX_WIDE_POLICY_H
#define MMX_WIDE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

uint8_t MmxWidePolicy_CrusherTileBase(const uint8_t ram[0x20000], uint16_t object, uint8_t base);
bool MmxWidePolicy_IsStageScene(const uint8_t ram[0x20000]);
uint16_t MmxWidePolicy_FlyerLeash(unsigned margin);
bool MmxWidePolicy_RideArmorCull(uint16_t distance, unsigned margin);
bool MmxWidePolicy_ShotCull(const uint8_t ram[0x20000], uint16_t object,
                            uint16_t distance, unsigned margin, bool custom);
bool MmxWidePolicy_PresentationCull(const uint8_t ram[0x20000], uint16_t object,
                                    uint16_t distance, unsigned margin, bool custom);
bool MmxWidePolicy_PrematureRideArmor(const uint8_t ram[0x20000]);
bool MmxWidePolicy_RecoverRideArmor(uint8_t ram[0x20000], unsigned margin);

/* Recover a waiting bee's premature camera lock from older spike saves and
 * keep its entrance behind the native encounter boundary. Custom mode only. */
uint16_t MmxWidePolicy_BeeEntrance(uint8_t ram[0x20000], uint16_t object,
                                  uint16_t distance);

/* MMX authors each boss-room boundary as two back-to-back 16-pixel door
 * columns. The native camera shows only the column belonging to the current
 * room. Door metatile IDs vary by stage, but their three-row 8x8 construction
 * is stable: mirrored top, four-way mirrored center, reversed bottom. Return
 * true when row_index belongs to a stack with that structural signature. */
bool MmxWidePolicy_IsBossDoorBody(const uint16_t words[3][4], int row_index);
/* Enemy-family identity, shared by original stages and fortress rematches. */
bool MmxWidePolicy_IsBossEncounter(uint8_t object_id);
/* Boss encounters plus the other enemy-pool objects whose initialization
 * locks the camera or forces the world actor. Their spawn records keep the
 * native entry timing: an early look-ahead spawn reorders them against the
 * room's own event triggers and allocation order. */
bool MmxWidePolicy_IsScriptedEncounter(uint8_t object_id);
uint16_t MmxWidePolicy_BarrierEnemyState(const uint8_t ram[0x20000], uint16_t controller,
                                       uint16_t object, uint16_t state, bool custom);
bool MmxWidePolicy_IsCollectible(uint8_t object_id);
/* Early Highway activation can precede the section's OBJ allocation. Only
 * this proven all-zero Gun Volt binding may borrow the resource priority. */
bool MmxWidePolicy_GunVoltUnbound(const uint8_t ram[0x20000], uint16_t object,
                                 unsigned animation);
bool MmxWidePolicy_RescanSpawnRecord(uint8_t stage, uint8_t kind, uint8_t object_id);
void MmxWidePolicy_StreakerEntrance(uint8_t ram[0x20000], uint16_t object, unsigned margin);
bool MmxWidePolicy_RecoverParkedStreaker(uint8_t ram[0x20000], uint16_t object, uint16_t authored_x);
uint16_t MmxWidePolicy_ChainPlatformLine(const uint8_t ram[0x20000], uint16_t object,
                                      uint16_t line, unsigned margin);

typedef struct MmxWideSpawnCursor {
  uint16_t wide;
  bool valid;
} MmxWideSpawnCursor;

/* The widened and native scans walk the same ROM event list at different
 * camera anchors. Keep a persistent widened cursor so rejecting a record in
 * one pass cannot consume it for the other. */
uint16_t MmxWidePolicy_BeginWideSpawnPass(MmxWideSpawnCursor *cursor,
                                          uint16_t native_cursor);
void MmxWidePolicy_EndWideSpawnPass(MmxWideSpawnCursor *cursor,
                                    uint16_t wide_cursor);

/* The widened cursor is only meaningful while a level is being played. A
 * death restarts the level from its checkpoint within the same stage: the
 * guest rebuilds its own event-list cursor, so a widened cursor left over from
 * before the death points past every record between the checkpoint and the
 * place the player died, and the wide pass (which alone allocates most
 * kind-3 enemies) never visits them again. Return false in every phase of the
 * stage scene except play, so the cursor is re-synchronized to the guest's
 * before the next scan. Derived from guest RAM only: deterministic, and
 * identical across a state load or a rollback. */
bool MmxWidePolicy_WideSpawnCursorPersists(const uint8_t ram[0x20000]);

/* Decide which half of the split scan owns a record. Most kind-3 objects are
 * ordinary enemies and belong to the early wide pass; kinds 0-2 retain native
 * timing. A small number of stable stage/object identities override that
 * broad kind classification. */
bool MmxWidePolicy_SpawnRecordAllowed(uint8_t stage, uint8_t kind,
                                      uint8_t object_id, bool native_pass);
/* Protect allocation-sensitive Vile scripts before a larger lookahead can
 * reach their room. A zero lookahead retains the shipped legacy interval. */
bool MmxWidePolicy_ForceNativeSpawnTiming(uint8_t stage, uint16_t camera,
                                         unsigned lookahead);

#endif
