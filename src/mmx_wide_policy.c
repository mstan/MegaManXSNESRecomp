#include "mmx_wide_policy.h"

bool MmxWidePolicy_IsBossDoorRow(const uint16_t tiles[4], int row_index) {
  if (!tiles || (unsigned)row_index >= 4)
    return false;

  /* The cap varies with its facing, while the three animated body metatiles
   * are the stable signature shared by the back-to-back columns. */
  return tiles[1] == 0x0172 && tiles[2] == 0x0173 &&
         tiles[3] == 0x0174;
}
