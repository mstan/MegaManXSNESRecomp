#include "mmx_project_zero.h"
#include <stdlib.h>
#include <string.h>

#if MMX_VARIANT_JP
static const uint8_t kMmxProjectZeroData[8] = {0};
static const size_t kMmxProjectZeroDataSize = 0;
#else
extern const uint8_t kMmxProjectZeroData[];
extern const size_t kMmxProjectZeroDataSize;
#endif

bool MmxProjectZeroBundled(void) {
  return kMmxProjectZeroDataSize == MMX_PROJECT_ZERO_PACK_BYTES &&
      !memcmp(kMmxProjectZeroData, MMX_PROJECT_ZERO_PACK_MAGIC, 8);
}

static unsigned unpack_word(const uint8_t **data) {
  unsigned word = (*data)[0] | (*data)[1] << 8;
  *data += 2;
  return word;
}

MmxProjectZeroAssets *MmxProjectZeroLoad(uint8_t hud[MMX_ZERO_HUD_BYTES]) {
  if (!MmxProjectZeroBundled()) return NULL;
  MmxProjectZeroAssets *assets = malloc(sizeof(*assets));
  if (!assets) return NULL;
  const uint8_t *data = kMmxProjectZeroData + 8;
  memcpy(assets->body, data, sizeof(assets->body));
  data += sizeof(assets->body);
  memcpy(assets->capture, data, sizeof(assets->capture));
  data += sizeof(assets->capture);
  for (unsigned i = 0; i < 128; ++i) assets->colors[i] = (uint16_t)unpack_word(&data);
  assets->menu_y = (int)unpack_word(&data);
  assets->jump_speed = (uint16_t)unpack_word(&data);
  memcpy(assets->muzzle_y, data, sizeof(assets->muzzle_y));
  data += sizeof(assets->muzzle_y);
  memcpy(hud, data, MMX_ZERO_HUD_BYTES);
  return assets;
}
