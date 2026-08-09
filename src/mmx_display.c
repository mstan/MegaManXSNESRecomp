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

int MmxDisplay_ComputeFrameWidth(bool widescreen) {
  return widescreen
      ? ClampEven(SnesDisplayAspect_ComputeWideFrameWidth(256)) : 256;
}

void MmxDisplay_ComputePresentationSize(int frame_width, int frame_height,
                                        SnesDisplayAspect display_aspect,
                                        int *width, int *height) {
  SnesDisplayAspect_ComputePresentationSize(
      frame_width, frame_height, display_aspect, width, height);
}

void MmxDisplay_ComputeViewport(int source_width, int source_height,
                                int drawable_width, int drawable_height,
                                SnesDisplayAspect display_aspect,
                                bool ignore_aspect, bool integer_scale,
                                MmxDisplayViewport *viewport) {
  SnesDisplayAspect_ComputeViewport(
      source_width, source_height, drawable_width, drawable_height,
      display_aspect, ignore_aspect, integer_scale, viewport);
}

int MmxDisplay_GetWindowBaseWidth(int frame_width,
                                  SnesDisplayAspect display_aspect) {
  return SnesDisplayAspect_ComputeWindowWidth(
      frame_width, 224, MmxDisplay_GetWindowBaseHeight(), display_aspect);
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
