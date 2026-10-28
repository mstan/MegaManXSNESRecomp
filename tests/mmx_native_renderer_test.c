/* Run the established scene suite against the native compositor, then compare
 * the tile jobs with its scalar decoder over randomized captured VRAM. */
#define MMX_NATIVE_RASTER 1
#define main renderer_scene_suite
#include "mmx_renderer_test.c"
#undef main
#include "../src/mmx_renderer.c"

/* These synthetic captures supply an already complete stock image. The real
 * captured-line service is covered by the framework and game-state gates. */
void ppu_drawCapturedLine(Ppu *p, int line, bool pixels) {
  (void)p; (void)line; (void)pixels;
  assert(!"unexpected deferred stock raster in a synthetic capture");
  abort();
}

static uint32_t row_rng = 0x53ea1234u;
static uint32_t row_random(void) {
  row_rng ^= row_rng << 13; row_rng ^= row_rng >> 17; row_rng ^= row_rng << 5;
  return row_rng;
}

#ifndef MMX_NATIVE_TEST_ENTRY
#define MMX_NATIVE_TEST_ENTRY main
#endif
int MMX_NATIVE_TEST_ENTRY(void) {
  renderer_scene_suite();
  MmxRendererReset(); MmxRendererSetRom(NULL, 0);
  memset(&frame_rush, 0, sizeof(frame_rush));
  memset(frame.ram, 0, sizeof(frame.ram));
  view_dx = view_dy = airport_upper_sky = 0;
  for (unsigned sample = 0; sample < 64; ++sample) {
    Raster raster = {0}; SnesRenderOamIndex entries;
    for (unsigned i = 0; i < 256; ++i) raster.oam[i] = (uint16_t)row_random();
    for (unsigned i = 0; i < 32; ++i) raster.high_oam[i] = (uint8_t)row_random();
    /* Duplicate hardware submissions must all be replaced, not just the
     * first matching slot. Include duplicates across both mask words. */
    for (unsigned slot = 17; slot < 128; slot += 13) {
      raster.oam[slot*2] = raster.oam[32]; raster.oam[slot*2+1] = raster.oam[33];
      unsigned shift = slot % 4 * 2;
      raster.high_oam[slot/4] = (uint8_t)((raster.high_oam[slot/4] & ~(1u<<shift)) |
          ((raster.high_oam[4] & 1u) << shift));
    }
    assert(SnesRenderIndexOam(&entries, raster.oam, raster.high_oam, 16, 112));
    for (unsigned query = 0; query < 128; ++query) {
      unsigned pos = raster.oam[query*2], attr = raster.oam[query*2+1];
      int x = (pos & 255) | (((raster.high_oam[query/4] >> (query%4*2)) & 1) << 8);
      if (x >= 256) x -= 512;
      uint64_t expected[2] = {0, 0};
      for (unsigned slot = 16; slot < 128; ++slot) {
        unsigned other = raster.oam[slot*2];
        int ox = (other & 255) | (((raster.high_oam[slot/4] >> (slot%4*2)) & 1) << 8);
        if (ox >= 256) ox -= 512;
        if (x == ox && (other >> 8) == (pos >> 8) && raster.oam[slot*2+1] == attr)
          expected[slot/64] |= UINT64_C(1) << (slot%64);
      }
      const SnesRenderOamBinding *found = SnesRenderFindOam(&entries, x, (pos>>8)+256, attr);
      assert((found != NULL) == ((expected[0] | expected[1]) != 0));
      if (found) assert(found->slots[0] == expected[0] && found->slots[1] == expected[1]);
    }
    assert(!SnesRenderFindOam(&entries, -257, 0, 0));
    assert(!SnesRenderFindOam(&entries, 256, 0, 0));
  }
  for (unsigned sample = 0; sample < 128; ++sample) {
    memset(&ppu, 0, sizeof(ppu));
    ppu.bgmode = (uint8_t)(1 | (row_random() & 0xf8));
    for (unsigned i = 0; i < 0x8000; ++i) ppu.vram[i] = (uint16_t)row_random();
    Raster raster = {0}; raster.vram = ppu.vram;
    int y = (int)(row_random() & 1023);
    for (unsigned layer = 0; layer < 3; ++layer) {
      ppu.bgXsc[layer] = (uint8_t)row_random();
      ppu.hScroll[layer] = (uint16_t)row_random();
      ppu.vScroll[layer] = (uint16_t)row_random();
      ppu.bgTileAdr = (uint16_t)row_random();
      uint16_t pixels[256];
      native_background_center(&ppu, &raster, layer, y, pixels);
      for (int x = 0; x < 256; ++x) {
        int private_color = -1;
        uint16_t expected = background(&ppu, &raster, layer, x, y, true, &private_color);
        if (pixels[x] != expected || private_color != -1) {
          fprintf(stderr, "native tile job differs sample=%u layer=%u x=%d\n", sample, layer, x);
          return 1;
        }
      }
    }
  }
  for (unsigned level = 0; level < 16; ++level) {
    uint8_t brightness[32]; uint16_t palette[256];
    for (unsigned c = 0; c < 32; ++c)
      brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * level / 15);
    for (unsigned c = 0; c < 256; ++c) palette[c] = (uint16_t)row_random();
    const uint32_t *rgb = native_brightness(level);
    for (unsigned sample = 0; sample < 4096; ++sample) {
      unsigned layer = row_random() % 7;
      if (layer == 3) continue; /* Mode1 has no BG4. */
      bool inside = (row_random() & 1) != 0;
      ppu.cgwsel = (uint8_t)row_random(); ppu.cgadsub = (uint8_t)row_random();
      unsigned plain = condition(ppu.cgwsel >> 6, inside) ? 0 :
          condition((ppu.cgwsel >> 4) & 3, inside) ? 127 : (~ppu.cgadsub & 127);
      if (!(plain & (1u << layer))) continue;
      uint16_t main_pixel = (uint16_t)((layer << 8) | (row_random() & 255));
      int object = (row_random() & 1) ? (int)(row_random() & 0x7fff) : -1;
      int bg[3];
      for (unsigned i = 0; i < 3; ++i)
        bg[i] = (row_random() & 1) ? (int)(row_random() & 0xffff) : -1;
      uint32_t expected = colour(&ppu, palette, brightness, main_pixel,
          (uint16_t)row_random(), inside, object, bg, false, 0x500);
      assert(native_plain_colour(palette, rgb, main_pixel, object, bg) == expected);
    }
  }
  puts("NATIVE OAM/TILE/COLOR JOBS AND RENDERER SCENES PASSED");
  return 0;
}
