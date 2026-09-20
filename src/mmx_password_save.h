#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Host file only: no cartridge SRAM, address mapping, or save-state extension. */
bool MmxPasswordOpen(const char *path);
void MmxPasswordClose(void);
void MmxPasswordPrefill(uint8_t *ram);
void MmxPasswordCapture(const uint8_t *ram);
#ifdef __cplusplus
}
#endif
