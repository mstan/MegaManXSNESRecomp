#include "mmx_display.h"

#include <stddef.h>

#include "widescreen.h"

static int ClampEven(int64_t value) {
  value &= ~1;
  if (value < 256) value = 256;
  if (value > 256 + 2 * kWsExtraMax)
    value = 256 + 2 * kWsExtraMax;
  return (int)value;
}

int MmxDisplay_ComputeFrameWidth(int drawable_width, int drawable_height,
                                 bool widescreen) {
  if (!widescreen || drawable_width <= 0 || drawable_height <= 0)
    return 256;

  /* 224 * 6 / 7 preserves the SNES 7:6 pixel aspect while matching the
   * requested display aspect. Use 64-bit arithmetic for hostile window sizes. */
  int64_t numerator = (int64_t)drawable_width * 192;
  int64_t width = numerator / drawable_height;
  int64_t remainder = numerator % drawable_height;
  if (remainder * 2 > drawable_height ||
      (remainder * 2 == drawable_height && (width & 1) != 0))
    width++;
  if (width & 1) width++;
  return ClampEven(width);
}

void MmxDisplay_ComputePresentationSize(int frame_width, int frame_height,
                                        int *width, int *height) {
  if (!width || !height) return;
  if (frame_width <= 0) frame_width = 256;
  if (frame_height <= 0) frame_height = 224;
  *width = (frame_width * 7 + 3) / 6;
  *height = frame_height;
}

void MmxDisplay_ComputeViewport(int source_width, int source_height,
                                int drawable_width, int drawable_height,
                                bool ignore_aspect, bool integer_scale,
                                MmxDisplayViewport *viewport) {
  if (!viewport) return;
  viewport->x = viewport->y = 0;
  viewport->width = drawable_width > 0 ? drawable_width : 1;
  viewport->height = drawable_height > 0 ? drawable_height : 1;
  if (ignore_aspect || source_width <= 0 || source_height <= 0 ||
      drawable_width <= 0 || drawable_height <= 0)
    return;

  double source_display_width = (double)source_width * 7.0 / 6.0;
  double scale_x = drawable_width / source_display_width;
  double scale_y = (double)drawable_height / source_height;
  double scale = scale_x < scale_y ? scale_x : scale_y;
  if (integer_scale && scale >= 1.0)
    scale = (double)(int)scale;
  if (scale <= 0.0) scale = scale_x < scale_y ? scale_x : scale_y;

  viewport->width = (int)(source_display_width * scale + 0.5);
  viewport->height = (int)(source_height * scale + 0.5);

  /* The integer PPU width cannot represent every display aspect exactly.
   * At 16:9, for example, the ideal 7:6-PAR frame is 341 1/3 pixels wide,
   * while the renderer must choose the even width 342. Do not turn that
   * harmless sub-pixel quantization into a visible 1-2 px letterbox seam.
   * Snap only gaps smaller than one native source pixel; real aspect-ratio
   * differences still letterbox normally, and integer scaling stays exact. */
  if (!integer_scale) {
    int width_gap = drawable_width - viewport->width;
    int height_gap = drawable_height - viewport->height;
    int native_x = (drawable_width + 255) / 256;
    int native_y = (drawable_height + 223) / 224;
    if (width_gap > 0 && width_gap <= native_x)
      viewport->width = drawable_width;
    if (height_gap > 0 && height_gap <= native_y)
      viewport->height = drawable_height;
  }
  viewport->x = (drawable_width - viewport->width) / 2;
  viewport->y = (drawable_height - viewport->height) / 2;
}

int MmxDisplay_GetWindowBaseWidth(int frame_width) {
  if (frame_width <= 0) frame_width = 256;
  return (frame_width * 5 + 2) / 4;
}

int MmxDisplay_GetWindowBaseHeight(void) { return 240; }

int MmxDisplay_ExpandStageScroll(uint16_t camera, uint16_t ppu_scroll) {
  /* MMX streams the current 256x256 stage screen into a reusable SNES
   * tilemap page. The PPU scroll is authoritative for the pixel phase, but
   * it loses the full stage-screen number after crossing a page. Choose the
   * phase copy nearest the camera so shake/HDMA offsets survive page edges. */
  int world = (camera & ~0xff) | (ppu_scroll & 0xff);
  int delta = world - (int)camera;
  if (delta > 128)
    world -= 256;
  else if (delta < -128)
    world += 256;
  return world;
}
