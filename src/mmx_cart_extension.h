#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Patchable cart windows for code relocated from X2/X3 at activation.
 *
 * X1's 1.5 MB image is padded to 2 MB on load by mirroring its last 512 KB;
 * that mirror is LoROM banks $30-$3F/$B0-$BF, which X1 never reads.
 * The end of X1's data bank $86 ($FA80-$FF3F) is $FF padding; relocated
 * bosses keep the tables X1's damage and terrain helpers read through that
 * data bank there ($FF40-$FFFF holds the air/ground saber, weapon, saber-wave
 * and Zero collision tables). Optionally the image can grow to 4 MB (upper half
 * mirroring the original 2 MB) for banks $F0-$FD.
 *
 * Every peer rebuilds identical bytes from the same verified source ROMs,
 * and romImageSize (the image identity) never changes, so nothing here is
 * part of snapshots. */
enum { MMX_CART_EXTENSION_FIRST_BANK=0xf0, MMX_CART_EXTENSION_LAST_BANK=0xfd };
enum { MMX_CART_PAD86_START=0x86fa80, MMX_CART_PAD86_END=0x86ff40 };

/* Grow the loaded LoROM image to 4 MB once (banks $F0-$FD). */
bool MmxCartExtensionEnsure(void);
bool MmxCartExtensionReady(void);
/* True when the loaded image is X1's padded 2 MB (or grown 4 MB) LoROM and
 * its $86 padding is intact or already owned by a build. */
bool MmxCartPatchAvailable(void);
/* Write/read inside one patch window (padding banks $30-$3F/$B0-$BF, the
 * $86 padding, or grown banks $F0-$FD). Writes invalidate decoded code. */
bool MmxCartExtensionWrite(uint32_t address,const void *data,size_t size);
bool MmxCartExtensionRead(uint32_t address,void *data,size_t size);
/* Restore every window to the original image. */
void MmxCartExtensionClear(void);
