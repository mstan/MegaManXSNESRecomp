#pragma once
/* Local presentation only. -1 selects the existing shared view. */
#include <stdint.h>

void MmxRendererSetPeerView(int seat);
#include "snes/ppu.h"
#include "mmx_display.h"
#include "mmx_coop.h"

enum { MMX_RENDER_HEIGHT = 224, MMX_RENDER_MAX_WIDTH = 1024 };
typedef enum MmxRenderAspect {
  MMX_ASPECT_ADAPTIVE, MMX_ASPECT_16_9, MMX_ASPECT_21_9, MMX_ASPECT_32_9
} MmxRenderAspect;
typedef struct MmxRenderView { int width, extra; double aspect; } MmxRenderView;
typedef struct MmxRenderStats {
  unsigned custom_lines, fallback_lines, margin_sprite_pixels, pieces;
} MmxRenderStats;

/* A presentation-only player plane. Origins are signed coordinates on the
 * player's 128x128 canvas; the renderer owns projection and composition. */
typedef struct MmxRenderPlayerOverlayPlane {
  const uint8_t *pixels;
  uint16_t width;
  uint16_t height;
  int16_t origin_x;
  int16_t origin_y;
} MmxRenderPlayerOverlayPlane;

typedef struct MmxRenderPlayerOverlay {
  bool active;
  MmxRenderPlayerOverlayPlane body;
  MmxRenderPlayerOverlayPlane blade;
  /* 0 none, 1 behind body, 2 in front. */
  uint8_t blade_layer;
  const uint16_t *palette;
  unsigned palette_count;
  bool facing_left;
} MmxRenderPlayerOverlay;

/* Called for each Zero body the renderer draws: the live seat at BeginFrame,
 * and in co-op the partner seat at CoopFrame. `ram` holds that body at
 * $0BA8 and `zero` is its seat's Zero state. */
struct MmxZeroState;
typedef bool (*MmxRendererPlayerOverlayProvider)(
    const uint8_t *ram, const struct MmxZeroState *zero,
    MmxRenderPlayerOverlay *out);

void MmxRendererSetPlayerOverlayProvider(
    MmxRendererPlayerOverlayProvider provider);
MmxRenderPlayerOverlay MmxRendererPlayerOverlaySnapshot(void);

/* A presentation-only world sprite. The renderer owns camera projection,
 * horizontal mirroring, clipping, and compositor placement. */
typedef struct MmxRenderWorldSprite {
  const uint8_t *pixels;
  uint16_t width, height;
  int16_t origin_x, origin_y;
  int32_t world_x, world_y;
  bool facing_left;
  const uint16_t *palette;
  unsigned palette_count;
  uint16_t z;
} MmxRenderWorldSprite;

typedef unsigned (*MmxRendererWorldSpriteProvider)(MmxRenderWorldSprite *out,
                                                   unsigned max);

void MmxRendererSetWorldSpriteProvider(MmxRendererWorldSpriteProvider provider);
/* Copy the immutable snapshot captured by BeginFrame. */
unsigned MmxRendererWorldSpriteSnapshot(MmxRenderWorldSprite *out,
                                        unsigned max);

/* A presentation-only world-space diagnostic rectangle. The origin is the
 * top-left corner; w and h are inclusive pixel dimensions. rgb555 uses the
 * native BGR555 bit layout. */
typedef struct MmxRenderDebugRect {
  int32_t world_x, world_y;
  uint16_t w, h;
  uint16_t rgb555;
} MmxRenderDebugRect;

typedef unsigned (*MmxRendererDebugRectProvider)(MmxRenderDebugRect *out,
                                                 unsigned max);

void MmxRendererSetDebugRectProvider(MmxRendererDebugRectProvider provider);
/* The developer hitbox overlay; when set it replaces the provider above. */
void MmxRendererSetHitboxOverlayProvider(MmxRendererDebugRectProvider provider);
/* Copy the immutable snapshot captured by BeginFrame. */
unsigned MmxRendererDebugRectSnapshot(MmxRenderDebugRect *out,
                                      unsigned max);

extern bool g_mmx_custom_renderer;
extern bool g_mmx_custom_hud;
extern bool g_mmx_expanded_sprites;
/* Diagnostic oracle switch: compare the compositor before art repairs. */
extern bool g_mmx_render_asset_repairs;
extern MmxRenderAspect g_mmx_custom_aspect;
extern MmxRenderView g_mmx_custom_view;
void MmxRendererSetCompactCoopHud(bool compact);
/* The mod selects the output aspect; Display Aspect selects pixel proportions.
 * Clamp the view extent at those proportions before rounding to an even width. */
MmxRenderView MmxRendererViewport(MmxRenderAspect aspect, int width, int height,
                                  SnesDisplayAspect display_aspect);
MmxDisplayViewport MmxRendererDestination(MmxRenderView view, int width, int height);
void MmxRendererReset(void);
void MmxRendererSetRom(const uint8_t *rom, size_t size);
/* Capture drawing data before native clipping; never modify guest state. */
void MmxRendererRecordPiece(const uint8_t ram[0x20000], uint16_t scratch);
void MmxRendererObserveObject(const uint8_t ram[0x20000], uint16_t object);
/* Co-op replays an object loop speculatively and discards its guest state;
 * these drop the pieces that replay recorded. */
typedef struct MmxRendererPieceMark {
  unsigned building, expanded;
  uint16_t object;
  uint8_t stage;
  bool observed;
} MmxRendererPieceMark;
MmxRendererPieceMark MmxRendererMarkPieces(void);
void MmxRendererRewindPieces(MmxRendererPieceMark mark);
void MmxRendererLatchSprites(void);
void MmxRendererBeginFrame(const uint8_t ram[0x20000]);
void MmxRendererCoopFrame(const MmxCoopState *state);
void MmxRendererHoldFrame(bool held);
void MmxRendererCaptureLine(const Ppu *ppu, unsigned line);
bool MmxRendererEndFrame(const uint32_t stock[256 * 224]);
bool MmxRendererDraw(uint32_t *output, MmxRenderView view, bool anchor_hud);
MmxRenderStats MmxRendererGetStats(void);
bool MmxRendererStageTile(const uint8_t ram[0x20000], unsigned layer,
                          int x, int y, uint16_t *word);
bool MmxRendererSaveCapture(const char *path);
bool MmxRendererLoadCapture(const char *path);
const uint32_t *MmxRendererStockFrame(void);
