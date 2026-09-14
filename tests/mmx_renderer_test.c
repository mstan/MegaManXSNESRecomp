#include "mmx_renderer.h"
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
  MmxRendererLatchSprites(); capture();
  MmxRenderView v = MmxRendererViewport(MMX_ASPECT_32_9, 0, 0);
  assert(MmxRendererDraw(output, v, false));
  assert(output[40 * v.width + v.extra + 300] == 0xff0000);
  assert(output[40 * v.width + v.extra - 200] == 0xff0000);
  assert(output[40 * v.width + v.extra - 212] == 0);
  assert(MmxRendererGetStats().margin_sprite_pixels == 128);
  g_mmx_custom_renderer = false;
}
int main(void) { geometry(); raster_and_hud(); sprite_coordinates(); return 0; }
