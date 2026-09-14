#include "mmx_render_assets.h"
#include <string.h>

/* The same ROM compression/transfer format used by mmx_wide_preview.c,
 * applied to live animation pieces, without allocating guest VRAM/CGRAM. */
static const uint8_t *rom;
static size_t rom_size;
static MmxSpriteAsset assets[256];
static uint8_t ready[256], sprite_resource[256];
static unsigned cached_stage = ~0u, cached_section = ~0u;
static bool range(size_t a, size_t n) { return a <= rom_size && n <= rom_size - a; }
static unsigned word(size_t a) { return range(a, 2) ? rom[a] | (rom[a + 1] << 8) : 0; }
static size_t lorom(unsigned a) { return ((a >> 16) & 127) * 0x8000u + (a & 0x7fff); }
static unsigned ram_word(const uint8_t *r, unsigned a) { return r[a] | (r[a + 1] << 8); }

void MmxRenderAssetsSetRom(const uint8_t *bytes, size_t size) {
  if (rom == bytes && rom_size == size) return;
  rom = bytes; rom_size = size;
  cached_stage = cached_section = ~0u;
}
static bool tiles(unsigned id, uint8_t out[8192]) {
  uint8_t decoded[65536];
  size_t info = 0x376f7 + id * 5;
  if (!range(info, 5)) return false;
  size_t count = word(info), pos = lorom(word(info + 2) | (rom[info + 4] << 16));
  if (!count || count > sizeof(decoded)) return false;
  for (size_t n = 0; n < count;) {
    if (!range(pos, 2)) return false;
    unsigned control = rom[pos++], repeat = rom[pos++];
    for (unsigned bit = 128; bit && n < count; bit >>= 1) {
      if ((control & bit) && !range(pos, 1)) return false;
      decoded[n++] = (uint8_t)((control & bit) ? rom[pos++] : repeat);
    }
  }
  size_t spec = 0x371b7 + word(0x371b7 + id * 2), source = 0;
  memset(out, 0, 8192);
  for (unsigned guard = 0; guard < 256; ++guard) {
    if (!range(spec, 2)) return false;
    unsigned length = rom[spec], destination = rom[spec + 1];
    if (!length) return true;
    if (length == 255) { ++spec; continue; }
    length *= 16;
    if (source + length > count) return false;
    int address = ((int)(destination & 127) - 0x60) * 512;
    if (address >= 0 && address + (int)length <= 8192)
      memcpy(out + address, decoded + source, length);
    source += length; spec += 2;
    if (destination & 128) return true;
  }
  return false;
}
static bool palette(unsigned id, uint16_t out[16]) {
  size_t p = 0x30000 + (word(0x30133 + id) & 0x7fff);
  bool found = false;
  memset(out, 0, 32);
  for (unsigned guard = 0; guard < 32; ++guard, p += 4) {
    if (!range(p, 4)) return false;
    unsigned count = rom[p];
    if (!count) return found;
    size_t source = 0x28000 + (word(p + 1) & 0x7fff);
    int first = (int)rom[p + 3] - 128;
    if (!range(source, count * 2)) return false;
    for (unsigned i = 0; i < count; ++i) if ((unsigned)(first + (int)i) < 16) {
      out[first + i] = (uint16_t)word(source + i * 2); found = true;
    }
  }
  return false;
}
static void stage_assets(unsigned stage, unsigned section) {
  if (stage == cached_stage && section == cached_section) return;
  cached_stage = stage; cached_section = section;
  memset(ready, 0, sizeof(ready)); memset(sprite_resource, 255, sizeof(sprite_resource));
  if (stage >= 13 || !range(0x376f7, 1280)) return;
  /* The section table includes boss/cutscene resource sets as well as the
   * normal route. Prefer the active set; otherwise require a unique palette. */
  size_t base = 0x32cee;
  unsigned start = word(base + stage * 2), end = word(base + stage * 2 + 2);
  if (end < start || end - start > 128) return;
  unsigned palettes[256];
  for (unsigned i = 0; i < 256; ++i) palettes[i] = ~0u;
  for (unsigned pass = 0; pass < 2; ++pass) for (unsigned s = 0; s < (end - start) / 2; ++s) {
    if ((s == section) != (pass == 1)) continue;
    size_t p = base + word(base + start + s * 2);
    for (unsigned guard = 0; guard < 64 && range(p, 6) && rom[p] != 255; ++guard, p += 6) {
      unsigned id = rom[p], pal = word(p + 3);
      if (!pass && palettes[id] != ~0u && palettes[id] != pal) { ready[id] = 3; continue; }
      if (!pass && ready[id] == 3) continue;
      palettes[id] = pal;
      MmxSpriteAsset *a = &assets[id];
      a->id = (uint8_t)id; a->tile_base = (uint8_t)(word(p + 1) >> 4);
      a->attributes = (uint8_t)(0x20 | ((word(p + 1) >> 12) & 1) | (rom[p + 5] >> 3));
      a->current = pass == 1;
      ready[id] = tiles(id, a->tiles) && palette(pal, a->colors) ? 1 : 2;
    }
  }
  /* The ROM pairs each enemy's animation set with its compressed resource.
   * Child pieces retain the parent's animation set, including Highway's
   * crusher and bee children. Ambiguous animation sets stay on live VRAM. */
  for (unsigned id = 1; id <= 0x68; ++id) {
    size_t p = 0x325e4 + (id - 1) * 2;
    unsigned animation = rom[p], resource = rom[p + 1];
    if (!animation || ready[resource] != 1) continue;
    if (sprite_resource[animation] == 255) sprite_resource[animation] = (uint8_t)resource;
    else if (sprite_resource[animation] != resource) sprite_resource[animation] = 254;
  }
}
const MmxSpriteAsset *MmxRenderAssetsSprite(unsigned stage, unsigned section, unsigned sprite) {
  stage_assets(stage, section);
  unsigned id = sprite < 256 ? sprite_resource[sprite] : 255;
  return id < 254 && ready[id] == 1 ? &assets[id] : NULL;
}
void MmxRenderAssetsMarginPalette(const uint8_t ram[0x20000], int extra,
                                uint16_t colors[128], bool changed[128]) {
  memset(changed, 0, 128 * sizeof(*changed));
  unsigned stage = ram[0x1f7a], phase = ram[0x1f0a];
  if (!extra || stage >= 13 || !range(0x32260, 32)) return;
  /* Highway's city layer scrolls at half the camera speed. An extra BG2
   * column becomes native after twice that camera travel, so its palette
   * lookahead must use the same parallax scale as the retained map. */
  int camera = (int)ram_word(ram, 0x1e4d), projected = camera + extra * (stage == 0 ? 2 : 1);
  size_t pos = 0x28000 + (word(0x282c2 + stage * 2) & 0x7fff);
  if (!range(pos, 1)) return;
  unsigned column = rom[pos++];
  int nearest = extra > 0 ? camera : projected - 1;
  for (unsigned guard = 0; guard < 512 && range(pos, 8); ++guard) {
    unsigned x = word(pos + 5), kind = rom[pos] & 15, event = rom[pos + 3];
    int line = (int)(x & 0x7fff);
    if (kind == 2 && event == 0x17 &&
        (extra > 0 ? line > camera && line <= projected && line >= nearest :
                     line > projected && line <= camera && line > nearest)) {
      phase = extra > 0 ? rom[pos + 4] & 15 : rom[pos + 4] >> 4;
      nearest = line;
      if (extra < 0) break;
    }
    pos += 7;
    if (x & 0x8000) { if (rom[pos] == column) break; column = rom[pos++]; }
  }
  if (phase == ram[0x1f0a]) return;
  size_t base = 0x32260, p = base + word(base + word(base + stage * 2) + phase * 2);
  for (unsigned guard = 0; guard < 32 && range(p, 3) && word(p) != 0xffff; ++guard, p += 3) {
    size_t source = 0x28000 + (word(p) & 0x7fff);
    unsigned first = rom[p + 2];
    if (first + 16 > 128 || !range(source, 32)) continue;
    for (unsigned i = 0; i < 16; ++i) { colors[first + i] = (uint16_t)word(source + i * 2); changed[first + i] = true; }
  }
}
