#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MmxCompleteFeature {
  MMX_COMPLETE_SCRIPT, MMX_COMPLETE_COLOURS, MMX_COMPLETE_TYPOS,
  MMX_COMPLETE_HOGANMER, MMX_COMPLETE_DAMAGE, MMX_COMPLETE_DEBUG,
  MMX_COMPLETE_COUNT
} MmxCompleteFeature;

/* Bind only the live cartridge copy; the owner's ROM and renderer input stay
 * immutable. Rebinding forgets the previous machine without dereferencing it. */
void MmxCompleteBindRom(uint8_t *rom, size_t size);
bool MmxCompleteSetEnabled(MmxCompleteFeature feature, bool enabled);
bool MmxCompleteEnabled(MmxCompleteFeature feature);
void MmxCompleteReset(void);

/* All input history and pending map-editor calls belong to simulation state. */
typedef struct MmxCompleteDebugState {
  uint16_t held;
  uint8_t open, selection, map_pending, map_active, suppress, reserved;
} MmxCompleteDebugState;
MmxCompleteDebugState MmxCompleteDebugGetState(void);
bool MmxCompleteDebugValidState(const MmxCompleteDebugState *state);
void MmxCompleteDebugSetState(const MmxCompleteDebugState *state);
void MmxCompleteDebugReset(void);
bool MmxCompleteDebugFrame(uint8_t *ram, uint16_t input);
bool MmxCompleteDebugDraw(uint32_t *pixels, int width, int height, const uint8_t *ram);
bool MmxCompleteDebugMapActive(void);
