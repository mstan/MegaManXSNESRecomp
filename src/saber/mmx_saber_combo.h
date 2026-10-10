#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mmx_saber_wave_runtime.h"

enum {
  MMX_SABER_DEFAULT_FINISHER_WINDOW = 27,
  MMX_SABER_MAX_FINISHER_WINDOW = 60
};

/* The window length is configured once when the package activates. */
void MmxSaberComboSetWindowFrames(unsigned frames);

/* Reset the Saber-owned, non-serialized combo state and any inactive wave
 * reservation. A NULL RAM pointer uses the most recently observed RAM. */
void MmxSaberComboReset(uint8_t *ram);

/* Cancel a window, pending request, and reservation on a lifecycle exit. */
void MmxSaberComboCancel(uint8_t *ram);

/* Run before the donor attack step. The return value tells the frame bridge
 * that this Y edge was claimed, including the atomic-allocation failure case.
 */
bool MmxSaberComboPrePlayer(uint8_t *ram, bool saber_pressed);

/* Callback polled by upstream Zero during its legacy player tick. */
bool MmxSaberComboLegacySlashRequest(const uint8_t *ram);

/* Run after upstream Zero's player-end callback. */
void MmxSaberComboPlayerEnd(uint8_t *ram);

/* Test/bridge observability for the Saber-owned window and reservation. */
unsigned MmxSaberComboWindowTicks(void);
unsigned MmxSaberComboReservedSlot(void);
unsigned MmxSaberComboFinisherCueCount(void);
