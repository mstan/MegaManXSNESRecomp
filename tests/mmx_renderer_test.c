#include "mmx_renderer.h"
#include "mmx_render_assets.h"
#include <assert.h>

static uint8_t ram[0x20000], rom_bytes[0x100000];
static Ppu ppu;
static uint32_t stock[256 * 224], output[MMX_RENDER_MAX_WIDTH * 224];
static void capture(void) {
  MmxRendererBeginFrame(ram);
  for (unsigned y = 1; y <= 224; ++y) MmxRendererCaptureLine(&ppu, y);
  assert(MmxRendererEndFrame(stock));
}
static void geometry(void) {
  assert(MmxRendererViewport(MMX_ASPECT_16_9, 0, 0).width == 342);
  assert(MmxRendererViewport(MMX_ASPECT_21_9, 0, 0).width == 448);
  assert(MmxRendererViewport(MMX_ASPECT_32_9, 0, 0).width == 682);
  assert(MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 3840, 1080).width == 682);
  assert(MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 1, 100).width == 256);
  assert(MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 10000, 1).width == MMX_RENDER_MAX_WIDTH);
  MmxDisplayViewport dst = MmxRendererDestination(MmxRendererViewport(MMX_ASPECT_21_9, 0, 0), 1920, 1080);
  assert(dst.width == 1920 && dst.height == 823 && dst.y == 128);
}
static void raster_and_hud(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram));
  ram[0xd1] = 2; ram[0xd2] = 4;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31; ppu.cgram[0] = 31 << 10;
  for (int i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xe000;
  ppu.oam[0] = 0x1010; ppu.oam[1] = 0;
  for (int y = 0; y < 8; ++y) ppu.vram[y] = 255;
  capture();
  /* Draw uses snapshots even if every live input changes after capture. */
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram));
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_32_9, 0, 0);
  assert(MmxRendererDraw(output, v, true));
  assert(output[16 * v.width + 16] == 0xff0000);
  assert(output[16 * v.width + 16 + v.extra] == 0x0000ff);
  assert(MmxRendererDraw(output, v, false));
  assert(output[16 * v.width + 16 + v.extra] == 0xff0000);
  assert(MmxRendererGetStats().custom_lines == 224);
  assert(!MmxRendererDraw(output, (MmxRenderView){2048,896,8}, true));
  MmxRendererReset(); assert(!MmxRendererDraw(output, v, true));
}
static void sprite_coordinates(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram));
  ram[0xd1] = 2; ram[0xd2] = 4;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16; ppu.cgram[129] = 31;
  for (int i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xe000;
  for (int y = 0; y < 8; ++y) ppu.vram[y] = 255;
  MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  g_mmx_custom_renderer = true;
  ram[0x18] = 0; ram[0x19] = 0x80; ram[0x1a] = 0x80;
  ram[0] = 44; ram[1] = 1; ram[2] = 40;
  MmxRendererRecordPiece(ram, 0); /* +300 must never wrap to -212. */
  ram[0] = 0x38; ram[1] = 0xff;
  MmxRendererRecordPiece(ram, 0); /* -200 must never wrap to +312. */
  ram[0] = 255; ram[1] = 0; ram[2] = 60;
  MmxRendererRecordPiece(ram, 0); /* Native x=255 clip must not cut a seam. */
  ram[0] = 54; ram[1] = 1; ram[2] = 44; ram[3] = 1;
  MmxRendererRecordPiece(ram, 0); /* Host y=300 must not wrap into row 44. */
  MmxRendererLatchSprites(); capture();
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_32_9, 0, 0);
  assert(MmxRendererDraw(output, v, false));
  assert(output[40 * v.width + v.extra + 300] == 0xff0000);
  assert(output[40 * v.width + v.extra - 200] == 0xff0000);
  assert(output[40 * v.width + v.extra - 212] == 0);
  assert(output[60 * v.width + v.extra + 255] == 0xff0000);
  assert(output[44 * v.width + v.extra + 310] == 0);
  assert(MmxRendererGetStats().margin_sprite_pixels == 184);
  g_mmx_custom_renderer = false;
}
static void put_word(unsigned a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static void rom_word(unsigned a, unsigned v) { rom_bytes[a] = (uint8_t)v; rom_bytes[a + 1] = (uint8_t)(v >> 8); }
static void rom_long(unsigned a, unsigned v) { rom_word(a, v); rom_bytes[a + 2] = (uint8_t)(v >> 16); }
static void expanded_capacity(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = 4;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16; ppu.cgram[129] = 31;
  for (int i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xe000;
  for (int i = 16; i < 128; ++i) { ppu.oam[i * 2] = 0x2800; ppu.oam[i * 2 + 1] = 0x2000; }
  for (int y = 0; y < 8; ++y) ppu.vram[y] = 255;
  rom_long(0x68003, 0x8d9000); rom_long(0x69000, 0x8d9100);
  rom_bytes[0x69100] = 200;
  for (unsigned i = 112; i < 200; ++i) rom_bytes[0x69101 + i * 4] = 50;
  ram[0xe7] = 1; put_word(0x920, 0xe68);
  ram[0xe68] = 1; ram[0xe7e] = 1; ram[0xe79] = 0x20;
  put_word(0xe70, 40);
  MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  g_mmx_custom_renderer = true;
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_16_9, 0, 0);
  for (int enabled = 0; enabled < 2; ++enabled) {
    MmxRendererReset(); g_mmx_expanded_sprites = enabled != 0;
    uint8_t before[0x20000]; memcpy(before, ram, sizeof(ram));
    MmxRendererObserveObject(ram, 0xe68);
    assert(!memcmp(before, ram, sizeof(ram)));
    /* Model the retail writer stopping after the 112 available gameplay
     * slots. The submitted list still contains the remaining 88 pieces. */
    ram[0xf] = 0x20; put_word(2, 40); ram[0x1a] = 0x8d;
    for (unsigned i = 0; i < 112; ++i) {
      put_word(0x18, 0x9100 + i * 4); MmxRendererRecordPiece(ram, 0);
    }
    MmxRendererLatchSprites(); capture();
    assert(MmxRendererDraw(output, v, false));
    assert(output[40 * v.width + v.extra + 50] == (enabled ? 0xff0000u : 0));
    assert(MmxRendererGetStats().pieces == 112);
    /* An invalidated frame must never resurrect expanded submissions. */
    MmxRendererReset(); assert(!MmxRendererDraw(output, v, false));
  }
  g_mmx_custom_renderer = g_mmx_expanded_sprites = false;
}
static void margin_palette_transition(void) {
  memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  put_word(0x1e4d, 0x7c2);
  rom_word(0x282c2, 0x9000);
  rom_bytes[0x29000] = 0x42; rom_bytes[0x29001] = 2;
  rom_bytes[0x29004] = 0x17; rom_bytes[0x29005] = 1;
  rom_word(0x29006, 0x8850); rom_bytes[0x29008] = 0x42;
  rom_word(0x32260, 0x20); rom_word(0x32280, 0x30); rom_word(0x32282, 0x40);
  rom_word(0x32290, 0xa000); rom_bytes[0x32292] = 0x70; rom_word(0x32293, 0xffff);
  rom_word(0x322a0, 0xa020); rom_bytes[0x322a2] = 0x70; rom_word(0x322a3, 0xffff);
  for (unsigned i = 0; i < 16; ++i) { rom_word(0x2a000 + i * 2, i); rom_word(0x2a020 + i * 2, i + 16); }
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  uint16_t colors[128]; bool changed[128];
  MmxRenderAssetsMarginPalette(ram, 0, colors, changed);
  for (int i = 0; i < 128; ++i) assert(!changed[i]);
  MmxRenderAssetsMarginPalette(ram, 96, colors, changed);
  for (int i = 0; i < 128; ++i) {
    assert(changed[i] == (i >= 0x70));
    if (i >= 0x70) assert(colors[i] == i - 0x70 + 16);
  }
  /* Backtracking projects the previous palette; the live phase stays put. */
  put_word(0x1e4d, 0x900); ram[0x1f0a] = 1;
  MmxRenderAssetsMarginPalette(ram, -96, colors, changed);
  for (int i = 0x70; i < 128; ++i) assert(changed[i] && colors[i] == i - 0x70);
  assert(ram[0x1f0a] == 1 && ram[0x1e4e] == 9);
}
static void resource_decode(void) {
  memset(rom_bytes, 0, sizeof(rom_bytes));
  /* Two section lists: resource 1 is available in the future section only,
   * with a legitimate tile base of zero. Decode a repeated-byte CHR stream. */
  rom_word(0x32cee, 0x20); rom_word(0x32cf0, 0x24);
  rom_word(0x32d0e, 0x30); rom_word(0x32d10, 0x40);
  rom_bytes[0x32d1e] = 255;
  rom_bytes[0x32d2e] = 1; rom_word(0x32d2f, 0); rom_word(0x32d31, 2);
  rom_bytes[0x32d33] = 0x40; rom_bytes[0x32d34] = 255;
  rom_bytes[0x325e4] = 7; rom_bytes[0x325e5] = 1;
  rom_word(0x376fc, 32); rom_long(0x376fe, 0x808000);
  for (unsigned i = 0; i < 8; i += 2) rom_bytes[i + 1] = 0x55;
  rom_word(0x371b9, 0x200); rom_bytes[0x373b7] = 2; rom_bytes[0x373b8] = 0xe0;
  rom_word(0x30135, 0x9000); rom_bytes[0x31000] = 16; rom_word(0x31001, 0x9000); rom_bytes[0x31003] = 128;
  for (unsigned i = 0; i < 16; ++i) rom_word(0x29000 + i * 2, i);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  const MmxSpriteAsset *asset = MmxRenderAssetsSprite(0, 0, 7);
  assert(asset && !asset->current && asset->tile_base == 0 && asset->attributes == 0x28);
  for (unsigned i = 0; i < 32; ++i) assert(asset->tiles[i] == 0x55);
  for (unsigned i = 0; i < 16; ++i) assert(asset->colors[i] == i);
  asset = MmxRenderAssetsSprite(0, 1, 7); assert(asset && asset->current);

  /* The rotor borrows resource $2D's palette, but uses permanent page-zero
   * CHR. Its animation is deliberately absent from the enemy asset table. */
  rom_bytes[0x32d2e] = 0x2d; rom_bytes[0x325e5] = 0x2d;
  memcpy(rom_bytes + 0x376f7 + 0x2d * 5, rom_bytes + 0x376fc, 5);
  rom_word(0x371b7 + 0x2d * 2, 0x200);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  memset(ram, 0, sizeof(ram));
  ram[0x1932] = 0x1f; ram[0x193e] = 0x36; put_word(0x1934, 0xe68); ram[0xe72] = 0x22;
  asset = MmxRenderAssetsObjectSprite(ram, 0x1928, 0x36);
  assert(asset && asset->live_tiles && asset->attributes == 0x28);
  ram[0xe72] = 0x29; assert(!MmxRenderAssetsObjectSprite(ram, 0x1928, 0x36));
  ram[0xe72] = 0x22; /* A dead parent's retained identity is sufficient. */
  memset(&ppu, 0, sizeof(ppu)); MmxRendererReset();
  ram[0xd1] = 2; ram[0xd2] = 4;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  for (int i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xe000;
  for (int y = 0; y < 8; ++y) ppu.vram[0x40 * 16 + y] = 255;
  rom_bytes[0x103] = 0x40; put_word(0x18, 0x8100); ram[0x1a] = 0x80;
  put_word(0, 40); put_word(2, 40);
  MmxRendererSetRom(rom_bytes, sizeof(rom_bytes)); g_mmx_custom_renderer = true;
  MmxRendererObserveObject(ram, 0x1928); MmxRendererRecordPiece(ram, 0);
  MmxRendererLatchSprites(); capture();
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_32_9, 0, 0);
  assert(MmxRendererDraw(output, v, false));
  assert(output[40 * v.width + v.extra + 40] == 0x080000);
  g_mmx_custom_renderer = false;
  MmxRenderAssetsSetRom(NULL, 0);
  assert(!MmxRenderAssetsSprite(0, 0, 7));
}
int main(void) { geometry(); raster_and_hud(); sprite_coordinates(); expanded_capacity(); margin_palette_transition(); resource_decode(); return 0; }
