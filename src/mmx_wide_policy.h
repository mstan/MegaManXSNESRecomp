#ifndef MMX_WIDE_POLICY_H
#define MMX_WIDE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* MMX authors each boss-room boundary as two back-to-back 16-pixel door
 * columns. The native camera shows only the column belonging to the current
 * room. Return true when row_index belongs to that four-metatile stack. */
bool MmxWidePolicy_IsBossDoorRow(const uint16_t tiles[4], int row_index);

#endif
