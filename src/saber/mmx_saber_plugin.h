#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Saber is a Zero behavior chosen in Add Zero or Co-op, not a character
 * package of its own. Its optional tuning lives in this settings feature. */
#define MMX_SABER_SETTINGS_PACKAGE "megaman-x.character.saber-zero"
#define MMX_SABER_SETTINGS_FEATURE "saber-zero"

/* Called by the Add Zero / Co-op activation after X3 Zero is loaded and its
 * hooks are registered. `package`/`feature` name the feature that owns the X3
 * ROM resource. Co-op reads each seat's own pad. On failure Zero keeps its
 * X3 behavior and the player has been told why. */
bool MmxSaberActivate(const char *package, const char *feature, bool coop);
bool MmxSaberEnabled(void);
bool MmxSaberAssetsLoaded(void);
bool MmxSaberRideAssetsLoaded(void);
bool MmxSaberWaveLoaded(void);

#ifdef __cplusplus
}
#endif
