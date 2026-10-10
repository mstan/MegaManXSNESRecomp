#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../mmx_renderer.h"

enum {
  MMX_SABER_HITBOX_RED = 0x001f,
  MMX_SABER_HITBOX_GREEN = 0x03e0,
  MMX_SABER_HITBOX_YELLOW = 0x03ff,
  MMX_SABER_HITBOX_CYAN = 0x7fe0
};

/* The provider reads the last native frame pointers; it never writes guest
 * RAM or the collision ROM. */
void MmxSaberHitboxDebugSetRam(const uint8_t *ram);
void MmxSaberHitboxDebugSetRom(const uint8_t *rom, size_t size);
void MmxSaberHitboxDebugReset(void);
unsigned MmxSaberHitboxDebugProvide(MmxRenderDebugRect *out, unsigned max);
