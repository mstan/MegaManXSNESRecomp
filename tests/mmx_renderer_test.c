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
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xd3] = 4;
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
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xd3] = 4;
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
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xd3] = 4;
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
static void background_resources(void) {
  memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  rom_word(0x282c2, 0x9000);
  rom_bytes[0x29000] = 0x42; rom_bytes[0x29001] = 2;
  rom_bytes[0x29004] = 0x17; rom_bytes[0x29005] = 1;
  rom_word(0x29006, 0x0850);
  rom_bytes[0x29008] = 2; rom_bytes[0x2900b] = 0x16; rom_bytes[0x2900c] = 1;
  rom_word(0x2900d, 0x8850); rom_bytes[0x2900f] = 0x42;
  rom_word(0x32260, 0x20); rom_word(0x32262, 0x24);
  rom_word(0x32280, 0x30); rom_word(0x32282, 0x40);
  rom_word(0x32290, 0xa000); rom_bytes[0x32292] = 0x70; rom_word(0x32293, 0xffff);
  rom_word(0x322a0, 0xa020); rom_bytes[0x322a2] = 0x70; rom_word(0x322a3, 0xffff);
  for (unsigned i = 0; i < 16; ++i) { rom_word(0x2a000 + i * 2, i); rom_word(0x2a020 + i * 2, i + 16); }
  rom_word(0x321d5, 0x20); rom_word(0x321d7, 0x24);
  rom_word(0x321f5, 0x30); rom_word(0x321f7, 0x40);
  rom_word(0x32205, 32); rom_word(0x32207, 0x3500); rom_long(0x32209, 0x808100);
  rom_word(0x32215, 32); rom_word(0x32217, 0x3500); rom_long(0x32219, 0x808120);
  memset(rom_bytes + 0x100, 0x55, 32); memset(rom_bytes + 0x120, 0xaa, 32);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  assert(MmxRenderAssetsBackgroundPalette(ram, 0x84f)->colors[0x71] == 1);
  const MmxBackgroundPalette *pal = MmxRenderAssetsBackgroundPalette(ram, 0x850);
  for (int i = 0; i < 128; ++i) {
    assert(pal->valid[i] == (i >= 0x70));
    if (i >= 0x70) assert(pal->colors[i] == i - 0x70 + 16);
  }
  const uint8_t *tile = MmxRenderAssetsBackgroundTile(ram, 0x850, 0x3500);
  assert(tile && tile[0] == 0xaa && tile[31] == 0xaa);
  assert(!MmxRenderAssetsBackgroundTile(ram, 0x850, 0x3510));
  ram[0x1f09] = ram[0x1f0a] = 1;
  pal = MmxRenderAssetsBackgroundPalette(ram, 0x84f);
  assert(pal && pal->colors[0x71] == 1);
  tile = MmxRenderAssetsBackgroundTile(ram, 0x84f, 0x3500);
  assert(tile && tile[0] == 0x55);
  /* Selecting the new phase in RAM precedes its DMA; margin resources
   * remain stable through that transition instead of borrowing stale VRAM. */
  assert(MmxRenderAssetsBackgroundPalette(ram, 0x850)->colors[0x71] == 17);
  assert(MmxRenderAssetsBackgroundTile(ram, 0x850, 0x3500)[0] == 0xaa);
  /* Camera travel cannot recolor the same authored column. */
  put_word(0x1e4d, 0x900);
  assert(MmxRenderAssetsBackgroundPalette(ram, 0x84f)->colors[0x71] == 1);
  uint16_t faded[256] = {0};
  for (unsigned i = 0; i < 16; ++i) {
    unsigned red = i + 26;
    faded[0x70 + i] = (uint16_t)((red > 31 ? 31 : red) | (10 << 5) | (10 << 10));
  }
  ram[0xd3] = 4; assert(MmxRenderAssetsDeathPaletteFade(ram, faded) == 0);
  ram[0xd3] = 6; assert(MmxRenderAssetsDeathPaletteFade(ram, faded) == 10);
  assert(MmxRenderAssetsFadeColor(0, 10) == ((10 << 10) | (10 << 5) | 10));
  assert(MmxRenderAssetsFadeColor(0x1234, 31) == 0x7fff);
  faded[0x71] = 0; assert(MmxRenderAssetsDeathPaletteFade(ram, faded) == 0);
  ram[0x1f7a] = 7;
  assert(!MmxRenderAssetsBackgroundPalette(ram, 0x84f));
  /* Chill's cave palette is phase 1; its first X boundary changes to 2.
   * No X projection is allowed for its vertically switched CHR. */
  rom_word(0x282c2 + 16, 0x9000); rom_bytes[0x29005] = 0x12;
  rom_word(0x32260 + 16, 0x20); rom_word(0x32260 + 18, 0x26);
  rom_word(0x32284, 0x50); rom_word(0x322b0, 0xa000); rom_bytes[0x322b2] = 0x70; rom_word(0x322b3, 0xffff);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0x1f7a] = 8;
  assert(MmxRenderAssetsBackgroundPalette(ram, 0x84f)->colors[0x71] == 17);
  assert(MmxRenderAssetsBackgroundPalette(ram, 0x850)->colors[0x71] == 1);
  assert(!MmxRenderAssetsBackgroundTile(ram, 0x850, 0x3500));
}
static void dialogue_and_password(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0);
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xd3] = 4;
  ppu.inidisp = 15; ppu.bgmode = 9; ppu.screenEnabled[0] = 4;
  ppu.bgXsc[2] = 4; ppu.cgram[1] = 31;
  for (int i = 0; i < 8; ++i) ppu.vram[i] = 255;
  capture();
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_32_9, 0, 0);
  assert(MmxRendererDraw(output, v, true));
  assert(output[40 * v.width + v.extra + 40] == 0xff0000);
  assert(output[40 * v.width + 40] == 0);
  ram[0xd3] = 0x0a;
  for (unsigned i = 0; i < 256 * 224; ++i) stock[i] = 0x123456;
  capture(); assert(MmxRendererDraw(output, v, true));
  assert(MmxRendererGetStats().fallback_lines == 224);
  for (int y = 0; y < 224; ++y) for (int x = 0; x < v.width; ++x)
    assert(output[y * v.width + x] == (x >= v.extra && x < v.extra + 256 ? 0x123456u : 0));
  memset(stock, 0, sizeof(stock));
}
static void highway_arena_sky(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4;
  put_word(0x1e8d, 0xa78); put_word(0x1e90, 0x16b);
  put_word(0xb98, 0x8000); ram[0xb9a] = 0x80;
  for (int y = 0; y < 4; ++y) for (int x = 10; x < 16; ++x) ram[0xec00 + y * 32 + x] = 1;
  for (int i = 0; i < 256; ++i) put_word(0xa800 + i * 2, 1);
  for (int q = 0; q < 4; ++q) rom_word(8 + q * 2, 1);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
  ppu.bgXsc[1] = 8; ppu.hScroll[1] = 0x278; ppu.vScroll[1] = 0x16a; ppu.cgram[1] = 31;
  for (int i = 0; i < 1024; ++i) ppu.vram[0x800 + i] = 1;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = 255;
  capture(); MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  assert(MmxRendererDraw(output, v, false));
  for (int x = 0; x < v.width; ++x) assert(output[80 * v.width + x] == 0xff0000);
}
static void distant_doors(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 8;
  put_word(0xb95, 0x8000); ram[0xb97] = 0x80;
  const unsigned tiles[3][4] = {{0x6df,0x46df,0x6ef,0x46ef},
      {0x6ff,0x46ff,0x86ff,0xc6ff}, {0x86ef,0xc6ef,0x86df,0xc6df}};
  /* Two back-to-back door columns at world 304/320, outside the native view. */
  ram[0xe801] = 1;
  for (int y = 0; y < 3; ++y) {
    for (int x = 3; x <= 4; ++x) put_word(0x2200 + ((y + 4) * 16 + x) * 2, y + 1);
    for (int q = 0; q < 4; ++q) {
      rom_word((y + 1) * 8 + q * 2, tiles[y][q]);
      for (int row = 0; row < 8; ++row) ppu.vram[(tiles[y][q] & 1023) * 16 + row] = 255;
    }
  }
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1;
  ppu.bgXsc[0] = 0x50; ppu.cgram[17] = 31;
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  capture(); assert(MmxRendererDraw(output, v, false));
  for (int x = 304; x < 336; ++x) assert(output[80 * v.width + v.extra + x] == (x < 320 ? 0xff0000u : 0));
  put_word(0x1e4d, 500); ppu.hScroll[0] = 500;
  capture(); assert(MmxRendererDraw(output, v, false));
  for (int x = 304; x < 336; ++x) assert(output[80 * v.width + v.extra + x - 500] == (x >= 320 ? 0xff0000u : 0));
}
static void storm_background_prefill(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 5;
  put_word(0x1e50, 0x600); ram[0x1e89] = 0x0e;
  put_word(0xb98, 0x8000); ram[0xb9a] = 0x80;
  ram[0xec01] = 1;
  for (int i = 0; i < 256; ++i) put_word(0xa800 + i * 2, 1);
  for (int q = 0; q < 4; ++q) rom_word(8 + q * 2, 1);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
  ppu.bgXsc[1] = 8; ppu.cgram[1] = 31;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = 255;
  /* The native VRAM tilemap remains blank, while the retained next screen
   * contains the complete mountain/road map during Storm's arrival. */
  capture(); MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  assert(MmxRendererDraw(output, v, false));
  assert(output[80 * v.width + v.extra + 128] == 0);
  assert(output[80 * v.width + v.extra + 320] == 0xff0000);
  assert(output[80 * v.width + v.extra - 320] == 0xff0000);
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

  /* Heart Tanks bind resource $36 without an enemy animation-table entry.
   * Keep that identity both before and after its section's VRAM allocation. */
  rom_bytes[0x32d2e] = 0x36;
  memcpy(rom_bytes + 0x376f7 + 0x36 * 5, rom_bytes + 0x376fc, 5);
  rom_word(0x371b7 + 0x36 * 2, 0x200);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  memset(ram, 0, sizeof(ram)); ram[0x1632] = ram[0x1902] = 0x0b;
  assert(!MmxRenderAssetsSprite(0, 0, 0x38));
  asset = MmxRenderAssetsObjectSprite(ram, 0x1628, 0x38);
  assert(asset && asset->id == 0x36 && !asset->current && asset->colors[5] == 5);
  assert(MmxRenderAssetsObjectSprite(ram, 0x18f8, 0x38) == asset);
  ram[0x1f08] = 1;
  asset = MmxRenderAssetsObjectSprite(ram, 0x1628, 0x38);
  assert(asset && asset->current);
  ram[0x1632] = 7; assert(!MmxRenderAssetsObjectSprite(ram, 0x1628, 0x38));
  assert(!MmxRenderAssetsObjectSprite(ram, 0x18f8, 0x37));

  /* Spark and his ice chips deliberately switch away from the resource's
   * default palette. Retain live colors when their section is resident. */
  rom_bytes[0x32d2e] = 0x8a; rom_bytes[0x325e4] = 0x91; rom_bytes[0x325e5] = 0x8a;
  memcpy(rom_bytes + 0x376f7 + 0x8a * 5, rom_bytes + 0x376fc, 5);
  rom_word(0x371b7 + 0x8a * 2, 0x200);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  memset(ram, 0, sizeof(ram)); ram[0xe72] = 0x31; ram[0x1932] = 6;
  asset = MmxRenderAssetsObjectSprite(ram, 0xe68, 0x91);
  assert(asset && !asset->current); /* Unloaded art still gets private repair. */
  ram[0x1f08] = 1;
  assert(!MmxRenderAssetsObjectSprite(ram, 0xe68, 0x91));
  assert(!MmxRenderAssetsObjectSprite(ram, 0x1928, 0x91));
  ram[0x1932] = 7; assert(MmxRenderAssetsObjectSprite(ram, 0x1928, 0x91));
  rom_bytes[0x325e4] = 7;

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
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xd3] = 4;
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
  /* Dedicated usable armor is not the pilot animation in the enemy table. */
  rom_bytes[0x32d2e] = 0x49;
  memcpy(rom_bytes + 0x376f7 + 0x49 * 5, rom_bytes + 0x376fc, 5);
  rom_word(0x371b7 + 0x49 * 2, 0x200);
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  asset = MmxRenderAssetsObjectSprite(ram, 0xe18, 0x4a);
  assert(asset && asset->id == 0x49 && !asset->current && !asset->live_tiles);
  assert(!MmxRenderAssetsObjectSprite(ram, 0xe68, 0x4a));
  /* The cave palette can survive after the section/armor bind advances.
   * Repair only that known stale palette, preserving arbitrary live flashes. */
  rom_bytes[0x32d1e] = 0x4a; rom_word(0x32d1f, 0); rom_word(0x32d21, 4);
  rom_bytes[0x32d23] = 0x40; rom_bytes[0x32d24] = 255;
  memcpy(rom_bytes + 0x376f7 + 0x4a * 5, rom_bytes + 0x376fc, 5);
  rom_word(0x371b7 + 0x4a * 2, 0x200);
  rom_word(0x30137, 0x9100); rom_bytes[0x31100] = 16; rom_word(0x31101, 0x9100); rom_bytes[0x31103] = 128;
  uint16_t old_colors[16];
  for (unsigned i = 0; i < 16; ++i) { old_colors[i] = (uint16_t)(i + 16); rom_word(0x29100 + i * 2, old_colors[i]); }
  rom_word(0x32cee + 16, 0x20); rom_word(0x32cee + 18, 0x24);
  ram[0x1f7a] = 8; ram[0x1f08] = 1;
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  assert(MmxRenderAssetsRideArmorPalettePending(ram, old_colors));
  old_colors[1] = 0x7fff; assert(!MmxRenderAssetsRideArmorPalettePending(ram, old_colors));
  /* Penguin's projectile combines body CHR with the distinct ice palette.
   * The body and unrelated effect users must keep the original mapping. */
  const unsigned ids[] = {0x61, 0x62, 7};
  for (unsigned i = 0; i < 3; ++i) {
    unsigned p = 0x32d1e + i * 6;
    rom_bytes[p] = (uint8_t)ids[i]; rom_word(p + 1, i == 1 ? 0x400 : 0x1000);
    rom_word(p + 3, i == 1 ? 4 : 2); rom_bytes[p + 5] = i == 1 ? 0x50 : 0x40;
    memcpy(rom_bytes + 0x376f7 + ids[i] * 5, rom_bytes + 0x376fc, 5);
    rom_word(0x371b7 + ids[i] * 2, 0x200);
  }
  rom_bytes[0x32d30] = 255; rom_word(0x32cee + 18, 0x22);
  rom_bytes[0x325e6] = 0x67; rom_bytes[0x325e7] = 0x61;
  rom_bytes[0x325e4 + 12 * 2] = 1; rom_bytes[0x325e5 + 12 * 2] = 7;
  ram[0x1f08] = 0; ram[0x1472] = 0x1a;
  MmxRenderAssetsSetRom(NULL, 0); MmxRenderAssetsSetRom(rom_bytes, sizeof(rom_bytes));
  asset = MmxRenderAssetsObjectSprite(ram, 0x1468, 0x67);
  assert(asset && asset->id == 0x61 && asset->current && asset->attributes == 0x2b && asset->colors[1] == 17);
  assert(asset->tiles[0] == 0x55);
  assert(MmxRenderAssetsObjectSprite(ram, 0x1468, 0x68)->id == 0x62);
  assert(MmxRenderAssetsObjectSprite(ram, 0xe68, 0x67)->colors[1] == 1);
  ram[0x1472] = 0x12;
  assert(MmxRenderAssetsObjectSprite(ram, 0x1468, 0x67)->colors[1] == 1);
  ram[0xe72] = 0x0d; ram[0xe80] = 8;
  assert(MmxRenderAssetsSprite(8, 0, 1)->id == 7);
  assert(!MmxRenderAssetsObjectSprite(ram, 0xe68, 1));
  MmxRenderAssetsSetRom(NULL, 0);
  assert(!MmxRenderAssetsSprite(0, 0, 7));
}
static void spark_effects(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 6;
  ram[0x1f0a] = 4; ram[0x1e89] = 2;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.cgram[0] = ppu.fixedColor = 0x7fff;
  ppu.cgadsub = 0xa0; /* Backdrop is dark until a light disables subtraction. */
  static const uint8_t profile[] = {0,0,0,0,0,0,1,1,2,3,3,4,8,8,7,6,6,5,5,5,5,5,5,3,0};
  memcpy(rom_bytes + 0x35136, profile, sizeof(profile));
  ram[0xe68] = 1; ram[0xe69] = 2; ram[0xe72] = 0x37; ram[0xe95] = 0x40;
  put_word(0xe8a, 400); put_word(0xe8c, 100); /* Outside native range, light state still zero. */
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[76 * v.width + v.extra + 436] == 0xffffff);
  assert(output[76 * v.width + v.extra + 435] == 0);
  assert(output[100 * v.width + v.extra + 424] == 0xffffff);
  assert(output[75 * v.width + v.extra + 500] == 0);
  /* A second, mirrored light can occupy the other margin simultaneously. */
  memcpy(ram + 0xea8, ram + 0xe68, 64); ram[0xeb3] = 1; ram[0xed5] = 0x80;
  put_word(0xeca, (uint16_t)-100);
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[76 * v.width + v.extra - 136] == 0xffffff);
  assert(output[76 * v.width + v.extra - 135] == 0);
  assert(output[100 * v.width + v.extra + 500] == 0xffffff);
  g_mmx_render_asset_repairs = false;
  assert(MmxRendererDraw(output, v, false));
  assert(output[100 * v.width + v.extra + 500] == 0);
  g_mmx_render_asset_repairs = true;
  ram[0xea8] = 0; ram[0xe6b] = 1; ram[0xea3] = 13; ram[0xe87] = 24;
  put_word(0xe9e, 90);
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[90 * v.width + v.extra + 500] == 0xffffff);
  assert(output[103 * v.width + v.extra + 500] == 0);
  assert(output[90 * v.width + v.extra + 616] == 0);
  /* Thunder Slimer's BG2 actor tiles must never repeat into the margins. */
  memset(ram + 0xe68, 0, 128); ram[0x1f0a] = 1;
  ppu.cgadsub = 0; ppu.cgram[0] = 0; ppu.cgram[1] = 31;
  ppu.screenEnabled[0] = 2; ppu.bgXsc[1] = 0x50;
  for (int i = 0; i < 1024; ++i) ppu.vram[0x5000 + i] = 1;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = 255;
  put_word(0xb98, 0x8000); ram[0xb9a] = 0x80;
  for (int q = 0; q < 4; ++q) rom_word(q * 2, 1);
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[50 * v.width + v.extra - 100] == 0xff0000);
  ram[0x1e89] = 0x0c;
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[50 * v.width + v.extra - 100] == 0);
  assert(output[50 * v.width + v.extra + 100] == 0xff0000);
}
static void airport_panorama_edge(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 5; ram[0x1e89] = 0x0e;
  put_word(0x1e50, 0x600); put_word(0x1e8d, 118);
  put_word(0xb98, 0x8000); ram[0xb9a] = 0x80;
  ram[0xec01] = 1; ram[0xec02] = 2; ram[0xec03] = 3;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 40; ++x)
    put_word(0xa600 + (x / 16) * 512 + y * 32 + (x % 16) * 2, x == 39 ? 2 : 1);
  for (int q = 0; q < 4; ++q) { rom_word(8 + q * 2, 1); rom_word(16 + q * 2, 0x402); }
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2; ppu.hScroll[1] = 118;
  ppu.bgXsc[1] = 8; ppu.cgram[1] = 31; ppu.cgram[17] = 31 << 10;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = ppu.vram[32 + y] = 255;
  capture(); MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  assert(MmxRendererDraw(output, v, false));
  assert(output[80 * v.width + v.extra + 128] == 0); /* Native still uses its own tilemap. */
  assert(output[80 * v.width + v.extra + 530] == 0x0000ff); /* Reflect the painted edge. */
  assert(output[80 * v.width + v.extra + 560] == 0xff0000); /* No blue backdrop hole. */
  put_word(0x1e90, 256); /* Other airport/roof planes are not the panorama. */
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[80 * v.width + v.extra + 560] == 0);
}

static void wide_water_plane(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 1;
  put_word(0xb95, 0x8000); ram[0xb97] = 0x80;
  for (int i = 0; i < 256; ++i) put_word(0x2000 + i * 2, 1);
  for (int q = 0; q < 4; ++q) rom_word(8 + q * 2, 1);
  ppu.inidisp = 15; ppu.bgmode = 9; ppu.screenEnabled[0] = 5; ppu.screenEnabled[1] = 1;
  ppu.cgwsel = 2; ppu.cgadsub = 0x44; ppu.bgTileAdr = 0x400;
  ppu.bgXsc[0] = 0x10; ppu.bgXsc[2] = 8; ppu.cgram[1] = 31; ppu.cgram[5] = 31 << 10;
  for (int i = 0; i < 1024; ++i) ppu.vram[0x1000 + i] = 1;
  for (int i = 4 * 32; i < 1024; ++i) ppu.vram[0x800 + i] = 0x2402;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = ppu.vram[0x4010 + y] = 255;
  capture(); MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  assert(MmxRendererDraw(output, v, false));
  for (int x = 0; x < v.width; ++x) {
    assert(output[16 * v.width + x] == 0xff0000); /* Above the waterline. */
    assert(output[40 * v.width + x] == 0x7b007b); /* Same half blend across both seams. */
  }
  ram[0x1f7a] = 0; /* Dialogue overlays in other stages must remain bounded. */
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[40 * v.width + v.extra + 128] == 0x7b007b);
  assert(output[40 * v.width + 128] == 0xff0000);
}

static void buried_submarine(void) {
  memset(&ppu, 0, sizeof(ppu)); memset(ram, 0, sizeof(ram)); memset(rom_bytes, 0, sizeof(rom_bytes));
  MmxRendererReset(); MmxRendererSetRom(NULL, 0); MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  ram[0xd1] = 2; ram[0xd2] = ram[0xd3] = 4; ram[0x1f7a] = 1;
  put_word(0x1e4d, 0xa53); put_word(0x1e50, 0x20f);
  put_word(0xb95, 0x8000); ram[0xb97] = 0x80;
  ram[0xe800 + 2 * 32 + 11] = 1; ram[0xe800 + 2 * 32 + 12] = 2;
  for (int y = 5; y < 10; ++y) for (int x = 12; x < 20; ++x)
    put_word(0x2200 + (x / 16) * 512 + y * 32 + (x % 16) * 2, 1);
  for (int q = 0; q < 4; ++q) rom_word(8 + q * 2, 1);
  rom_word(0x34bec, 0x258); rom_word(0x34bf2, 0x29f);
  ram[0xe68] = 1; ram[0xe72] = 0x21; ram[0xe73] = 0x80; ram[0xe6a] = 6; put_word(0xe6d, 0xbce);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1;
  ppu.hScroll[0] = 0x253; ppu.vScroll[0] = 0x20f; ppu.bgXsc[0] = 8; ppu.cgram[1] = 31;
  for (int y = 0; y < 8; ++y) ppu.vram[16 + y] = 255;
  capture(); MmxRenderView v = MmxRendererViewport(MMX_ASPECT_ADAPTIVE, 2048, 300);
  assert(MmxRendererDraw(output, v, false));
  assert(output[96 * v.width + v.extra + 389] == 0); /* Hidden at the owner's earlier camera. */
  ram[0xe6a] = 4; /* The native emergence state owns presentation now. */
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[96 * v.width + v.extra + 389] == 0xff0000);
  ram[0xe69] = 2; ram[0xe6a] = 0;
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[96 * v.width + v.extra + 389] == 0xff0000);
  ram[0xe69] = 0; ram[0xe73] = 0; /* Ordinary surface variant is never hidden. */
  capture(); assert(MmxRendererDraw(output, v, false));
  assert(output[96 * v.width + v.extra + 389] == 0xff0000);
}

int main(void) { geometry(); raster_and_hud(); sprite_coordinates(); expanded_capacity(); background_resources(); dialogue_and_password(); highway_arena_sky(); distant_doors(); storm_background_prefill(); resource_decode(); spark_effects(); airport_panorama_edge(); wide_water_plane(); buried_submarine(); return 0; }
