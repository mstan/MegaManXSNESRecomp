#include "mmx_display.h"

#include <assert.h>
#include <limits.h>

static void test_widths(void) {
  assert(MmxDisplay_ComputeFrameWidth(4, 3, true) == 256);
  assert(MmxDisplay_ComputeFrameWidth(16, 10, true) == 308);
  assert(MmxDisplay_ComputeFrameWidth(16, 9, true) == 342);
  assert(MmxDisplay_ComputeFrameWidth(21, 9, true) == 446);
  assert(MmxDisplay_ComputeFrameWidth(1, 2, true) == 256);
  assert(MmxDisplay_ComputeFrameWidth(INT_MAX, 1, true) == 446);
  assert(MmxDisplay_ComputeFrameWidth(0, 0, true) == 256);
  assert(MmxDisplay_ComputeFrameWidth(16, 9, false) == 256);
}

static void test_presentation(void) {
  int width, height;
  MmxDisplayViewport viewport;
  MmxDisplay_ComputePresentationSize(342, 224, &width, &height);
  assert(width == 399 && height == 224);
  MmxDisplay_ComputeViewport(342, 224, 1920, 1080, false, false, &viewport);
  assert(viewport.width == 1920 && viewport.height == 1078);
  assert(viewport.x == 0 && viewport.y == 1);
  MmxDisplay_ComputeViewport(256, 224, 800, 600, false, false, &viewport);
  assert(viewport.width == 800 && viewport.height == 600);
  assert(viewport.x == 0 && viewport.y == 0);
}

int main(void) {
  test_widths();
  test_presentation();
  return 0;
}
