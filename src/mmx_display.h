#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "desktop/display_aspect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-only display controls. These never modify emulated SNES state. */
void MmxDisplay_SetWidescreenEnabled(bool enabled);
bool MmxDisplay_IsWidescreenEnabled(void);
bool MmxDisplay_IsWidescreenActive(void);
int MmxDisplay_GetCurrentFrameWidth(void);

typedef SnesDisplayViewport MmxDisplayViewport;

/* Pure geometry. Widescreen extends the logical field by 4/3; display_aspect
 * independently selects how those pixels are presented. */
int MmxDisplay_ComputeFrameWidth(bool widescreen);
void MmxDisplay_ComputePresentationSize(int frame_width, int frame_height,
                                        SnesDisplayAspect display_aspect,
                                        int *width, int *height);
void MmxDisplay_ComputeViewport(int source_width, int source_height,
                                int drawable_width, int drawable_height,
                                SnesDisplayAspect display_aspect,
                                bool ignore_aspect, bool integer_scale,
                                MmxDisplayViewport *viewport);
int MmxDisplay_GetWindowBaseWidth(int frame_width,
                                  SnesDisplayAspect display_aspect);
int MmxDisplay_GetWindowBaseHeight(void);

/* Reattach a streamed PPU tilemap's 8-bit phase to MMX's full world camera. */
int MmxDisplay_ExpandStageScroll(uint16_t camera, uint16_t ppu_scroll);

#ifdef __cplusplus
}
#endif
