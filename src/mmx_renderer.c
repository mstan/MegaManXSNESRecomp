#include "mmx_renderer.h"
#include "mmx_display.h"
#include "mmx_wide_policy.h"
#include "mmx_render_assets.h"
#include <math.h>

/* Mode-1 decode/composition follows SuperMetroidRecomp's sm_renderer.c.
 * The PPU remains native. Immutable raster snapshots are the only inputs to
 * presentation; this module never calls guest code or the PPU renderer. */
typedef struct Raster {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high_oam[32];
} Raster;
typedef struct Piece {
  int16_t x, y; uint16_t attr; uint8_t size, animation;
  uint8_t tile, palette_bits; uint16_t object;
} Piece;
enum { MAX_PIECES = 2048 };
typedef struct Frame {
  Raster lines[224];
  uint8_t ram[0x20000];
  uint32_t stock[256 * 224];
  Piece pieces[MAX_PIECES];
  unsigned piece_count, captured;
  bool valid;
  Piece expanded[MAX_PIECES];
  unsigned expanded_count;
  bool expand;
} Frame;
static Frame frame;
static Piece building[MAX_PIECES], latched[MAX_PIECES];
static unsigned building_count, latched_count;
static uint8_t building_stage, latched_stage;
static Piece expanded_building[MAX_PIECES], expanded_latched[MAX_PIECES];
static unsigned expanded_building_count, expanded_latched_count;
static uint16_t current_object;
static bool observed_lists;
static const uint8_t *rom;
static size_t rom_size;
static MmxRenderStats stats;
static uint8_t door_cache[512 * 512];
static int airport_sky_width;
typedef struct BuriedBody { int left, top, bottom; } BuriedBody;
static BuriedBody buried_bodies[16];
static unsigned buried_count;
bool g_mmx_custom_renderer;
bool g_mmx_custom_hud = true;
bool g_mmx_expanded_sprites;
bool g_mmx_render_asset_repairs = true;
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
void MmxRendererSetRom(const uint8_t *bytes, size_t length) {
  rom = bytes; rom_size = length; MmxRenderAssetsSetRom(bytes, length);
}
void MmxRendererReset(void) {
  frame.valid = false; frame.captured = 0;
  building_count = latched_count = 0;
  building_stage = latched_stage = 0xff;
  expanded_building_count = expanded_latched_count = 0;
  current_object = 0; observed_lists = false;
}
static Piece make_piece(const uint8_t *p, int x, int y, unsigned flip,
                        unsigned attributes, unsigned base, unsigned animation, unsigned object) {
  int size = p[4] & 0x20 ? 16 : 8;
  x += flip & 0x40 ? -(int8_t)p[1] - size : (int8_t)p[1];
  y += flip & 0x80 ? -(int8_t)p[2] - size : (int8_t)p[2];
  unsigned attr = (((p[4] & 0xce) | attributes) ^ flip) << 8;
  attr |= (p[3] + base) & 255;
  return (Piece){(int16_t)x, (int16_t)y, (uint16_t)attr, (uint8_t)size,
      (uint8_t)animation, p[3], (uint8_t)(p[4] & 14), (uint16_t)object};
}
static void expand_object(const uint8_t *ram, unsigned object) {
  if (object < 0x20 || object > 0x1fe0) return;
  unsigned animation = ram[object + 0x16], f = ram[object + 0x17] & 127;
  const uint8_t *pointer = rom_at(0x8d8000 + animation * 3, 3);
  if (!pointer) return;
  unsigned address = word(pointer, 0) | (pointer[2] << 16);
  pointer = rom_at(address + f * 3, 3);
  if (!pointer) return;
  address = word(pointer, 0) | (pointer[2] << 16);
  const uint8_t *arrangement = rom_at(address, 1);
  if (!arrangement || !rom_at(address, 1 + arrangement[0] * 4)) return;
  int x = (int16_t)(word(ram, object + 5) - word(ram, 0x1e4d));
  int y = (int16_t)(word(ram, object + 8) + (int8_t)ram[object + 0x19] - word(ram, 0x1e50));
  unsigned base = MmxWidePolicy_CrusherTileBase(ram, (uint16_t)object, ram[object + 0x18]);
  for (unsigned i = 0; i < arrangement[0] && expanded_building_count < MAX_PIECES; ++i)
    expanded_building[expanded_building_count++] = make_piece(arrangement + i * 4, x, y,
        ram[object + 0x11] & 0x40, ram[object + 0x11] & 0x3f, base, animation, object);
}
void MmxRendererObserveObject(const uint8_t ram[0x20000], uint16_t object) {
  if (!g_mmx_custom_renderer || !ram) return;
  if (building_stage != ram[0x1f7a]) {
    building_count = expanded_building_count = 0; observed_lists = false;
    building_stage = ram[0x1f7a];
  }
  current_object = object;
  if (observed_lists || !g_mmx_expanded_sprites) return;
  observed_lists = true;
  /* D56F's actual six priority queues, captured before D6A7 can exhaust OAM.
   * Keep its order: queues 0..2, weapon objects, X, queues 3..5. No arbitrary
   * scan of dormant object slots and no additional guest objects or writes. */
  for (unsigned group = 0; group < 6; ++group) {
    if (group == 3) {
      for (unsigned d = 0xc38; d <= 0xc78; d += 0x20)
        if (ram[d] && ram[d + 14]) expand_object(ram, d);
      if (ram[0xbb6]) expand_object(ram, 0xba8);
    }
    unsigned count = ram[0xe7 + group];
    if (count > 32) count = 32;
    for (unsigned i = 0; i < count; ++i) expand_object(ram, word(ram, 0x920 + group * 64 + i * 2));
  }
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
  int x = (int16_t)word(ram, d), y = (int16_t)word(ram, d + 2);
  unsigned animation = current_object && current_object < 0x1fe0 ? ram[current_object + 0x16] : 255;
  building[building_count++] = make_piece(p, x, y, ram[d + 0xb], ram[d + 0xf],
                                         ram[d + 0x10], animation, current_object);
}
void MmxRendererLatchSprites(void) {
  latched_count = building_count;
  latched_stage = building_stage;
  memcpy(latched, building, building_count * sizeof(*building));
  building_count = 0;
  expanded_latched_count = expanded_building_count;
  memcpy(expanded_latched, expanded_building, expanded_building_count * sizeof(Piece));
  expanded_building_count = 0; observed_lists = false; current_object = 0;
}
static void trace_objects(const uint8_t *ram) {
  static FILE *log;
  static bool checked;
  static unsigned tick, previous[16];
  ++tick;
  if (!checked) {
    checked = true;
    const char *path = getenv("MMX_RENDER_OBJECT_TRACE");
    if (path && *path) log = fopen(path, "w");
    if (log) fputs("frame,object,state,camera,player_x,enemy_x,enemy_y,pieces,stage,id,health,player_y\n", log);
  }
  unsigned stage = ram[0x1f7a];
  if (!log) return;
  for (unsigned i = 0; i < 16; ++i) {
    unsigned d = i == 15 ? 0xe18 : 0xe68 + i * 64;
    bool selected = i == 15 ? stage == 8 :
        MmxWidePolicy_IsBossEncounter(ram[d + 10]) ||
        (stage == 8 && ram[d + 10] == 0x36) || (stage == 6 && ram[d + 10] == 0x37);
    unsigned state = ram[d] && selected ? ram[d + 1] + 1u : 0;
    if (state == previous[i] && (!state || (tick & 15))) continue;
    previous[i] = state;
    fprintf(log, "%u,%04x,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", tick, d, (int)state - 1,
        word(ram, 0x1e4d), word(ram, 0xbad), word(ram, d + 5), word(ram, d + 8), latched_count,
        stage, ram[d + 10], ram[d + 0x27], word(ram, 0xbb0));
    fflush(log);
  }
}
void MmxRendererBeginFrame(const uint8_t ram[0x20000]) {
  trace_objects(ram);
  frame.valid = false; frame.captured = 0;
  memcpy(frame.ram, ram, sizeof(frame.ram));
  frame.piece_count = latched_stage == ram[0x1f7a] ? latched_count : 0;
  memcpy(frame.pieces, latched, frame.piece_count * sizeof(*latched));
  frame.expanded_count = latched_stage == ram[0x1f7a] ? expanded_latched_count : 0;
  memcpy(frame.expanded, expanded_latched, frame.expanded_count * sizeof(Piece));
  frame.expand = g_mmx_expanded_sprites;
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
  uint32_t header[] = {0x4d4d5843, 2, sizeof(frame)};
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
      h[1] == 2 && h[2] == sizeof(frame) && fread(&frame, sizeof(frame), 1, f) == 1 &&
      frame.captured == 224 && frame.piece_count <= MAX_PIECES && frame.expanded_count <= MAX_PIECES && frame.valid && fgetc(f) == EOF;
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
static void prepare_stage_planes(void) {
  airport_sky_width = 0; buried_count = 0;
  if (frame.ram[0x1f7a] == 5 && frame.ram[0x1e89] == 0x0e &&
      word(frame.ram, 0x1e90) == 0 && word(frame.ram, 0x1e50) >= 0x300) {
    /* The airport panorama ends partway through screen 2 (640 pixels in
     * the retail map). Later cells belong to other mechanisms and contain
     * intentional holes. Discover the continuous sky band from its top row
     * and reflect that edge, instead of exposing those unpainted cells. */
    unsigned x; uint16_t tile;
    for (x = 0; x < 1024; x += 8)
      if (!MmxRendererStageTile(frame.ram, 1, x, 0, &tile) || !(tile & 1023)) break;
    if (x >= 256 && x < 1024) airport_sky_width = (int)x;
  }
  if (frame.ram[0x1f7a] != 1) return;
  for (unsigned d = 0xe68; d <= 0x1228; d += 64) {
    const uint8_t *r = frame.ram;
    if (!r[d] || r[d + 10] != 0x21 || !(r[d + 11] & 0x80) ||
        r[d + 1] != 0 || r[d + 2] == 4) continue;
    unsigned variant = r[d + 11] & 0x7f;
    if (variant >= 3) continue;
    /* $82:AE81 / $86:CBEC describe the buried submarine's BG1 body.
     * State 4 starts its real rise. Before that, the source-art rectangle
     * must stay empty even when an adaptive margin can already see it. */
    const uint8_t *top = rom_at(0x86cbec + variant * 2, 2);
    const uint8_t *bottom = rom_at(0x86cbf2 + variant * 2, 2);
    if (!top || !bottom || word(bottom, 0) < word(top, 0)) continue;
    buried_bodies[buried_count++] = (BuriedBody){
        ((int)word(r, d + 5) + 16) & ~31, (int)word(top, 0), (int)word(bottom, 0) + 1};
  }
}
static uint16_t background(const Ppu *p, const Raster *r, unsigned layer, int x, int y, bool stage, int *private_color) {
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  bool margin = x < 0 || x >= 256;
  /* Launch's BG3 water plane is blended over the world on the subscreen.
   * Repeat that plane while retaining the vertical waterline/scroll. Other
   * BG3 uses, including dialogue, stay within their native screen bounds. */
  bool water = frame.ram[0x1f7a] == 1 && (p->screenEnabled[0] & 4) &&
      !(p->screenEnabled[1] & 4) && (p->cgwsel & 2) && (p->cgadsub & 0x44) == 0x44;
  if (stage && layer == 2 && margin && !water) return 0;
  /* Spark's BG2 mode $0C is the Thunder Slimer actor surface, not the
   * scrolling level map. The retained map contains its staging tiles;
   * extending those outside the native arena duplicates dormant bubbles. */
  if (stage && layer == 1 && margin && frame.ram[0x1f7a] == 6 && frame.ram[0x1e89] == 0x0c) return 0;
  int asset_x = -1;
  unsigned bpp = layer == 2 ? 2 : 4, size = PPU_bigTiles(p, layer) ? 16 : 8;
  int px = (x + p->hScroll[layer]) & 1023, py = (y + p->vScroll[layer]) & 1023;
  unsigned sc = p->bgXsc[layer], tx = px / size, ty = py / size;
  unsigned a = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
  if ((sc & 1) && (tx & 32)) a += 1024;
  if ((sc & 2) && (ty & 32)) a += (sc & 1) ? 2048 : 1024;
  uint16_t tile = r->vram[a & 0x7fff];
  if (stage && size == 8 && layer < 2 && (x < 0 || x >= 256)) {
    int wx, wy;
    if (layer == 0) {
      wx = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d), p->hScroll[0]) + x;
      wy = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50), p->vScroll[0]) + y;
    } else {
      int stream_x = word(frame.ram, 0x1e8d), stream_y = word(frame.ram, 0x1e90);
      wx = stream_x + (((p->hScroll[1] - stream_x + 512) & 1023) - 512) + x;
      wy = stream_y + (((p->vScroll[1] - stream_y + 512) & 1023) - 512) + y;
      /* Highway's final arena switches to the sky plane at BG2 x=$A00.
       * Earlier columns are intentionally empty at this vertical scroll;
       * extend the arena's sky edge when a wide view reaches behind it. */
      if (frame.ram[0x1f7a] == 0 && stream_x >= 0xa00 && wx < 0xa00) wx = 0xa00;
    }
    /* Reconstruct prepared map data independently of circular VRAM history.
     * Outside authored terrain, reflect only the background edge. */
    if (wx < 0) wx = -wx - 1;
    if (layer == 1 && airport_sky_width && wx >= airport_sky_width) {
      wx %= 2 * airport_sky_width;
      if (wx >= airport_sky_width) wx = 2 * airport_sky_width - wx - 1;
    }
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
      for (unsigned i = 0; i < buried_count; ++i) {
        const BuriedBody *body = &buried_bodies[i];
        if (wx >= body->left && wx < body->left + 128 && wy >= body->top && wy < body->bottom) {
          wx -= 256; break; /* The preceding water screen has no source body. */
        }
      }
      if (door_body(wx, wy)) {
        bool left = door_body(wx - 16, wy), right = door_body(wx + 16, wy);
        if (left || right) {
          int boundary = (wx & ~15) + (right ? 16 : 0);
          bool view_left = word(frame.ram, 0x1e4d) + 128 < (unsigned)boundary;
          /* Retain the column facing the current room. Only its duplicate
           * samples the neighboring wall; a distant closed door stays visible. */
          if (view_left && left) wx += 16;
          if (!view_left && right) wx -= 16;
        }
      }
    }
    uint16_t mapped;
    if (MmxRendererStageTile(frame.ram, layer, wx, wy, &mapped)) {
      tile = mapped; px = wx; py = wy;
      /* The city moves at half speed. Express its map column as the player
       * X at which it crosses the native view's center (camera+128). */
      asset_x = layer == 1 && frame.ram[0x1f7a] == 0 ? wx * 2 - 128 : wx;
      /* Chill's BG2 sky palette also changes with elevation. Its cave-exit
       * X transition owns foreground art only; keep the live sky colors. */
      if (layer == 1 && frame.ram[0x1f7a] == 8) asset_x = -1;
    }
  }
  int cx = px & (size - 1), cy = py & (size - 1);
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + cy / 8 * 16) & 1023;
  unsigned address = (PPU_bgTileAdr(p, layer) + number * bpp * 4) & 0x7fff;
  const uint8_t *bits = g_mmx_render_asset_repairs && bpp == 4 ?
      MmxRenderAssetsBackgroundTile(frame.ram, asset_x, address) : NULL;
  unsigned pixel;
  if (bits) {
    bits += (cy & 7) * 2;
    unsigned shift = 7 - (cx & 7);
    pixel = ((bits[0] >> shift) & 1) | (((bits[1] >> shift) & 1) << 1) |
        (((bits[16] >> shift) & 1) << 2) | (((bits[17] >> shift) & 1) << 3);
  } else pixel = tile_pixel(r->vram, address, cx & 7, cy & 7, bpp);
  if (!pixel) return 0;
  unsigned index = (((tile >> 10) & 7) << bpp) | pixel;
  const MmxBackgroundPalette *palette = g_mmx_render_asset_repairs ?
      MmxRenderAssetsBackgroundPalette(frame.ram, asset_x) : NULL;
  if (palette && palette->valid[index]) *private_color = palette->colors[index];
  unsigned priority = tile & 0x2000 ? (layer == 2 && (p->bgmode & 8) ? 15 : high[layer]) : low[layer];
  return (uint16_t)((priority << 12) | (layer << 8) | index);
}
static void sprite(const Ppu *p, const Raster *r, int x, int sy, unsigned attr, int size,
                    int y, MmxRenderView view, uint16_t *out, bool margins_only,
                    const MmxSpriteAsset *asset, unsigned raw_tile, int *object_color,
                    bool full_coordinates) {
  int row = full_coordinates ? y - sy : (y - sy) & 255;
  if (row < 0 || row >= size) return;
  if (attr & 0x8000) row = size - 1 - row;
  unsigned base = (p->obsel & 7) * 8192;
  if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
  unsigned z = ((((attr >> 12) & 3) * 4 + 2) << 12) |
               ((attr & 0x800 ? 4 : 6) << 8) | (128 + ((attr >> 9) & 7) * 16);
  for (int c = 0; c < size; ++c) {
    int dx = x + c, dest = dx + view.extra;
    if (dest < 0 || dest >= view.width || (margins_only && dx >= 0 && dx < 256)) continue;
    int cx = attr & 0x4000 ? size - 1 - c : c;
    unsigned number = asset && !asset->live_tiles ? raw_tile : attr & 255;
    unsigned tile = ((((number >> 4) + row / 8) & 15) << 4) | (((number & 15) + cx / 8) & 15);
    unsigned pixel;
    if (asset && !asset->live_tiles) {
      const uint8_t *bits = asset->tiles + tile * 32 + (row & 7) * 2;
      unsigned shift = 7 - (cx & 7);
      pixel = ((bits[0] >> shift) & 1) | (((bits[1] >> shift) & 1) << 1) |
          (((bits[16] >> shift) & 1) << 2) | (((bits[17] >> shift) & 1) << 3);
    } else pixel = tile_pixel(r->vram, base + tile * 16, cx & 7, row & 7, 4);
    if (pixel) {
      out[dest] = (uint16_t)(z | pixel);
      object_color[dest] = asset ? asset->colors[pixel] : -1;
      if (x + c < 0 || x + c >= 256) ++stats.margin_sprite_pixels;
    }
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
typedef struct LightBeam { int left[224], right[224]; } LightBeam;
static unsigned spark_lights(LightBeam beams[2], int extra) {
  /* $87:A7F6 builds 8-bit HDMA windows from the ROM's rounded beam profile.
   * Rebuild only that color window in signed host coordinates. The native
   * generator clamps a left-moving light at zero and waits for a right-side
   * arrival to enter 256 pixels; neither limitation describes a wide view. */
  if (!g_mmx_render_asset_repairs || frame.ram[0x1f7a] != 6 ||
      frame.ram[0x1f0a] != 4 || frame.ram[0x1e89] != 2) return 0;
  const uint8_t *curve = rom_at(0x86d136, 25);
  if (!curve) return 0;
  /* Sprite/HDMA submission precedes the next camera update in frame.ram.
   * Use the captured raster scroll, as terrain reconstruction does. */
  int camera_x = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d),
      (uint16_t)word(frame.lines[0].registers, 14));
  int camera_y = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50),
      (uint16_t)word(frame.lines[0].registers, 22));
  unsigned count = 0;
  for (unsigned d = 0xe68; d <= 0x1228 && count < 2; d += 64) {
    const uint8_t *r = frame.ram;
    if (!r[d] || r[d + 10] != 0x37 || r[d + 1] != 2 || r[d + 0x1c] ||
        (r[d + 0x2d] != 0x40 && r[d + 0x2d] != 0x80)) continue;
    bool right = r[d + 0x0b] == 1, fading = r[d + 3] != 0;
    int tip = (int16_t)(word(r, d + 0x22) - camera_x) + (right ? -24 : 24);
    int top = fading ? (int16_t)word(r, d + 0x36) :
        (int16_t)(word(r, d + 0x24) - camera_y) - 24;
    unsigned height = fading ? r[d + 0x3b] : 49;
    int inset = fading ? r[d + 0x1f] : 0;
    LightBeam *beam = &beams[count++];
    for (int y = 0; y < 224; ++y) { beam->left[y] = 1; beam->right[y] = 0; }
    unsigned index = 0, remaining = curve[0]; int edge = fading ? 0 : 12;
    for (unsigned row = 0; row < height && row < 49; ++row) {
      int y = top + (int)row;
      if (y >= 0 && y < 224) {
        beam->left[y] = right ? -extra + inset : tip + edge;
        beam->right[y] = right ? tip - edge : 255 + extra - inset;
      }
      if (!fading) {
        if (remaining) --remaining;
        else if (index < 25) {
          remaining = curve[index++];
          if (remaining >= 5) { remaining -= 5; ++edge; } else --edge;
        }
      }
    }
  }
  return count;
}
static bool condition(unsigned mode, bool inside) { return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside); }
static uint32_t colour(const Ppu *p, const uint16_t *palette, const uint8_t brightness[32], uint16_t main, uint16_t sub, bool inside, int object_color, const int bg_colors[3]) {
  unsigned rgb = palette[main & 255], layer = (main >> 8) & 15;
  if (object_color >= 0 && (layer == 4 || layer == 6)) rgb = (unsigned)object_color;
  if (layer < 3 && bg_colors[layer] >= 0) rgb = (unsigned)bg_colors[layer];
  bool clipped = condition(p->cgwsel >> 6, inside);
  bool math = !condition((p->cgwsel >> 4) & 3, inside) && ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if (sub & 255) {
      unsigned sub_layer = (sub >> 8) & 15;
      other = object_color >= 0 && (sub_layer == 4 || sub_layer == 6) ? (unsigned)object_color : palette[sub & 255];
      if (sub_layer < 3 && bg_colors[sub_layer] >= 0) other = (unsigned)bg_colors[sub_layer];
    } else half = false;
  }
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
  bool stage = MmxWidePolicy_IsStageScene(frame.ram);
  prepare_stage_planes();
  LightBeam beams[2];
  unsigned beam_count = stage ? spark_lights(beams, view.extra) : 0;
  unsigned palette_fade = stage && g_mmx_render_asset_repairs ?
      MmxRenderAssetsDeathPaletteFade(frame.ram, frame.lines[0].palette) : 0;
  const Piece *pieces = frame.expand && g_mmx_render_asset_repairs ? frame.expanded : frame.pieces;
  unsigned piece_count = frame.expand && g_mmx_render_asset_repairs ? frame.expanded_count : frame.piece_count;
  const MmxSpriteAsset *piece_assets[MAX_PIECES] = {0};
  if (stage && g_mmx_render_asset_repairs) for (unsigned i = 0; i < piece_count; ++i) {
    const Piece *s = &pieces[i];
    const MmxSpriteAsset *a = MmxRenderAssetsObjectSprite(frame.ram, s->object, s->animation);
    /* Keep current allocations and their live flashes/animation. Repair
     * missing or stale bindings using the ROM resource's own palette. */
    if (a && (!a->current || (s->attr & 255) != ((s->tile + a->tile_base) & 255) ||
        ((s->attr >> 8) & 0x2f) != (unsigned)(a->attributes | s->palette_bits) ||
        (s->object == 0xe18 && MmxRenderAssetsRideArmorPalettePending(frame.ram,
            frame.lines[0].palette + 128 + ((s->attr >> 9) & 7) * 16)))) piece_assets[i] = a;
  }
  for (int y = 0; y < 224; ++y) {
    const Raster *r = &frame.lines[y]; Ppu p;
    memcpy(&p, r->registers, PPU_SAVESTATE_REGS_SIZE);
    if (beam_count) p.cgwsel = (p.cgwsel & 0xcf) | 0x20;
    if ((p.bgmode & 7) != 1 || !stage) {
      memcpy(out + y * view.width + view.extra, frame.stock + y * 256, 256 * sizeof(*out));
      ++stats.fallback_lines; continue;
    }
    ++stats.custom_lines;
    if (p.inidisp & 128) continue;
    uint8_t brightness[32];
    for (int c = 0; c < 32; ++c) brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (p.inidisp & 15) / 15);
    uint16_t objects[MMX_RENDER_MAX_WIDTH] = {0};
    int object_colors[MMX_RENDER_MAX_WIDTH];
    for (int x = 0; x < view.width; ++x) object_colors[x] = -1;
    bool replaced[128] = {false};
    for (int i = (int)piece_count - 1; i >= 0; --i) {
      Piece s = pieces[i]; const MmxSpriteAsset *asset = piece_assets[i];
      /* Recorded pieces already obey the retail submission budget. Draw
       * their entire footprint, including x=255 which native D76A clips.
       * Only the explicit expanded list can add pieces beyond that budget. */
      bool center = g_mmx_render_asset_repairs;
      if (g_mmx_render_asset_repairs) for (int slot = 16; slot < 128; ++slot) {
        unsigned pos = r->oam[slot * 2], hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
        int ox = (pos & 255) | ((hi & 1) << 8); if (ox >= 256) ox -= 512;
        if (ox == s.x && (pos >> 8) == ((unsigned)s.y & 255) && r->oam[slot * 2 + 1] == s.attr) {
          replaced[slot] = true; center = true;
        }
      }
      unsigned attr = asset ? (s.attr & 0xd000) | 0x2000 | ((asset->attributes & 15) << 8) |
          (asset->live_tiles ? s.attr & 255 : 0) : s.attr;
      sprite(&p, r, s.x, s.y, attr, s.size, y, view, objects, !center, asset, s.tile, object_colors, true);
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
      if (replaced[slot]) continue;
      unsigned pos = r->oam[slot * 2], attr = r->oam[slot * 2 + 1];
      unsigned hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
      int x = (pos & 255) | ((hi & 1) << 8), sy = pos >> 8;
      if (x >= 256) x -= 512;
      int size = sizes[p.obsel >> 5][(hi >> 1) & 1];
      if (x + size <= 0 || x >= 256) continue;
      bool anchored = hud && sy < 96 && (slot < 16 || (bar_count >= 4 && slot >= bar_first && slot < bar_first + bar_count));
      if (anchored) { if (x < 25) x -= view.extra; else if (x >= 216) x += view.extra; }
      sprite(&p, r, x, sy, attr, size, y, view, objects, false, NULL, 0, object_colors, false);
    }
    for (int sx = 0; sx < view.width; ++sx) {
      int x = sx - view.extra;
      uint16_t screens[2] = {0x500, 0x500}, bg[3] = {0};
      int bg_colors[3] = {-1, -1, -1};
      for (int layer = 0; layer < 3; ++layer) if ((p.screenEnabled[0] | p.screenEnabled[1]) & (1 << layer)) {
        int bx = x, by = y + 1;
        if (p.mosaic & (1 << layer)) { int size = (p.mosaic >> 4) + 1;
          bx -= ((bx % size) + size) % size; by -= by % size; }
        bg[layer] = background(&p, r, layer, bx, by, stage, &bg_colors[layer]);
      }
      if (palette_fade) {
        for (int layer = 0; layer < 3; ++layer) if (bg_colors[layer] >= 0)
          bg_colors[layer] = MmxRenderAssetsFadeColor((uint16_t)bg_colors[layer], palette_fade);
        if (object_colors[sx] >= 0)
          object_colors[sx] = MmxRenderAssetsFadeColor((uint16_t)object_colors[sx], palette_fade);
      }
      for (int sub = 0; sub < 2; ++sub) {
        for (int layer = 0; layer < 3; ++layer)
          if ((p.screenEnabled[sub] & (1 << layer)) &&
              (!(p.screenWindowed[sub] & (1 << layer)) || !window(&p, layer, x, view.extra)) && bg[layer] > screens[sub]) screens[sub] = bg[layer];
        if ((p.screenEnabled[sub] & 16) && (!(p.screenWindowed[sub] & 16) || !window(&p, 4, x, view.extra)) &&
            objects[sx] > screens[sub]) screens[sub] = objects[sx];
      }
      bool color_window = window(&p, 5, x, view.extra);
      if (beam_count) {
        color_window = false;
        for (unsigned i = 0; i < beam_count; ++i)
          color_window |= x >= beams[i].left[y] && x <= beams[i].right[y];
      }
      out[y * view.width + sx] = colour(&p, r->palette, brightness, screens[0], screens[1], color_window, object_colors[sx], bg_colors);
    }
  }
  return true;
}
