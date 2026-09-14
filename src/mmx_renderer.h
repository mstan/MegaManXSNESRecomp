#pragma once
#include "snes/ppu.h"
#include "mmx_display.h"

enum { MMX_RENDER_HEIGHT = 224, MMX_RENDER_MAX_WIDTH = 1024 };
typedef enum MmxRenderAspect {
  MMX_ASPECT_ADAPTIVE, MMX_ASPECT_16_9, MMX_ASPECT_21_9, MMX_ASPECT_32_9
} MmxRenderAspect;
typedef struct MmxRenderView { int width, extra; double aspect; } MmxRenderView;
typedef struct MmxRenderStats {
  unsigned custom_lines, fallback_lines, margin_sprite_pixels, pieces;
} MmxRenderStats;

extern bool g_mmx_custom_renderer;
extern bool g_mmx_custom_hud;
extern bool g_mmx_expanded_sprites;
/* Diagnostic oracle switch: compare the compositor before art repairs. */
extern bool g_mmx_render_asset_repairs;
extern MmxRenderAspect g_mmx_custom_aspect;
extern MmxRenderView g_mmx_custom_view;
MmxRenderView MmxRendererViewport(MmxRenderAspect aspect, int width, int height);
MmxDisplayViewport MmxRendererDestination(MmxRenderView view, int width, int height);
void MmxRendererReset(void);
void MmxRendererSetRom(const uint8_t *rom, size_t size);
/* Capture drawing data before native clipping; never modify guest state. */
void MmxRendererRecordPiece(const uint8_t ram[0x20000], uint16_t scratch);
void MmxRendererObserveObject(const uint8_t ram[0x20000], uint16_t object);
void MmxRendererLatchSprites(void);
void MmxRendererBeginFrame(const uint8_t ram[0x20000]);
void MmxRendererCaptureLine(const Ppu *ppu, unsigned line);
bool MmxRendererEndFrame(const uint32_t stock[256 * 224]);
bool MmxRendererDraw(uint32_t *output, MmxRenderView view, bool anchor_hud);
MmxRenderStats MmxRendererGetStats(void);
bool MmxRendererStageTile(const uint8_t ram[0x20000], unsigned layer,
                          int x, int y, uint16_t *word);
bool MmxRendererSaveCapture(const char *path);
bool MmxRendererLoadCapture(const char *path);
const uint32_t *MmxRendererStockFrame(void);
