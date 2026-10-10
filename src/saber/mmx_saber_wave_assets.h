#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the private X3 ROM-derived Z-Saber wave cache. */
int MmxSaberWaveAssetsBuild(const char *rom, const char *output,
                            char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
