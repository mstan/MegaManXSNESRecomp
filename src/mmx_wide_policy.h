#ifndef MMX_WIDE_POLICY_H
#define MMX_WIDE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* MMX authors each boss-room boundary as two back-to-back 16-pixel door
 * columns. The native camera shows only the column belonging to the current
 * room. Return true when row_index belongs to that four-metatile stack. */
bool MmxWidePolicy_IsBossDoorRow(const uint16_t tiles[4], int row_index);

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

/* Decide which half of the split scan owns a record. Most kind-3 objects are
 * ordinary enemies and belong to the early wide pass; kinds 0-2 retain native
 * timing. A small number of stable stage/object identities override that
 * broad kind classification. */
bool MmxWidePolicy_SpawnRecordAllowed(uint8_t stage, uint8_t kind,
                                      uint8_t object_id, bool native_pass);

#endif
