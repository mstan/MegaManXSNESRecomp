#ifndef MMX_PARALLAX_H
#define MMX_PARALLAX_H

#include <stdbool.h>

/* Mega Man X's binding for the shared layered-parallax presenter
 * (snesrecomp/runner/src/parallax.h, docs/PARALLAX.md). This file holds the
 * only MMX-specific knowledge the feature needs: which BG layer is which
 * depth, and when a frame is a stage the effect makes sense on. */

/* Install the MMX layer profile and seed the master switch from g_config.
 * Call once, after ParseConfigFile. */
void MmxParallax_Init(void);

/* Per-frame gate + capture policy. Call from MmxDisplay_PreparePpuFrame, after
 * PpuBeginDrawing/PpuSetExtraSpace have fixed this frame's geometry. */
void MmxParallax_PrepareFrame(int frame_width, int frame_height, int extra);

/* Hotkey toggle (persists to config.ini, like the widescreen toggle). */
void MmxParallax_Toggle(void);
bool MmxParallax_IsEnabled(void);

#endif  /* MMX_PARALLAX_H */
