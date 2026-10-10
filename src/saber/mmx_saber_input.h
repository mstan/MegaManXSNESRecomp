#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../mmx_zero.h"

/* Physical SNES controller bits used by the native input poller. */
enum {
  MMX_SABER_PAD_B = 0x8000,
  MMX_SABER_PAD_Y = 0x4000,
  MMX_SABER_PAD_SELECT = 0x2000,
  MMX_SABER_PAD_START = 0x1000,
  MMX_SABER_PAD_UP = 0x0800,
  MMX_SABER_PAD_DOWN = 0x0400,
  MMX_SABER_PAD_LEFT = 0x0200,
  MMX_SABER_PAD_RIGHT = 0x0100,
  MMX_SABER_PAD_A = 0x0080,
  MMX_SABER_PAD_X = 0x0040,
  MMX_SABER_PAD_L = 0x0020,
  MMX_SABER_PAD_R = 0x0010
};

/* Native mapped input bytes.  These are the bytes the old branch patched:
 * $0BDE, $0BDF, $0BE1, $0BE2, and $0BE3, respectively. */
enum {
  MMX_SABER_NATIVE_HORIZONTAL_BITS = 0x03,
  MMX_SABER_NATIVE_FIRE_BIT = 0x40,
  MMX_SABER_NATIVE_DASH_BIT = 0x80,
  MMX_SABER_NATIVE_JUMP_BIT = 0x80
};

typedef struct MmxSaberPhysicalPad {
  uint16_t buttons;
  uint16_t prev_buttons;
} MmxSaberPhysicalPad;

typedef struct MmxSaberNativePad {
  uint8_t dash_held;      /* $0BDE */
  uint8_t action_held;    /* $0BDF */
  uint8_t fire_previous;  /* $0BE1 */
  uint8_t dash_pressed;   /* $0BE2 */
  uint8_t action_pressed; /* $0BE3 */
} MmxSaberNativePad;

typedef enum MmxSaberPadPhase {
  SABER_PHASE_IDLE,
  SABER_PHASE_STARTUP,
  SABER_PHASE_ACTIVE,
  SABER_PHASE_RECOVERY
} MmxSaberPadPhase;

typedef enum MmxSaberPadKind {
  SABER_KIND_NONE,
  SABER_KIND_GROUND1,
  SABER_KIND_GROUND2,
  SABER_KIND_GROUND3,
  SABER_KIND_AIR,
  SABER_KIND_WALL,
  SABER_KIND_DASH,
  SABER_KIND_SABER_LAND,
  SABER_KIND_LAND = SABER_KIND_SABER_LAND
} MmxSaberPadKind;

typedef struct MmxSaberPadSaber {
  MmxSaberPadPhase phase;
  MmxSaberPadKind kind;
  bool wave_active;
  bool finisher_active;
  bool release_pending;
} MmxSaberPadSaber;

typedef struct MmxSaberPadZero {
  bool buster_selected;
  bool hurt;
  bool dead_or_reset;
  bool grounded;
} MmxSaberPadZero;

typedef struct MmxSaberPadOut {
  MmxSaberNativePad native;
  MmxZeroLegacyIntent legacy;
  bool legacy_override;
  bool fire_blocked;
  bool release_pending;
  bool saber_pressed;
  bool saber_held;
} MmxSaberPadOut;

MmxSaberPadOut MmxSaberComputePad(MmxSaberPhysicalPad phys,
                                  MmxSaberNativePad native_mapped,
                                  MmxSaberPadSaber saber,
                                  MmxSaberPadZero zero);
