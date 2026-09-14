#include "mmx_renderer.h"
#include "mmx_display.h"
#include "mmx_wide_policy.h"
#include <math.h>

/* Mode-1 decode/composition follows SuperMetroidRecomp's sm_renderer.c.
 * The PPU remains native. Immutable raster snapshots are the only inputs to
 * presentation; this module never calls guest code or the PPU renderer. */
typedef struct Raster {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high_oam[32];
} Raster;
typedef struct Piece { int16_t x, y; uint16_t attr; uint8_t size, reserved; } Piece;
enum { MAX_PIECES = 2048 };
typedef struct Frame {
  Raster lines[224];
  uint8_t ram[0x20000];
  uint32_t stock[256 * 224];
  Piece pieces[MAX_PIECES];
  unsigned piece_count, captured;
  bool valid;
} Frame;
static Frame frame;
static Piece building[MAX_PIECES], latched[MAX_PIECES];
static unsigned building_count, latched_count;
static uint8_t building_stage, latched_stage;
static const uint8_t *rom;
static size_t rom_size;
static MmxRenderStats stats;
static uint8_t door_cache[512 * 512];
bool g_mmx_custom_renderer;
bool g_mmx_custom_hud = true;
MmxRenderAspect g_mmx_custom_aspect = MMX_ASPECT_ADAPTIVE;
MmxRenderView g_mmx_custom_view = {342, 43, 16.0 / 9.0};

MmxRenderView MmxRendererViewport(MmxRenderAspect mode, int w, int h) {
  double aspect = mode == MMX_ASPECT_16_9 ? 16.0 / 9.0 :
                  mode == MMX_ASPECT_21_9 ? 21.0 / 9.0 :
                  mode == MMX_ASPECT_32_9 ? 32.0 / 9.0 :
                  w > 0 && h > 0 ? (double)w / h : 16.0 / 9.0;
  /* Preserve CRT pixel proportions; adaptive is bounded by host capacity,
   * independently of the shared PPU's 96-pixel margin capacity. */
  aspect = fmax(4.0 / 3.0, fmin(MMX_RENDER_MAX_WIDTH / 192.0, aspect));
  int width = 2 * (int)floor(aspect * 96.0 + 0.5);
  return (MmxRenderView){width, (width - 256) / 2, aspect};
}
MmxDisplayViewport MmxRendererDestination(MmxRenderView view, int width, int height) {
  if (width <= 0 || height <= 0) return (MmxDisplayViewport){0};
  int w = width, h = (int)floor(width / view.aspect + 0.5);
  if (h > height) { h = height; w = (int)floor(height * view.aspect + 0.5); }
  return (MmxDisplayViewport){(width - w) / 2, (height - h) / 2, w, h};
}
static unsigned word(const uint8_t *p, unsigned a) { return p[a] | (p[a + 1] << 8); }
static const uint8_t *rom_at(unsigned address, size_t length) {
  if ((address & 0xffff) < 0x8000) return NULL;
  size_t offset = ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
  return rom && offset <= rom_size && length <= rom_size - offset ? rom + offset : NULL;
}
void MmxRendererSetRom(const uint8_t *bytes, size_t length) { rom = bytes; rom_size = length; }
void MmxRendererReset(void) {
  frame.valid = false; frame.captured = 0;
  building_count = latched_count = 0;
  building_stage = latched_stage = 0xff;
}
void MmxRendererRecordPiece(const uint8_t ram[0x20000], uint16_t d) {
  if (!g_mmx_custom_renderer || !ram || d > 0xffe0 || building_count >= MAX_PIECES) return;
  if (building_stage != ram[0x1f7a]) {
    building_count = 0;
    building_stage = ram[0x1f7a];
  }
  unsigned pointer = word(ram, d + 0x18) | (ram[d + 0x1a] << 16);
  const uint8_t *p = rom_at(pointer, 5);
  if (!p) return;
  int size = p[4] & 0x20 ? 16 : 8;
  int x = (int16_t)word(ram, d), y = (int16_t)word(ram, d + 2);
  x += ram[d + 0xb] & 0x40 ? -(int8_t)p[1] - size : (int8_t)p[1];
  y += ram[d + 0xb] & 0x80 ? -(int8_t)p[2] - size : (int8_t)p[2];
  unsigned attr = (((p[4] & 0xce) | ram[d + 0xf]) ^ ram[d + 0xb]) << 8;
  attr |= (p[3] + ram[d + 0x10]) & 255;
  building[building_count++] = (Piece){(int16_t)x, (int16_t)y, (uint16_t)attr, (uint8_t)size, 0};
}
void MmxRendererLatchSprites(void) {
  latched_count = building_count;
  latched_stage = building_stage;
  memcpy(latched, building, building_count * sizeof(*building));
  building_count = 0;
}
void MmxRendererBeginFrame(const uint8_t ram[0x20000]) {
  frame.valid = false; frame.captured = 0;
  memcpy(frame.ram, ram, sizeof(frame.ram));
  frame.piece_count = latched_stage == ram[0x1f7a] ? latched_count : 0;
  memcpy(frame.pieces, latched, frame.piece_count * sizeof(*latched));
}
void MmxRendererCaptureLine(const Ppu *p, unsigned line) {
  if (!p || line < 1 || line > 224 || line != frame.captured + 1) return;
  Raster *r = &frame.lines[line - 1];
  memcpy(r->registers, p, sizeof(r->registers));
  memcpy(r->palette, p->cgram, sizeof(r->palette));
  memcpy(r->oam, p->oam, sizeof(r->oam));
  memcpy(r->high_oam, p->highOam, sizeof(r->high_oam));
  memcpy(r->vram, p->vram, sizeof(r->vram));
  ++frame.captured;
}
bool MmxRendererEndFrame(const uint32_t stock[256 * 224]) {
  if (!stock || frame.captured != 224) return false;
  memcpy(frame.stock, stock, sizeof(frame.stock));
  return frame.valid = true;
}
MmxRenderStats MmxRendererGetStats(void) { return stats; }
const uint32_t *MmxRendererStockFrame(void) { return frame.valid ? frame.stock : NULL; }
bool MmxRendererSaveCapture(const char *path) {
  if (!frame.valid || !path) return false;
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  uint32_t header[] = {0x4d4d5843, 1, sizeof(frame)};
  bool ok = fwrite(header, sizeof(header), 1, f) == 1 && fwrite(&frame, sizeof(frame), 1, f) == 1;
  return fclose(f) == 0 && ok;
}
bool MmxRendererLoadCapture(const char *path) {
  if (!path) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint32_t h[3];
  frame.valid = false;
  bool ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == 0x4d4d5843 &&
      h[1] == 1 && h[2] == sizeof(frame) && fread(&frame, sizeof(frame), 1, f) == 1 &&
      frame.captured == 224 && frame.piece_count <= MAX_PIECES && frame.valid && fgetc(f) == EOF;
  fclose(f); frame.valid = ok; return ok;
}

bool MmxRendererStageTile(const uint8_t ram[0x20000], unsigned layer,
                          int x, int y, uint16_t *entry) {
  if (!ram || !entry || layer > 1 || x < 0 || y < 0 || x >= 8192 || y >= 8192) return false;
  unsigned layout = layer ? 0xec00 : 0xe800;
  unsigned screens = layer ? 0xa600 : 0x2000;
  unsigned screen = ram[layout + (y >> 8) * 32 + (x >> 8)];
  unsigned address = (screens + screen * 512 + ((y & 0xf0) << 1) + ((x & 0xf0) >> 3)) & 0xffff;
  unsigned metatile = word(ram, address);
  unsigned pointer = 0xb95 + layer * 3;
  unsigned table = word(ram, pointer) | (ram[pointer + 2] << 16);
  address = (table & 0xff0000) | ((table + metatile * 8 + ((y & 8) ? 4 : 0) + ((x & 8) ? 2 : 0)) & 0xffff);
  const uint8_t *tile = rom_at(address, 2);
  if (!tile) return false;
  *entry = (uint16_t)word(tile, 0); return true;
}
static bool door_body(int x, int y) {
  if (x < 0 || y < 0 || x >= 8192 || y >= 8192) return false;
  unsigned key = (y >> 4) * 512 + (x >> 4);
  if (door_cache[key]) return door_cache[key] == 2;
  door_cache[key] = 1;
  x &= ~15; y &= ~15;
  for (int row = 0; row < 3; ++row) {
    uint16_t entries[3][4];
    bool valid = true;
    for (int r = 0; r < 3; ++r) for (int q = 0; q < 4; ++q)
      valid &= MmxRendererStageTile(frame.ram, 0, x + (q & 1) * 8,
                                     y + (r - row) * 16 + (q >> 1) * 8, &entries[r][q]);
    if (valid && MmxWidePolicy_IsBossDoorBody(entries, row)) { door_cache[key] = 2; return true; }
  }
  return false;
}
static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, unsigned bpp) {
  unsigned a = (address + y) & 0x7fff, shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) { bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8); }
  return pixel;
}
static uint16_t background(const Ppu *p, const Raster *r, unsigned layer, int x, int y, bool stage) {
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  unsigned bpp = layer == 2 ? 2 : 4, size = PPU_bigTiles(p, layer) ? 16 : 8;
  int px = (x + p->hScroll[layer]) & 1023, py = (y + p->vScroll[layer]) & 1023;
  unsigned sc = p->bgXsc[layer], tx = px / size, ty = py / size;
  unsigned a = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
  if ((sc & 1) && (tx & 32)) a += 1024;
  if ((sc & 2) && (ty & 32)) a += (sc & 1) ? 2048 : 1024;
  uint16_t tile = r->vram[a & 0x7fff];
  bool roof_sky = layer == 1 && frame.ram[0x1f7a] == 5 && word(frame.ram, 0x1e50) >= 0x300;
  if (stage && size == 8 && layer < 2 && !roof_sky && (x < 0 || x >= 256)) {
    int wx, wy;
    if (layer == 0) {
      wx = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d), p->hScroll[0]) + x;
      wy = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50), p->vScroll[0]) + y;
    } else {
      int stream_x = word(frame.ram, 0x1e8d), stream_y = word(frame.ram, 0x1e90);
      wx = stream_x + (((p->hScroll[1] - stream_x + 512) & 1023) - 512) + x;
      wy = stream_y + (((p->vScroll[1] - stream_y + 512) & 1023) - 512) + y;
    }
    /* Reconstruct prepared map data independently of circular VRAM history.
     * Outside authored terrain, reflect only the background edge. */
    if (wx < 0) wx = -wx - 1;
    if (layer == 0) {
      unsigned stage_id = frame.ram[0x1f7a];
      if (stage_id < 13 && rom_size > 0x30d24 + stage_id * 3 + 2) {
        const uint8_t *t = rom + 0x30d24 + stage_id * 3;
        const uint8_t *bounds = rom_at(word(t, 0) | (t[2] << 16), 2);
        if (bounds && bounds[0] && bounds[0] <= 32) {
          int width = bounds[0] * 256;
          if (wx >= width) wx = width * 2 - wx - 1;
          if (wx < 0) wx = 0;
        }
      }
      if (stage_id == 1 && word(frame.ram, 0x1e4d) >= 0xa70 && word(frame.ram, 0x1e4d) < 0xac0 &&
          y >= 0x50 && y < 0xb0 && wx >= 0xbc0 && wx < 0xc40) wx -= 256;
      if (door_body(wx, wy) && (door_body(wx - 16, wy) || door_body(wx + 16, wy))) {
        int step = x < 0 ? -16 : 16;
        /* Skip the whole authored pair, retaining the native scripted door. */
        for (int i = 0; i < 2 && door_body(wx, wy); ++i) wx += step;
      }
    }
    uint16_t mapped;
    if (MmxRendererStageTile(frame.ram, layer, wx, wy, &mapped)) { tile = mapped; px = wx; py = wy; }
  }
  int cx = px & (size - 1), cy = py & (size - 1);
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + cy / 8 * 16) & 1023;
  unsigned pixel = tile_pixel(r->vram, PPU_bgTileAdr(p, layer) + number * bpp * 4, cx & 7, cy & 7, bpp);
  if (!pixel) return 0;
  unsigned priority = tile & 0x2000 ? (layer == 2 && (p->bgmode & 8) ? 15 : high[layer]) : low[layer];
  return (uint16_t)((priority << 12) | (layer << 8) | (((tile >> 10) & 7) << bpp) | pixel);
}
static void sprite(const Ppu *p, const Raster *r, int x, int sy, unsigned attr, int size,
                    int y, MmxRenderView view, uint16_t *out, bool margins_only) {
  int row = (y - sy) & 255;
  if (row >= size) return;
  if (attr & 0x8000) row = size - 1 - row;
  unsigned base = (p->obsel & 7) * 8192;
  if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
  unsigned z = ((((attr >> 12) & 3) * 4 + 2) << 12) |
               ((attr & 0x800 ? 4 : 6) << 8) | (128 + ((attr >> 9) & 7) * 16);
  for (int c = 0; c < size; ++c) {
    int dx = x + c, dest = dx + view.extra;
    if (dest < 0 || dest >= view.width || (margins_only && dx >= 0 && dx < 256)) continue;
    int cx = attr & 0x4000 ? size - 1 - c : c;
    unsigned tile = (((((attr & 255) >> 4) + row / 8) & 15) << 4) | (((attr & 15) + cx / 8) & 15);
    unsigned pixel = tile_pixel(r->vram, base + tile * 16, cx & 7, row & 7, 4);
    if (pixel) { out[dest] = (uint16_t)(z | pixel); if (margins_only) ++stats.margin_sprite_pixels; }
  }
}
static bool window(const Ppu *p, int layer, int x, int extra) {
  unsigned flags = (p->windowsel >> (layer * 4)) & 15;
  int l1 = p->window1left == 0 ? -extra : p->window1left;
  int r1 = p->window1right == 255 ? 255 + extra : p->window1right;
  int l2 = p->window2left == 0 ? -extra : p->window2left;
  int r2 = p->window2right == 255 ? 255 + extra : p->window2right;
  bool a = (x >= l1 && x <= r1) != ((flags & 1) != 0);
  bool b = (x >= l2 && x <= r2) != ((flags & 4) != 0);
  if (!(flags & 2)) return (flags & 8) && b;
  if (!(flags & 8)) return a;
  switch ((p->wbgobjlog >> (layer * 2)) & 3) {
    case 0: return a || b; case 1: return a && b; case 2: return a != b; default: return a == b;
  }
}
static bool condition(unsigned mode, bool inside) { return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside); }
static uint32_t colour(const Ppu *p, const Raster *r, const uint8_t brightness[32], uint16_t main, uint16_t sub, bool inside) {
  unsigned rgb = r->palette[main & 255], layer = (main >> 8) & 15;
  bool clipped = condition(p->cgwsel >> 6, inside);
  bool math = !condition((p->cgwsel >> 4) & 3, inside) && ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) { if (sub & 255) other = r->palette[sub & 255]; else half = false; }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) { int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    result |= (uint32_t)brightness[c] << (16 - component * 8);
  }
  return result;
}
bool MmxRendererDraw(uint32_t *out, MmxRenderView view, bool hud) {
  if (!out || !frame.valid || view.width < 256 || view.width > MMX_RENDER_MAX_WIDTH ||
      view.extra != (view.width - 256) / 2 || (view.width & 1)) return false;
  memset(&stats, 0, sizeof(stats)); stats.pieces = frame.piece_count;
  memset(door_cache, 0, sizeof(door_cache));
  memset(out, 0, (size_t)view.width * 224 * sizeof(*out));
  bool stage = frame.ram[0xd1] == 2 && frame.ram[0xd2] == 4;
  for (int y = 0; y < 224; ++y) {
    const Raster *r = &frame.lines[y]; Ppu p;
    memcpy(&p, r->registers, PPU_SAVESTATE_REGS_SIZE);
    if ((p.bgmode & 7) != 1 || !stage) {
      memcpy(out + y * view.width + view.extra, frame.stock + y * 256, 256 * sizeof(*out));
      ++stats.fallback_lines; continue;
    }
    ++stats.custom_lines;
    if (p.inidisp & 128) continue;
    uint8_t brightness[32];
    for (int c = 0; c < 32; ++c) brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (p.inidisp & 15) / 15);
    uint16_t objects[MMX_RENDER_MAX_WIDTH] = {0};
    for (int i = (int)frame.piece_count - 1; i >= 0; --i) {
      Piece s = frame.pieces[i];
      sprite(&p, r, s.x, s.y, s.attr, s.size, y, view, objects, true);
    }
    int bar_first = -1, bar_count = 0;
    if (hud) for (int slot = 16; slot <= 48; ++slot) {
      unsigned pos = r->oam[slot * 2], attr = r->oam[slot * 2 + 1];
      unsigned tile = attr & 255, hi = (r->high_oam[slot / 4] >> (slot % 4 * 2)) & 1;
      bool bar = !hi && (pos & 255) >= 216 && (pos >> 8) < 96 && ((attr >> 9) & 7) == 2 &&
          (tile == 128 || tile == 130 || tile == 132 || tile == 134 || tile == 170);
      if (bar) { if (bar_first < 0) bar_first = slot; ++bar_count; }
      else if (bar_first >= 0) break;
    }
    static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
    for (int slot = 127; slot >= 0; --slot) {
      unsigned pos = r->oam[slot * 2], attr = r->oam[slot * 2 + 1];
      unsigned hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
      int x = (pos & 255) | ((hi & 1) << 8), sy = pos >> 8;
      if (x >= 256) x -= 512;
      int size = sizes[p.obsel >> 5][(hi >> 1) & 1];
      if (x + size <= 0 || x >= 256) continue;
      bool anchored = hud && sy < 96 && (slot < 16 || (bar_count >= 4 && slot >= bar_first && slot < bar_first + bar_count));
      if (anchored) { if (x < 25) x -= view.extra; else if (x >= 216) x += view.extra; }
      sprite(&p, r, x, sy, attr, size, y, view, objects, false);
    }
    for (int sx = 0; sx < view.width; ++sx) {
      int x = sx - view.extra;
      uint16_t screens[2] = {0x500, 0x500}, bg[3] = {0};
      for (int layer = 0; layer < 3; ++layer) if ((p.screenEnabled[0] | p.screenEnabled[1]) & (1 << layer)) {
        int bx = x, by = y + 1;
        if (p.mosaic & (1 << layer)) { int size = (p.mosaic >> 4) + 1;
          bx -= ((bx % size) + size) % size; by -= by % size; }
        bg[layer] = background(&p, r, layer, bx, by, stage);
      }
      for (int sub = 0; sub < 2; ++sub) {
        for (int layer = 0; layer < 3; ++layer)
          if ((p.screenEnabled[sub] & (1 << layer)) &&
              (!(p.screenWindowed[sub] & (1 << layer)) || !window(&p, layer, x, view.extra)) && bg[layer] > screens[sub]) screens[sub] = bg[layer];
        if ((p.screenEnabled[sub] & 16) && (!(p.screenWindowed[sub] & 16) || !window(&p, 4, x, view.extra)) &&
            objects[sx] > screens[sub]) screens[sub] = objects[sx];
      }
      out[y * view.width + sx] = colour(&p, r, brightness, screens[0], screens[1], window(&p, 5, x, view.extra));
    }
  }
  return true;
}
