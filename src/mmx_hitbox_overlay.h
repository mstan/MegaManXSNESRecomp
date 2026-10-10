#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mmx_renderer.h"

/* Developer hitbox overlay. Presentation only: it reads guest RAM and the
 * collision ROM and never writes either. Each box is the object's native
 * collision record, the one $84:9C16 loads from +$20 (bank $86: signed
 * center X/Y, half width, half height; X mirrors with facing bit +$11.6). */
enum {
  MMX_HITBOX_ENEMY = 0x001f,      /* red */
  MMX_HITBOX_PLAYER = 0x03e0,     /* green */
  MMX_HITBOX_SHOT = 0x03ff,       /* yellow */
  MMX_HITBOX_MELEE = 0x7fe0,      /* cyan: Zero's slash and Saber attacks */
  MMX_HITBOX_HAZARD = 0x7c1f      /* magenta: enemy projectiles */
};

void MmxHitboxOverlaySetEnabled(bool enabled);
bool MmxHitboxOverlayEnabled(void);

/* Boxes for the live seat from `ram`, plus the co-op partner's stored body
 * and shots when `partner_body`/`partner_shots` are given. */
unsigned MmxHitboxOverlayCollect(const uint8_t *ram, const uint8_t *rom,
                                 size_t rom_size,
                                 const uint8_t *partner_body,
                                 const uint8_t *partner_shots,
                                 MmxRenderDebugRect *out, unsigned max);

/* Renderer provider for the custom compositor. */
unsigned MmxHitboxOverlayProvide(MmxRenderDebugRect *out, unsigned max);

/* Without the custom compositor: snapshot at frame start, then outline the
 * stock 256x224 raster. Returns false when there is nothing to draw. */
void MmxHitboxOverlayBeginStockFrame(void);
bool MmxHitboxOverlayDrawStock(uint32_t *pixels, const uint8_t *field);
