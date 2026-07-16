#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-only display controls. These never modify emulated SNES state. */
void MmxDisplay_SetWidescreenEnabled(bool enabled);
bool MmxDisplay_IsWidescreenEnabled(void);

#ifdef __cplusplus
}
#endif
