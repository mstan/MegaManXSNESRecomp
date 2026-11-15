#include "mmx_complete.h"
#include <stdio.h>
#include <string.h>

typedef struct CompletePatch {
  MmxCompleteFeature feature;
  uint32_t offset;
  unsigned size;
  uint32_t fingerprint;
  const char *data;
} CompletePatch;
#include "mmx_complete_data.inc"

enum { PATCH_COUNT = sizeof(patches) / sizeof(patches[0]), MAX_PATCH = 256 };
static uint8_t *cartridge;
static size_t cartridge_size;
static uint8_t original[PATCH_COUNT][MAX_PATCH];
static bool valid[PATCH_COUNT], active[MMX_COMPLETE_COUNT];

static uint32_t fingerprint(const uint8_t *p, unsigned size) {
  uint32_t hash = 2166136261u;
  while (size--) hash = (hash ^ *p++) * 16777619u;
  return hash;
}
void MmxCompleteBindRom(uint8_t *rom, size_t size) {
  if (rom && rom == cartridge && size == cartridge_size) MmxCompleteReset();
  cartridge = rom; cartridge_size = size;
  memset(active, 0, sizeof(active));
  memset(valid, 0, sizeof(valid));
  MmxCompleteDebugReset();
  for (unsigned i = 0; i < PATCH_COUNT; ++i) {
    const CompletePatch *p = patches + i;
    if (!rom || p->size > MAX_PATCH || p->offset > size || p->size > size - p->offset) continue;
    valid[i] = fingerprint(rom + p->offset, p->size) == p->fingerprint;
    if (valid[i]) memcpy(original[i], rom + p->offset, p->size);
  }
}
bool MmxCompleteEnabled(MmxCompleteFeature feature) {
  return feature >= 0 && feature < MMX_COMPLETE_COUNT && active[feature];
}
bool MmxCompleteSetEnabled(MmxCompleteFeature feature, bool on) {
  if (feature < 0 || feature >= MMX_COMPLETE_COUNT || !cartridge || !cartridge_size) return false;
  if (active[feature] == on) return true;
  /* Validate the complete feature before any write. Disjoint byte ownership
   * keeps every toggle independent and rejects an overlapping third-party mod. */
  for (unsigned i = 0; i < PATCH_COUNT; ++i) {
    const CompletePatch *p = patches + i;
    if (p->feature != feature) continue;
    const void *expected = on ? (const void *)original[i] : (const void *)p->data;
    if (!valid[i] || memcmp(cartridge + p->offset, expected, p->size)) {
      fprintf(stderr, "[complete-edition] Feature %u conflicts at ROM offset $%06x\n", (unsigned)feature, p->offset);
      return false;
    }
  }
  for (unsigned i = 0; i < PATCH_COUNT; ++i) {
    const CompletePatch *p = patches + i;
    if (p->feature == feature)
      memcpy(cartridge + p->offset, on ? (const void *)p->data : (const void *)original[i], p->size);
  }
  active[feature] = on;
  if (feature == MMX_COMPLETE_DEBUG && !on) MmxCompleteDebugReset();
  return true;
}
void MmxCompleteReset(void) {
  for (unsigned i = 0; i < MMX_COMPLETE_COUNT; ++i)
    if (active[i]) (void)MmxCompleteSetEnabled((MmxCompleteFeature)i, false);
  MmxCompleteDebugReset();
}
