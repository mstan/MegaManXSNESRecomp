#include "mmx_render_assets.h"
#include "mmx_wide_policy.h"
#include <string.h>

/* The same ROM compression/transfer format used by mmx_wide_preview.c,
 * applied to live animation pieces, without allocating guest VRAM/CGRAM. */
static const uint8_t *rom;
static size_t rom_size;
static MmxSpriteAsset assets[256];
static MmxSpriteAsset captive_zero;
static unsigned captive_zero_ready;
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
  captive_zero_ready = 0;
}
static size_t decode_resource(unsigned id, uint8_t decoded[65536]) {
  size_t info = 0x376f7 + id * 5;
  if (!range(info, 5)) return 0;
  size_t count = word(info), pos = lorom(word(info + 2) | (rom[info + 4] << 16));
  if (!count || count > 65536) return 0;
  for (size_t n = 0; n < count;) {
    if (!range(pos, 2)) return false;
    unsigned control = rom[pos++], repeat = rom[pos++];
    for (unsigned bit = 128; bit && n < count; bit >>= 1) {
      if ((control & bit) && !range(pos, 1)) return false;
      decoded[n++] = (uint8_t)((control & bit) ? rom[pos++] : repeat);
    }
  }
  return count;
}
static bool tiles(unsigned id, uint8_t out[8192]) {
  uint8_t decoded[65536];
  size_t count = decode_resource(id, decoded);
  if (!count) return false;
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
const MmxSpriteAsset *MmxRenderAssetsCaptiveZero(void) {
  if (captive_zero_ready) return captive_zero_ready == 1 ? &captive_zero : NULL;
  captive_zero_ready = 2;
  uint8_t decoded[65536];
  size_t count = decode_resource(0x51, decoded);
  if (!count || !range(0x2a96e + 0x20 * 2, 2)) return NULL;
  /* $88:D1F5 / $84:8FCA upload Zero's CHR on pose changes. His waiting pose
   * is not in VRAM before initialization. Reproduce frame $20's ROM DMA list
   * privately; frame $21 only adds an eye tile and has no further transfer. */
  size_t list = 0x2a96e + word(0x2a96e + 0x20 * 2);
  memset(&captive_zero, 0, sizeof(captive_zero));
  for (unsigned n = 0; n < 64; ++n, list += 5) {
    if (!range(list, 5) || !rom[list] || rom[list + 3] != 0x7f) return NULL;
    unsigned length = rom[list] * 16, source = word(list + 1);
    int dest = (((int)rom[list + 4] & 127) * 256 - 0x6000) * 2;
    if (source + length > count || dest < 0 || dest + length > 8192) return NULL;
    memcpy(captive_zero.tiles + dest, decoded + source, length);
    if (rom[list + 4] & 128) {
      captive_zero.id = 0x51; captive_zero.live_colors = true;
      captive_zero_ready = 1;
      return &captive_zero;
    }
  }
  return NULL;
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
      a->live_colors = false;
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
  /* Sub Tanks bind resource $8C directly at $81:E4D3, outside the enemy
   * animation table. Cold loads must not depend on previously resident CHR. */
  if (object >= 0x1628 && object < 0x1928 && (object - 0x1628) % 0x30 == 0 &&
      ram[object + 10] == 5 && animation == 0x96) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    if (ready[0x8c] != 1) return NULL;
    static MmxSpriteAsset tank;
    tank = assets[0x8c]; tank.current = false; tank.live_colors = true;
    /* E4E9 explicitly borrows permanent OBJ palette 2. Resource $8C's
     * section palette belongs to its other art and would turn the tank green. */
    return &tank;
  }
  /* Spark's freeze state ($88:A25E) deliberately selects palette $0A,
   * and thawing returns it to $08. Its ice chips share animation $91 and
   * the live ice palette. Native-timed bosses have current art; replacing
   * these bindings with resource $8A's default colors erases the ice coat. */
  if (animation == 0x91 &&
      ((object >= 0xe68 && object <= 0x1228 && (object & 63) == 0x28 && ram[object + 10] == 0x31) ||
       (object >= 0x1928 && object < 0x1d08 && (object & 31) == 8 && ram[object + 10] == 6))) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    if (ready[0x8a] == 1 && assets[0x8a].current) return NULL;
  }
  /* $81:E99D binds collectible $0B directly to resource $36; animation
   * $38 has no entry in the enemy resource table. Its old palette slot can
   * be reused while the tank is still visible in the extended view. */
  if (object >= 0x1628 && object < 0x1928 && (object - 0x1628) % 0x30 == 0 &&
      ram[object + 10] == 0x0b && animation == 0x38) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    return ready[0x36] == 1 ? &assets[0x36] : NULL;
  }
  /* Penguin's balls and breath share the body's CHR ($61) but deliberately
   * borrow the ice-statue palette ($62), $81:BBEE and $81:BCAA..BCBA. Animation identity alone
   * must not turn that valid mixed binding back into the boss palette. */
  bool penguin_attack = object >= 0x1428 && object < 0x1628 &&
      (object & 63) == 0x28 && (ram[object + 10] == 6 || ram[object + 10] == 0x1a);
  /* Ice fragments inherit that same mixed binding. The shared
   * actors also run in the fortress rematch, not just Penguin's own stage. */
  bool penguin_ice = object >= 0x1928 && object < 0x1d08 &&
      (object & 31) == 8 && ram[object + 10] == 8;
  if ((penguin_attack || penguin_ice) &&
      (animation == 0x67 || animation == 0x68)) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    if (ready[0x62] != 1) return NULL;
    if (animation == 0x68) return &assets[0x62];
    if (ready[0x61] != 1) return NULL;
    static MmxSpriteAsset breath;
    breath = assets[0x61];
    memcpy(breath.colors, assets[0x62].colors, sizeof(breath.colors));
    breath.attributes = (assets[0x62].attributes & 0xfe) | (assets[0x61].attributes & 1);
    breath.current = assets[0x61].current && assets[0x62].current;
    return &breath;
  }
  /* Enemy $0D's $88:8F42 setup chooses tile offsets 0/8, and its damage
   * states change palette bits explicitly. Its active resource uses live
   * page-zero art; treating these authored variants as stale erases it. */
  if (object >= 0xe68 && object <= 0x1228 && (object & 63) == 0x28 &&
      ram[object + 10] == 0x0d && animation == 1) {
    stage_assets(ram[0x1f7a], ram[0x1f08]);
    if (ready[7] == 1 && assets[7].current) return NULL;
  }
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
  const MmxSpriteAsset *asset = MmxRenderAssetsSprite(ram[0x1f7a], ram[0x1f08], animation);
  /* Dr. Light ($88:A7BB) deliberately overrides the section's tile base
   * with $20 for both the hologram and dialogue portrait. Their current
   * VRAM is authoritative; treating that offset as stale replaces the
   * doctor with blank resource tiles. Preserve the shared capsule actor
   * across stages, including its live colors and guest-controlled flicker. */
  if (asset && asset->current && object >= 0xe68 && object < 0x1228 &&
      (object & 63) == 0x28 && ram[object + 10] == 0x5c) return NULL;
  /* Rangda Bangda's eyes, nose and moving walls share animation $9B.
   * Their current bindings deliberately select separate live palettes;
   * $88:B45D explicitly gives the wall ends palette 7 instead of the
   * eye resource's default palette 4. Preserve their animation and flashes. */
  if (asset && asset->current && animation == 0x9b &&
      object >= 0xe68 && object < 0x1228 && (object & 63) == 0x28 &&
      ram[object + 10] >= 0x5e && ram[object + 10] <= 0x60) return NULL;
  /* Native-timed bosses own current resources. Their palette changes are
   * intentional damage/weapon effects, including Armadillo's hit flash. */
  if (asset && asset->current && object >= 0xe68 && object < 0x1228 &&
      (object & 63) == 0x28 && MmxWidePolicy_IsBossEncounter(ram[object + 10])) return NULL;
  return asset;
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
/* Kind-2 $17 records select background resources. Highway/Launch project
 * CHR horizontally; they, Sting, Mammoth and Chill also project palettes. Armadillo's shaft
 * continuation requests its destination palette explicitly. */
static bool prepare_background(const uint8_t *ram) {
  unsigned stage = ram[0x1f7a];
  if ((stage != 0 && stage != 1 && stage != 2 && stage != 3 && stage != 4 && stage != 8) || !range(0x32280, 2)) return false;
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
    /* Mammoth's shafts join rooms ordered left-to-right, even though their
     * palette controllers trigger on Y. Each nibble names one side: continue
     * into the phase opposite the room preceding the shaft. The guest still
     * owns when its live palette changes; this resolves only distant art. */
    if (stage == 4 && (rom[pos] & 15) == 2 && event == 0x1a) {
      unsigned line = x & 0x7fff, above = rom[pos + 4] >> 4, below = rom[pos + 4] & 15;
      if (line > 0 && line < 8192) {
        unsigned preceding = bg_phase[1][line - 1];
        if (preceding == above || preceding == below)
          memset(bg_phase[1] + line, preceding == above ? below : above, 8192 - line);
      }
    }
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
  if (!ram || (ram[0x1f7a] != 0 && ram[0x1f7a] != 1) || world_x < 0 || world_x >= 8192 || vram_word >= 0x8000 || !prepare_background(ram)) return NULL;
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
    /* Launch's phase 2 updates only $10; its distant ruins still use the
     * $50 group from phase 1 after the boss-room palette replaces CGRAM. */
    if (bg_stage == 1 && phase) bg_palette[phase] = *background_palette(phase - 1);
    /* Phase 3 reasserts the original ocean $70 group, which earlier phase
     * lists leave resident. Seed that group before the boss can reuse it. */
    if (bg_stage == 1 && !phase) {
      size_t seed = background_list(0x32260, 3);
      for (unsigned guard = 0; guard < 32 && range(seed, 3) && word(seed) != 0xffff; ++guard, seed += 3) {
        size_t source = 0x28000 + (word(seed) & 0x7fff);
        if (rom[seed + 2] != 0x70 || !range(source, 32)) continue;
        for (unsigned i = 0; i < 16; ++i) {
          bg_palette[phase].colors[0x70 + i] = (uint16_t)word(source + i * 2);
          bg_palette[phase].valid[0x70 + i] = true;
        }
      }
    }
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
  if (!ram || (ram[0x1f7a] != 0 && ram[0x1f7a] != 1 && ram[0x1f7a] != 2 && ram[0x1f7a] != 4 && ram[0x1f7a] != 8) || world_x < 0 || world_x >= 8192 || !prepare_background(ram)) return NULL;
  unsigned phase = bg_phase[1][world_x];
  /* $80:B508 adds five palette lists after Chill Penguin freezes Mammoth's
   * factory. The saved requested phase does not include that offset. */
  if (bg_stage == 4 && (ram[0x1f96] & 0x40)) phase += 5;
  return phase < 16 ? background_palette(phase) : NULL;
}
const MmxBackgroundPalette *MmxRenderAssetsBackgroundPalettePhase(const uint8_t ram[0x20000], unsigned phase) {
  return ram && phase < 16 && prepare_background(ram) ? background_palette(phase) : NULL;
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
