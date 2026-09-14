#include "mmx_render_assets.h"
#include <string.h>

/* The same ROM compression/transfer format used by mmx_wide_preview.c,
 * applied to live animation pieces, without allocating guest VRAM/CGRAM. */
static const uint8_t *rom;
static size_t rom_size;
static MmxSpriteAsset assets[256];
static uint8_t ready[256], sprite_resource[256];
static unsigned cached_stage = ~0u, cached_section = ~0u;
static unsigned bg_stage = ~0u;
static uint8_t bg_phase[2][8192], bg_chr[16][65536];
static bool bg_chr_valid[16][2048], bg_chr_ready[16], bg_palette_ready[16];
static MmxBackgroundPalette bg_palette[16];
static bool range(size_t a, size_t n) { return a <= rom_size && n <= rom_size - a; }
static unsigned word(size_t a) { return range(a, 2) ? rom[a] | (rom[a + 1] << 8) : 0; }
static size_t lorom(unsigned a) { return ((a >> 16) & 127) * 0x8000u + (a & 0x7fff); }
static unsigned ram_word(const uint8_t *r, unsigned a) { return r[a] | (r[a + 1] << 8); }

void MmxRenderAssetsSetRom(const uint8_t *bytes, size_t size) {
  if (rom == bytes && rom_size == size) return;
  rom = bytes; rom_size = size;
  cached_stage = cached_section = ~0u;
  bg_stage = ~0u;
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
      a->live_tiles = false;
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
const MmxSpriteAsset *MmxRenderAssetsObjectSprite(const uint8_t ram[0x20000],
                                                unsigned object, unsigned animation) {
  if (!ram) return NULL;
  /* The usable Ride Armor has a dedicated slot/animation; the enemy table
   * maps its pilot ($4F), not the armor's own $4A animation, to resource $49.
   * Early visibility must not borrow CHR/palettes from the current cave set. */
  if (object == 0xe18 && animation == 0x4a) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    return ready[0x49] == 1 ? &assets[0x49] : NULL;
  }
  /* 82:F486's rotor effect uses animation $36, but binds resource $2D
   * directly through $7F832D. It is absent from the enemy animation table.
   * Verify its Bee Blader parent; unrelated users of animation $36 must
   * retain their own art. The parent need not remain live after detachment. */
  if (ram[0x1f7a] == 0 && animation == 0x36 && object >= 0x1928 && object <= 0x1be8 &&
      (object & 31) == 8 && ram[object + 10] == 0x1f) {
    unsigned parent = ram_word(ram, object + 12);
    if (parent >= 0xe68 && parent <= 0x1228 && (parent & 63) == 0x28 && ram[parent + 10] == 0x22) {
      stage_assets(ram[0x1f7a], ram[0x1f08]);
      if (ready[0x2d] != 1) return NULL;
      /* F48C clears the tile base and F492 clears the OBJ page bit. The
       * rotor uses permanent page-zero CHR, borrowing only the bee palette.
       * Substituting the body's CHR would turn its blades into body tiles. */
      static MmxSpriteAsset rotor;
      memcpy(rotor.colors, assets[0x2d].colors, sizeof(rotor.colors));
      rotor.id = 0x2d; rotor.tile_base = 0;
      rotor.attributes = assets[0x2d].attributes & 0xfe;
      rotor.current = assets[0x2d].current; rotor.live_tiles = true;
      return &rotor;
    }
  }
  return MmxRenderAssetsSprite(ram[0x1f7a], ram[0x1f08], animation);
}
bool MmxRenderAssetsRideArmorPalettePending(const uint8_t ram[0x20000], const uint16_t colors[16]) {
  if (!ram || !colors || ram[0x1f7a] != 8) return false;
  stage_assets(ram[0x1f7a], ram[0x1f08]);
  /* Section 4 replaces resource $4A's cave palette in OBJ slot 5 with
   * armor resource $49. Binding metadata advances before the palette DMA.
   * Recognize the exact previous palette, leaving flashes/other colors alone. */
  return ready[0x49] == 1 && ready[0x4a] == 1 && assets[0x49].current &&
      memcmp(colors + 1, assets[0x4a].colors + 1, 15 * sizeof(*colors)) == 0 &&
      memcmp(colors + 1, assets[0x49].colors + 1, 15 * sizeof(*colors)) != 0;
}
/* Highway and Chill Penguin's kind-2 $17 records select palettes at X
 * boundaries. Chill's CHR also switches vertically, so only Highway gets
 * the horizontal CHR projection below. */
static bool prepare_background(const uint8_t *ram) {
  unsigned stage = ram[0x1f7a];
  if ((stage != 0 && stage != 8) || !range(0x32280, 2)) return false;
  if (bg_stage == stage) return true;
  bg_stage = stage;
  memset(bg_phase, 0, sizeof(bg_phase));
  memset(bg_chr_ready, 0, sizeof(bg_chr_ready));
  memset(bg_palette_ready, 0, sizeof(bg_palette_ready));
  size_t pos = 0x28000 + (word(0x282c2 + stage * 2) & 0x7fff);
  if (!range(pos, 1)) return false;
  unsigned column = rom[pos++];
  bool first[2] = {true, true};
  for (unsigned guard = 0; guard < 512 && range(pos, 8); ++guard) {
    unsigned x = word(pos + 5), event = rom[pos + 3];
    if ((rom[pos] & 15) == 2 && (event == 0x16 || event == 0x17)) {
      unsigned line = x & 0x7fff;
      /* The high nibble names the phase on the left of the first boundary.
       * Chill's cave starts in palette phase 1, not phase 0. */
      if (first[event - 0x16]) {
        memset(bg_phase[event - 0x16], rom[pos + 4] >> 4, 8192);
        first[event - 0x16] = false;
      }
      if (line < 8192) memset(bg_phase[event - 0x16] + line, rom[pos + 4] & 15, 8192 - line);
    }
    pos += 7;
    if (x & 0x8000) { if (rom[pos] == column) break; column = rom[pos++]; }
  }
  return true;
}
static size_t background_list(size_t base, unsigned phase) {
  unsigned start = word(base + bg_stage * 2), end = word(base + bg_stage * 2 + 2);
  if (end < start || phase >= (end - start) / 2) return rom_size;
  return base + word(base + start + phase * 2);
}
const uint8_t *MmxRenderAssetsBackgroundTile(const uint8_t ram[0x20000],
                                            int world_x, unsigned vram_word) {
  if (!ram || ram[0x1f7a] != 0 || world_x < 0 || world_x >= 8192 || vram_word >= 0x8000 || !prepare_background(ram)) return NULL;
  unsigned phase = bg_phase[0][world_x];
  /* RAM records the requested phase before DMA completes. Use private
   * resources even when it matches, so margin art cannot briefly regress. */
  if (!bg_chr_ready[phase]) {
    bg_chr_ready[phase] = true;
    memset(bg_chr_valid[phase], 0, sizeof(bg_chr_valid[phase]));
    size_t p = background_list(0x321d5, phase);
    /* B436's nine-byte DMA records: byte count, VRAM word destination,
     * ROM long source, palette descriptor. BG data is uncompressed. */
    for (unsigned guard = 0; guard < 32 && range(p, 9) && word(p); ++guard, p += 9) {
      unsigned count = word(p), dest = word(p + 2) * 2;
      size_t source = lorom(word(p + 4) | (rom[p + 6] << 16));
      if ((dest & 31) || (count & 31) || dest + count > 65536 || !range(source, count)) continue;
      memcpy(bg_chr[phase] + dest, rom + source, count);
      memset(bg_chr_valid[phase] + dest / 32, 1, count / 32);
    }
  }
  return bg_chr_valid[phase][vram_word / 16] ? bg_chr[phase] + vram_word * 2 : NULL;
}
static const MmxBackgroundPalette *background_palette(unsigned phase) {
  if (!bg_palette_ready[phase]) {
    bg_palette_ready[phase] = true;
    memset(&bg_palette[phase], 0, sizeof(bg_palette[phase]));
    size_t p = background_list(0x32260, phase);
    for (unsigned guard = 0; guard < 32 && range(p, 3) && word(p) != 0xffff; ++guard, p += 3) {
      size_t source = 0x28000 + (word(p) & 0x7fff);
      unsigned first = rom[p + 2];
      /* Chill's cave/outdoor phases leave the $20 group unchanged. */
      if (bg_stage == 8 && first == 0x20) continue;
      if (first + 16 > 128 || !range(source, 32)) continue;
      for (unsigned i = 0; i < 16; ++i) {
        bg_palette[phase].colors[first + i] = (uint16_t)word(source + i * 2);
        bg_palette[phase].valid[first + i] = true;
      }
    }
  }
  return &bg_palette[phase];
}
const MmxBackgroundPalette *MmxRenderAssetsBackgroundPalette(const uint8_t ram[0x20000],
                                                             int world_x) {
  if (!ram || world_x < 0 || world_x >= 8192 || !prepare_background(ram)) return NULL;
  return background_palette(bg_phase[1][world_x]);
}
uint16_t MmxRenderAssetsFadeColor(uint16_t color, unsigned amount) {
  unsigned result = 0;
  for (unsigned shift = 0; shift < 15; shift += 5) {
    unsigned c = ((color >> shift) & 31) + amount;
    result |= (c > 31 ? 31 : c) << shift;
  }
  return (uint16_t)result;
}
unsigned MmxRenderAssetsDeathPaletteFade(const uint8_t ram[0x20000], const uint16_t colors[256]) {
  if (!ram || !colors || ram[0xd3] != 6 || !prepare_background(ram)) return 0;
  const MmxBackgroundPalette *base = background_palette(ram[0x1f0a] & 15);
  /* Death adds the same saturating RGB amount to the live palettes. Infer
   * only an exact transform of every owned opaque color, from captured CGRAM
   * (RAM can be a DMA ahead), then apply it to private margin resources too. */
  for (unsigned fade = 0; fade <= 31; ++fade) {
    unsigned checked = 0; bool match = true;
    for (unsigned i = 0; i < 128 && match; ++i) if ((i & 15) && base->valid[i]) {
      ++checked;
      match = MmxRenderAssetsFadeColor(base->colors[i], fade) == colors[i];
    }
    if (checked && match) return fade;
  }
  return 0;
}
