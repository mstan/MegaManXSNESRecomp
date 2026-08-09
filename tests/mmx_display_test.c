#include "mmx_display.h"

#include <assert.h>

static void test_widths(void) {
  assert(MmxDisplay_ComputeFrameWidth(true) == 342);
  assert(MmxDisplay_ComputeFrameWidth(false) == 256);
}

static void test_presentation(void) {
  int width, height;
  MmxDisplayViewport viewport;

  MmxDisplay_ComputePresentationSize(
      256, 224, kSnesDisplayAspect_Crt4x3, &width, &height);
  assert(width == 299 && height == 224);
  MmxDisplay_ComputePresentationSize(
      256, 224, kSnesDisplayAspect_SquarePixels8x7, &width, &height);
  assert(width == 256 && height == 224);
  MmxDisplay_ComputePresentationSize(
      256, 224, kSnesDisplayAspect_SquareFrame1x1, &width, &height);
  assert(width == 224 && height == 224);

  MmxDisplay_ComputePresentationSize(
      342, 224, kSnesDisplayAspect_Crt4x3, &width, &height);
  assert(width == 399 && height == 224);
  MmxDisplay_ComputePresentationSize(
      342, 224, kSnesDisplayAspect_SquarePixels8x7, &width, &height);
  assert(width == 342 && height == 224);
  MmxDisplay_ComputePresentationSize(
      342, 224, kSnesDisplayAspect_SquareFrame1x1, &width, &height);
  assert(width == 299 && height == 224);

  assert(MmxDisplay_GetWindowBaseWidth(
      256, kSnesDisplayAspect_Crt4x3) == 320);
  assert(MmxDisplay_GetWindowBaseWidth(
      256, kSnesDisplayAspect_SquarePixels8x7) == 274);
  assert(MmxDisplay_GetWindowBaseWidth(
      256, kSnesDisplayAspect_SquareFrame1x1) == 240);

  MmxDisplay_ComputeViewport(342, 224, 1920, 1080,
                             kSnesDisplayAspect_Crt4x3,
                             false, false, &viewport);
  assert(viewport.width == 1920 && viewport.height == 1080);
  assert(viewport.x == 0 && viewport.y == 0);
  MmxDisplay_ComputeViewport(342, 224, 1536, 1008,
                             kSnesDisplayAspect_SquarePixels8x7,
                             false, false, &viewport);
  assert(viewport.width == 1536 && viewport.height == 1008);
  assert(viewport.x == 0 && viewport.y == 0);
  MmxDisplay_ComputeViewport(342, 224, 1200, 900,
                             kSnesDisplayAspect_SquareFrame1x1,
                             false, false, &viewport);
  assert(viewport.width == 1200 && viewport.height == 900);
  assert(viewport.x == 0 && viewport.y == 0);

  /* A square-pixel widescreen frame remains 32:21 on a 16:9 monitor. */
  MmxDisplay_ComputeViewport(342, 224, 1920, 1080,
                             kSnesDisplayAspect_SquarePixels8x7,
                             false, false, &viewport);
  assert(viewport.width == 1649 && viewport.height == 1080);
  assert(viewport.x == 135 && viewport.y == 0);

  MmxDisplay_ComputeViewport(256, 224, 800, 600,
                             kSnesDisplayAspect_Crt4x3,
                             false, false, &viewport);
  assert(viewport.width == 800 && viewport.height == 600);
  assert(viewport.x == 0 && viewport.y == 0);
}

static void test_streamed_stage_scroll(void) {
  assert(MmxDisplay_ExpandStageScroll(0x0234, 0x0034) == 0x0234);
  assert(MmxDisplay_ExpandStageScroll(0x0520, 0x0320) == 0x0520);
  assert(MmxDisplay_ExpandStageScroll(0x0202, 0x03fe) == 0x01fe);
  assert(MmxDisplay_ExpandStageScroll(0x01fe, 0x0002) == 0x0202);
}

int main(void) {
  test_widths();
  test_presentation();
  test_streamed_stage_scroll();
  return 0;
}
