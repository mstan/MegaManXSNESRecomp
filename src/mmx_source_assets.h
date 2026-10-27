#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Validate original ROM identity (copier headers accepted), extract locally,
 * and atomically publish a private runtime cache. No external interpreter. */
int MmxSourceAssetsBuild(const char *rom, unsigned game, int zero,
                        const char *output, char *error, size_t error_size);
int MmxArmorAssetsBuild(const char *rom, unsigned game, const char *output,
                       char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
