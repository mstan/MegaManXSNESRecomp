#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MMX_ZERO_HUD_BYTES = 160 };
/* Read the verified X3 ROM badge for any Zero behavior. */
int MmxSourceAssetsReadZeroHud(const char *rom, uint8_t hud[MMX_ZERO_HUD_BYTES],
                               char *error, size_t error_size);
/* Source ROMs accept copier headers. Publish complete caches atomically. */
int MmxSourceAssetsBuild(const char *rom, unsigned game, int zero,
                        const char *output, char *error, size_t error_size);
int MmxArmorAssetsBuild(const char *rom, unsigned game, const char *output,
                       char *error, size_t error_size);
int MmxDashAssetsBuild(const char *rom,const char *output,char *error,size_t error_size);
#ifdef __cplusplus
}
#endif
