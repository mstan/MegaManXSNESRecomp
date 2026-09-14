#include "mmx_renderer.h"
#include <stdio.h>

static bool bmp(const char *path, const uint32_t *pixels, int width) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  uint32_t size = (uint32_t)width * 224 * 4;
  const uint8_t signature[2] = {'B', 'M'};
  uint32_t header[] = {54 + size, 0, 54, 40, (uint32_t)width, 224, 0x200001, 0, size, 0, 0, 0, 0};
  bool ok = fwrite(signature, 2, 1, f) == 1 && fwrite(header, sizeof(header), 1, f) == 1;
  for (int y = 223; y >= 0; --y) ok &= fwrite(pixels + y * width, width * 4, 1, f) == 1;
  return fclose(f) == 0 && ok;
}
int main(int argc, char **argv) {
  if (argc != 6) {
    fprintf(stderr, "usage: mmx_render_capture capture rom aspect hud(0|1) output.bmp\n"); return 2;
  }
  FILE *f = fopen(argv[2], "rb");
  if (!f || fseek(f, 0, SEEK_END)) return 2;
  long size = ftell(f); rewind(f);
  if (size <= 0 || size > 0x1000000) return 2;
  uint8_t *bytes = malloc(size);
  if (!bytes || fread(bytes, size, 1, f) != 1) return 2;
  fclose(f);
  size_t copier = (size % 0x8000) == 512 ? 512 : 0;
  MmxRendererSetRom(bytes + copier, size - copier);
  if (!MmxRendererLoadCapture(argv[1])) return 2;
  MmxRenderAspect aspect = !strcmp(argv[3], "16:9") ? MMX_ASPECT_16_9 :
      !strcmp(argv[3], "21:9") ? MMX_ASPECT_21_9 : MMX_ASPECT_32_9;
  MmxRenderView view = !strcmp(argv[3], "4:3") ? (MmxRenderView){256,0,4.0/3.0} : MmxRendererViewport(aspect, 16, 9);
  uint32_t *pixels = calloc((size_t)view.width * 224, 4);
  if (!pixels || !MmxRendererDraw(pixels, view, false)) return 2;
  unsigned differences = 0;
  const uint32_t *stock = MmxRendererStockFrame();
  for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x)
    differences += ((stock[y * 256 + x] ^ pixels[y * view.width + x + view.extra]) & 0xffffff) != 0;
  MmxRenderStats stats = MmxRendererGetStats();
  printf("{\"width\":%d,\"native_differences\":%u,\"custom_lines\":%u,\"fallback_lines\":%u,\"pieces\":%u,\"margin_sprite_pixels\":%u}\n",
          view.width, differences, stats.custom_lines, stats.fallback_lines, stats.pieces, stats.margin_sprite_pixels);
  if (atoi(argv[4]) && !MmxRendererDraw(pixels, view, true)) return 2;
  bool ok = bmp(argv[5], pixels, view.width);
  free(pixels); free(bytes);
  return !ok ? 2 : differences ? 1 : 0;
}
