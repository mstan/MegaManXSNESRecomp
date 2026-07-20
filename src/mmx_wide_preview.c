#include "mmx_wide_preview.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mmx_display.h"
#include "snes/ppu.h"
#include "snes/snes.h"

extern Snes *g_snes;

#ifndef MMX_VARIANT_JP
#define MMX_VARIANT_JP 0
#endif

enum {
  kPlayableStages = 0x0d,
  kEnemyPointerOffset = 0x0282c2,
  kEnemyDataBankOffset = 0x028000,
  kPaletteInfoOffset = 0x030133,
  kPaletteBankOffset = 0x030000,
  kPaletteColorBankOffset = 0x028000,
  kSpritePointerOffset = 0x068000,
  kCompressedInfoOffset = 0x0376f7 + (MMX_VARIANT_JP ? 3 : 0),
  kTileSpecOffset = 0x0371b7 + (MMX_VARIANT_JP ? 3 : 0),
  kObjectSpriteInfoOffset = 0x0325e4 + (MMX_VARIANT_JP ? 3 : 0),
  kLayoutPointerOffset = 0x030d24,
  kExpandedLayoutWram = 0x0e800,
  kExpandedScreensWram = 0x02000,
  kTile16PointerWram = 0x00b95,
  kMaxIconPixels = 128 * 128,
};

typedef struct StageBounds {
  bool attempted;
  bool valid;
  uint8_t width, height;
} StageBounds;

static StageBounds s_stage_bounds[kPlayableStages];

typedef struct EnemyIconSpec {
  uint8_t id;
  uint16_t palette;
  int8_t frame;
  int8_t tile_base;
  bool load_from_spec;
} EnemyIconSpec;

/* Rendering metadata recovered from MMX's own sprite/palette tables. The
 * palette number is the editor-facing lookup key; arrangements and graphics
 * still come from the user's ROM at runtime. */
static const EnemyIconSpec kEnemySpecs[] = {
  {0x01,0x004,0,0,true},{0x02,0x128,0,0,true},{0x04,0x01e,3,0,true},
  {0x05,0x13c,0,0,true},{0x06,0x022,0,0,true},{0x07,0x13e,0,0,true},
  {0x0a,0x054,0,0,true},{0x0b,0x036,0,0,true},{0x0c,0x15a,0,0,true},
  {0x0f,0x03c,0,0,true},{0x10,0x008,0,0,true},{0x11,0x03a,0,0,true},
  {0x13,0x006,0,0,true},{0x14,0x120,0,0,true},{0x15,0x03e,0,0,true},
  {0x16,0x182,0,0,true},{0x17,0x182,0,0,true},{0x19,0x058,0,0,true},
  {0x1d,0x082,0,0,true},{0x1e,0x084,0,0,true},{0x20,0x088,0,0,true},
  {0x22,0x08a,0,0,true},{0x27,0x08e,0,0,true},{0x29,0x090,0,0,true},
  {0x2b,0x0e2,0,0,true},{0x2c,0x0e4,0,0,true},{0x2d,0x0e8,0,0,true},
  {0x2e,0x0ec,0,0,true},{0x2f,0x0e6,0,0,true},{0x30,0x0f0,0,0,true},
  {0x31,0x1d2,0,0,true},{0x34,0x0ee,0,0,true},{0x35,0x0fc,0,0,true},
  {0x36,0x114,0,0,true},{0x37,0x0fa,0,0,true},{0x39,0x0fe,0,0,true},
  {0x3a,0x11e,0,0,true},{0x3b,0x148,0,0,true},{0x3d,0x14c,0,0,true},
  {0x40,0x134,0,0,true},{0x42,0x13a,0,0,true},{0x44,0x13a,0,0,true},
  {0x47,0x150,0,0,true},{0x49,0x156,0,0,true},{0x4c,0x026,0,0,true},
  {0x4d,0x038,0,-0x40,false},{0x4f,0x026,0,0,true},{0x50,0x096,0,0,true},
  {0x51,0x168,0,0,true},{0x52,0x16e,0,0,true},{0x53,0x16c,0,0,true},
  {0x54,0x180,0,0,true},{0x5b,0x196,0,0,true},{0x5d,0x1fc,0,0,true},
  {0x62,0x1da,0,0,true},{0x63,0x1d8,0,0,true},{0x65,0x1dc,0,0,true},
};

typedef struct CachedIcon {
  bool attempted;
  bool valid;
  int left, top, width, height;
  uint32_t pixels[kMaxIconPixels];
} CachedIcon;

static CachedIcon s_icons[256];

static size_t RomSize(void) {
  return (g_snes && g_snes->cart) ? g_snes->cart->romSize : 0;
}

static bool RomRange(size_t off, size_t size) {
  size_t total = RomSize();
  return off <= total && size <= total - off;
}

static const uint8_t *SafeRomPtr(size_t off, size_t size) {
  return RomRange(off, size) ? g_rom + off : NULL;
}

static uint8_t Rom8(size_t off) {
  const uint8_t *p = SafeRomPtr(off, 1);
  return p ? *p : 0;
}

static uint16_t Read16(size_t off) {
  const uint8_t *p = SafeRomPtr(off, 2);
  return p ? (uint16_t)(p[0] | ((uint16_t)p[1] << 8)) : 0;
}

static uint32_t Read24(size_t off) {
  const uint8_t *p = SafeRomPtr(off, 3);
  return p ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) : 0;
}

static size_t LoRomOffset(uint32_t address) {
  size_t off = (size_t)(((address >> 16) & 0x7f) * 0x8000u) +
               (size_t)(address & 0x7fffu);
  return RomRange(off, 1) ? off : SIZE_MAX;
}

static bool LoadStageBounds(uint8_t stage) {
  StageBounds *bounds = &s_stage_bounds[stage];
  if (bounds->attempted) return bounds->valid;
  bounds->attempted = true;

  size_t src = LoRomOffset(Read24(kLayoutPointerOffset + (size_t)stage * 3));
  if (src == SIZE_MAX || !RomRange(src, 2)) return false;
  bounds->width = Rom8(src++);
  bounds->height = Rom8(src++);
  if (bounds->width == 0 || bounds->width > 32 ||
      bounds->height == 0 || bounds->height > 32)
    return false;
  bounds->valid = true;
  return true;
}

static bool StageTileWord(const StageBounds *bounds, int world_x, int world_y,
                          uint16_t *word) {
  int screen_x = world_x >> 8;
  int screen_y = world_y >> 8;
  if ((unsigned)screen_x >= bounds->width ||
      (unsigned)screen_y >= bounds->height)
    return false;

  int local_x = world_x & 0xff;
  int local_y = world_y & 0xff;
  uint8_t screen = g_ram[kExpandedLayoutWram + screen_y * 32 + screen_x];
  size_t expanded = kExpandedScreensWram + (size_t)screen * 0x200 +
                    (size_t)(((local_y >> 4) * 16 + (local_x >> 4)) * 2);
  if (expanded + 1 >= 0x10000) return false;
  uint16_t tile16 = (uint16_t)(g_ram[expanded] | (g_ram[expanded + 1] << 8));

  /* $0B95-$0B97 is the live ROM pointer selected by MMX for BG1's 16x16
   * table. The game has already expanded screen/32x32 data into WRAM, which
   * is both faster to query and preserves stage-specific preparation. */
  uint32_t tile16_table = (uint32_t)g_ram[kTile16PointerWram] |
                          ((uint32_t)g_ram[kTile16PointerWram + 1] << 8) |
                          ((uint32_t)g_ram[kTile16PointerWram + 2] << 16);
  if ((tile16_table & 0x800000) == 0 || (tile16_table & 0xffff) < 0x8000)
    return false;
  int quadrant8 = ((local_y >> 3) & 1) * 2 + ((local_x >> 3) & 1);
  uint16_t table_addr = (uint16_t)(tile16_table + tile16 * 8 + quadrant8 * 2);
  if (table_addr < 0x8000) return false;
  size_t table = LoRomOffset((tile16_table & 0xff0000) | table_addr);
  if (table == SIZE_MAX || !RomRange(table, 2)) return false;
  *word = Read16(table);
  return true;
}

static uint8_t BackgroundTilePixel(const Ppu *ppu, uint16_t tile_word,
                                   int x, int y) {
  if (tile_word & 0x4000) x = 7 - x;
  if (tile_word & 0x8000) y = 7 - y;
  size_t tile_addr = (size_t)PPU_bgTileAdr(ppu, 0) +
                     (size_t)(tile_word & 0x03ff) * 16;
  uint16_t lo = ppu->vram[(tile_addr + y) & 0x7fff];
  uint16_t hi = ppu->vram[(tile_addr + 8 + y) & 0x7fff];
  int bit = 7 - x;
  return (uint8_t)(((lo >> bit) & 1) | (((lo >> (bit + 8)) & 1) << 1) |
                   (((hi >> bit) & 1) << 2) |
                   (((hi >> (bit + 8)) & 1) << 3));
}

bool MmxWidePreview_IsMarginEnhancerReady(void) {
  if (!g_rom || !g_snes || !g_snes->cart || !g_ram) return false;
  uint8_t stage = g_ram[0x1f7a];
  return stage < kPlayableStages && LoadStageBounds(stage);
}

void MmxWidePreview_EnhancePpuLine(Ppu *ppu, unsigned int y, bool sub,
                                   void *context) {
  (void)context;
  if (!ppu || y == 0 || (ppu->bgmode & 7) != 1 ||
      !(ppu->screenEnabled[sub] & 1))
    return;
  uint8_t stage = g_ram[0x1f7a];
  if (stage >= kPlayableStages || !LoadStageBounds(stage)) return;

  /* Use the full game camera for the stage-screen number and the PPU's live
   * BG1 registers for the within-screen phase. Using only the camera caused
   * a one-line seam; using only the 10-bit PPU scroll tore by whole streamed
   * pages after later horizontal/vertical camera motion. */
  uint16_t world_camera_x =
      (uint16_t)(g_ram[0x1e4d] | (g_ram[0x1e4e] << 8));
  uint16_t world_camera_y =
      (uint16_t)(g_ram[0x1e50] | (g_ram[0x1e51] << 8));
  int camera_x = MmxDisplay_ExpandStageScroll(world_camera_x, ppu->hScroll[0]);
  int world_y = MmxDisplay_ExpandStageScroll(world_camera_y, ppu->vScroll[0]) +
                (int)y;
  PpuZbufType *dst = ppu->bgBuffers[sub].data;

  /* Add only host-visible margin columns. Each ROM tile word becomes the same
   * priority-buffer value produced by PpuDrawBackground_4bpp, so live OAM,
   * BG priorities, subscreen color math, brightness, and windows retain their
   * normal ordering. */
  for (int side = 0; side < 2; side++) {
    int x_begin = side ? 256 : -(int)ppu->extraLeftCur;
    int x_end = side ? 256 + ppu->extraRightCur : 0;
    for (int x = x_begin; x < x_end; x++) {
      int world_x = camera_x + x;
      uint16_t tile_word;
      if (!StageTileWord(&s_stage_bounds[stage], world_x, world_y, &tile_word))
        continue;
      uint8_t pixel = BackgroundTilePixel(ppu, tile_word,
                                          world_x & 7, world_y & 7);
      if (!pixel) continue;
      PpuZbufType z = (tile_word & 0x2000) ? 0xc000 : 0x8000;
      z += (PpuZbufType)(((tile_word & 0x1c00) >> 6) + pixel);
      int index = x + kPpuExtraLeftRight;
      if (z > dst[index]) dst[index] = z;
    }
  }
}

static const EnemyIconSpec *FindSpec(uint8_t id) {
  for (size_t i = 0; i < sizeof(kEnemySpecs) / sizeof(kEnemySpecs[0]); i++)
    if (kEnemySpecs[i].id == id)
      return &kEnemySpecs[i];
  return NULL;
}

static bool DecompressTiles(uint8_t compressed_id, uint8_t *dst,
                            size_t capacity, size_t *size_out) {
  size_t info = kCompressedInfoOffset + (size_t)compressed_id * 5;
  if (!RomRange(info, 5)) return false;
  size_t output_size = Read16(info);
  size_t src = LoRomOffset(Read24(info + 2));
  if (src == SIZE_MAX || output_size == 0 || output_size > capacity)
    return false;

  size_t out = 0;
  size_t groups = (output_size + 7) >> 3;
  while (groups--) {
    if (!RomRange(src, 2)) return false;
    uint16_t control = Rom8(src++);
    uint8_t repeated = Rom8(src++);
    for (int bit = 0; bit < 8 && out < output_size; bit++) {
      control <<= 1;
      if (control & 0x100) {
        if (!RomRange(src, 1)) return false;
        dst[out++] = Rom8(src++);
      } else {
        dst[out++] = repeated;
      }
    }
  }
  *size_out = output_size;
  return true;
}

static bool BuildTilePage(const EnemyIconSpec *spec, uint8_t compressed_id,
                          uint8_t *tiles, size_t capacity) {
  uint8_t decompressed[0x10000];
  size_t decompressed_size = 0;
  memset(tiles, 0, capacity);
  if (!DecompressTiles(compressed_id, decompressed, sizeof(decompressed),
                       &decompressed_size))
    return false;

  if (!spec->load_from_spec) {
    memcpy(tiles, decompressed,
           decompressed_size < capacity ? decompressed_size : capacity);
    return true;
  }

  size_t spec_ref = kTileSpecOffset + (size_t)compressed_id * 2;
  if (!RomRange(spec_ref, 2)) return false;
  size_t spec_pos = kTileSpecOffset + Read16(spec_ref);
  size_t src = 0;
  for (int guard = 0; guard < 128; guard++) {
    if (!RomRange(spec_pos, 2)) return false;
    uint8_t length_units = Rom8(spec_pos);
    if (length_units == 0)
      break;
    if (length_units == 0xff) {
      spec_pos++;
      continue;
    }
    size_t length = (size_t)length_units * 16;
    uint8_t vram_high = Rom8(spec_pos + 1);
    int page = (vram_high & 0x7f) - 0x60;
    if (page >= 0) {
      size_t dst = (size_t)page * 0x200;
      if (src < decompressed_size && dst < capacity) {
        size_t n = length;
        if (n > decompressed_size - src) n = decompressed_size - src;
        if (n > capacity - dst) n = capacity - dst;
        memcpy(tiles + dst, decompressed + src, n);
      }
    }
    if (vram_high & 0x80)
      break;
    src += length;
    spec_pos += 2;
  }
  return true;
}

static void LoadPalette(uint16_t palette_id, uint32_t palette[16]) {
  memset(palette, 0, sizeof(uint32_t) * 16);
  size_t info_ref = kPaletteInfoOffset + palette_id;
  if (!RomRange(info_ref, 2)) return;
  size_t info = kPaletteBankOffset + (Read16(info_ref) & 0x7fff);
  for (int guard = 0; guard < 32 && RomRange(info, 4) && Rom8(info) != 0;
       guard++, info += 4) {
    int count = Rom8(info);
    size_t colors = kPaletteColorBankOffset + (Read16(info + 1) & 0x7fff);
    int first = (int)Rom8(info + 3) - 0x80;
    if (count > 16 || !RomRange(colors, (size_t)count * 2)) return;
    for (int i = 0; i < count; i++) {
      int index = first + i;
      if ((unsigned)index >= 16) continue;
      uint16_t c = Read16(colors + (size_t)i * 2);
      uint8_t r = (uint8_t)((c & 31) << 3);
      uint8_t g = (uint8_t)(((c >> 5) & 31) << 3);
      uint8_t b = (uint8_t)(((c >> 10) & 31) << 3);
      palette[index] = 0xff000000u | ((uint32_t)r << 16) |
                       ((uint32_t)g << 8) | b;
    }
  }
}

static uint8_t TilePixel(const uint8_t *tiles, int tile, int x, int y) {
  if (tile < 0 || tile >= 0x800) return 0;
  const uint8_t *p = tiles + (size_t)tile * 32;
  int bit = 7 - x;
  return (uint8_t)(((p[y * 2] >> bit) & 1) |
                   (((p[y * 2 + 1] >> bit) & 1) << 1) |
                   (((p[16 + y * 2] >> bit) & 1) << 2) |
                   (((p[17 + y * 2] >> bit) & 1) << 3));
}

static void DrawTile(CachedIcon *icon, const uint8_t *tiles,
                     const uint32_t palette[16], int tile, int x, int y,
                     bool hflip, bool vflip) {
  for (int py = 0; py < 8; py++) {
    for (int px = 0; px < 8; px++) {
      int color = TilePixel(tiles, tile, hflip ? 7 - px : px,
                            vflip ? 7 - py : py);
      int dx = x + px, dy = y + py;
      if (color && (unsigned)dx < (unsigned)icon->width &&
          (unsigned)dy < (unsigned)icon->height)
        icon->pixels[dy * icon->width + dx] = palette[color];
    }
  }
}

static bool BuildIcon(uint8_t id) {
  CachedIcon *icon = &s_icons[id];
  if (icon->attempted) return icon->valid;
  icon->attempted = true;

  const EnemyIconSpec *spec = FindSpec(id);
  if (!spec || id == 0) return false;
  size_t object_info = kObjectSpriteInfoOffset + (size_t)(id - 1) * 2;
  if (!RomRange(object_info, 2)) return false;
  uint8_t sprite_id = Rom8(object_info);
  uint8_t compressed_id = Rom8(object_info + 1);
  size_t frames = LoRomOffset(Read24(kSpritePointerOffset + sprite_id * 3));
  if (frames == SIZE_MAX || !RomRange(frames + (size_t)spec->frame * 3, 3))
    return false;
  size_t arrangement = LoRomOffset(Read24(frames + (size_t)spec->frame * 3));
  if (arrangement == SIZE_MAX || !RomRange(arrangement, 1)) return false;
  int count = Rom8(arrangement);
  if (count <= 0 || count > 64 || !RomRange(arrangement + 1, (size_t)count * 4))
    return false;

  int left = 0, top = 0, right = 0, bottom = 0;
  for (int i = 0; i < count; i++) {
    const uint8_t *part = SafeRomPtr(arrangement + 1 + i * 4, 4);
    if (!part) return false;
    int x = (int8_t)part[0], y = (int8_t)part[1];
    int size = (part[3] & 0x20) ? 16 : 8;
    if (x < left) left = x;
    if (y < top) top = y;
    if (x + size > right) right = x + size;
    if (y + size > bottom) bottom = y + size;
  }
  icon->left = left;
  icon->top = top;
  icon->width = right - left;
  icon->height = bottom - top;
  if (icon->width <= 0 || icon->height <= 0 ||
      icon->width * icon->height > kMaxIconPixels)
    return false;

  uint8_t tiles[0x10000];
  uint32_t palette[16];
  if (!BuildTilePage(spec, compressed_id, tiles, sizeof(tiles))) return false;
  LoadPalette(spec->palette, palette);
  memset(icon->pixels, 0, sizeof(icon->pixels));

  for (int i = count - 1; i >= 0; i--) {
    const uint8_t *part = SafeRomPtr(arrangement + 1 + i * 4, 4);
    if (!part) return false;
    int x = (int8_t)part[0] - left;
    int y = (int8_t)part[1] - top;
    int tile = (int)part[2] + spec->tile_base;
    bool h = (part[3] & 0x40) != 0;
    bool v = (part[3] & 0x80) != 0;
    if (part[3] & 0x20) {
      int tl = tile, tr = tile + 1, bl = tile + 16, br = tile + 17;
      if (h) { int t=tl; tl=tr; tr=t; t=bl; bl=br; br=t; }
      if (v) { int t=tl; tl=bl; bl=t; t=tr; tr=br; br=t; }
      DrawTile(icon, tiles, palette, tl, x, y, h, v);
      DrawTile(icon, tiles, palette, tr, x + 8, y, h, v);
      DrawTile(icon, tiles, palette, bl, x, y + 8, h, v);
      DrawTile(icon, tiles, palette, br, x + 8, y + 8, h, v);
    } else {
      DrawTile(icon, tiles, palette, tile, x, y, h, v);
    }
  }
  icon->valid = true;
  return true;
}

static uint32_t ApplyBrightness(uint32_t color, int brightness) {
  if (brightness >= 15) return color;
  uint32_t r = ((color >> 16) & 0xff) * (uint32_t)brightness / 15;
  uint32_t g = ((color >> 8) & 0xff) * (uint32_t)brightness / 15;
  uint32_t b = (color & 0xff) * (uint32_t)brightness / 15;
  return 0xff000000u | r << 16 | g << 8 | b;
}

static void CompositeIcon(uint8_t *pixels, int width, int height,
                          const CachedIcon *icon, int center_x, int center_y,
                          int min_x, int max_x, int brightness) {
  int x0 = center_x + icon->left;
  int y0 = center_y + icon->top;
  uint32_t *dst = (uint32_t *)pixels;
  for (int y = 0; y < icon->height; y++) {
    int dy = y0 + y;
    if ((unsigned)dy >= (unsigned)height) continue;
    for (int x = 0; x < icon->width; x++) {
      int dx = x0 + x;
      uint32_t c = icon->pixels[y * icon->width + x];
      if (c && dx >= min_x && dx < max_x && (unsigned)dx < (unsigned)width)
        dst[dy * width + dx] = ApplyBrightness(c, brightness);
    }
  }
}

void MmxWidePreview_Draw(uint8_t *pixels, int width, int height, int extra) {
  /* Real margin spawning supersedes the frozen-icon preview: with the
   * WS-SPAWN/WS-CULL consumers statically compiled and active, margin
   * enemies are live simulated objects rendered through the widened OAM
   * path. Compositing the ROM-table icon on top would double-draw a
   * frozen twin over (or beside) the real enemy. */
  extern int MmxWsRealSpawnActive(void);
  if (MmxWsRealSpawnActive())
    return;
  if (!pixels || extra <= 0 || !g_rom || !g_ppu ||
      PPU_forcedBlank(g_ppu) || PPU_brightness(g_ppu) == 0)
    return;
  uint8_t stage = g_ram[0x1f7a];
  if (stage >= kPlayableStages || !LoadStageBounds(stage)) return;

  uint16_t camera_x = (uint16_t)(g_ram[0x1e4d] | (g_ram[0x1e4e] << 8));
  uint16_t camera_y = (uint16_t)(g_ram[0x1e50] | (g_ram[0x1e51] << 8));
  size_t pos = kEnemyDataBankOffset +
               (Read16(kEnemyPointerOffset + (size_t)stage * 2) & 0x7fff);
  if (!RomRange(pos, 1)) return;
  uint8_t column = Rom8(pos++);
  if (column == 0xff) return;

  /* Preview only future (right-side) type-3 enemy events. Backtracking stays
   * authoritative: defeated enemies behind the camera are never resurrected
   * visually. The original event parser remains untouched. */
  for (int guard = 0; guard < 0x200; guard++) {
    if (!RomRange(pos, 7)) break;
    uint8_t type = Rom8(pos);
    uint16_t world_y = Read16(pos + 1) & 0x7fff;
    uint8_t id = Rom8(pos + 3);
    uint16_t world_x_word = Read16(pos + 5);
    uint16_t world_x = world_x_word & 0x7fff;
    int sx = (int)world_x - (int)camera_x;
    int sy = (int)world_y - (int)camera_y;
    if (type == 3 && sx >= 256 && sx < 256 + extra &&
        sy > -128 && sy < height + 128 && BuildIcon(id))
      CompositeIcon(pixels, width, height, &s_icons[id], sx + extra, sy,
                    extra + 256, width, PPU_brightness(g_ppu));

    pos += 7;
    if (world_x_word & 0x8000) {
      if (!RomRange(pos, 1)) break;
      if (Rom8(pos) == column) break;
      column = Rom8(pos++);
    }
  }
}
