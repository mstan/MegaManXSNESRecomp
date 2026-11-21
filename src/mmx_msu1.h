#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "snes/msu1.h"

/* User playback belongs to the local audio timeline, never canonical rollback. */
typedef struct {
  uint8_t valid, fade_state, fade_volume, reserved;
  uint8_t playback[MSU1_PLAYBACK_STATE_SIZE];
} MmxMsu1State;

MmxMsu1State MmxMsu1GetState(void);
bool MmxMsu1ValidState(const MmxMsu1State *state);
void MmxMsu1StateLoaded(const MmxMsu1State *state);
