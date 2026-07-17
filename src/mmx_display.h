#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-only display controls. These never modify emulated SNES state. */
void MmxDisplay_SetWidescreenEnabled(bool enabled);
bool MmxDisplay_IsWidescreenEnabled(void);
bool MmxDisplay_IsWidescreenActive(void);
int MmxDisplay_GetCurrentFrameWidth(void);

typedef struct MmxDisplayViewport {
  int x, y, width, height;
} MmxDisplayViewport;

/* Pure geometry: SNES active pixels have a 7:6 horizontal pixel aspect. */
int MmxDisplay_ComputeFrameWidth(int drawable_width, int drawable_height,
                                 bool widescreen);
void MmxDisplay_ComputePresentationSize(int frame_width, int frame_height,
                                        int *width, int *height);
void MmxDisplay_ComputeViewport(int source_width, int source_height,
                                int drawable_width, int drawable_height,
                                bool ignore_aspect, bool integer_scale,
                                MmxDisplayViewport *viewport);
int MmxDisplay_GetWindowBaseWidth(int frame_width);
int MmxDisplay_GetWindowBaseHeight(void);

#ifdef __cplusplus
}
#endif
