#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Validate original ROM identity (copier headers accepted), extract locally,
 * and atomically publish a private runtime cache. No external interpreter. */
int MmxSourceAssetsBuild(const char *rom, unsigned game, int zero,
                        const char *output, char *error, size_t error_size);
/* Validate the same original ROM and return its header-free image in memory
 * (malloc-owned; free with free()). Nothing is written to disk. */
int MmxSourceRomLoad(const char *rom, unsigned game, unsigned char **image,
                     size_t *size, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
