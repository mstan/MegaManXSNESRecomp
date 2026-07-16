#ifndef MMX_WIDE_PREVIEW_H
#define MMX_WIDE_PREVIEW_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Ppu Ppu;

/* Draw not-yet-active enemies in the host-only margins after the PPU frame.
 * This never allocates an object slot or writes guest/PPU state; ROM graphics
 * and current CGRAM colors are consumed read-only. */
void MmxWidePreview_Draw(uint8_t *pixels, int width, int height, int extra);

/* Extend MMX's prepared level geometry into the live scanline priority buffer.
 * Installed as a host-only PPU callback by the MMX display path. */
void MmxWidePreview_EnhancePpuLine(Ppu *ppu, unsigned int y, bool sub);

#endif
