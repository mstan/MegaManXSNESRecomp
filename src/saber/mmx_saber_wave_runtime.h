#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Legacy X3 wave ABI copied from oldsaber/saber-zero-variant:
 * src/mmx_zero.h:101-135 and src/mmx_saber.c:1656-1723, 2383-2487. */
enum {
  MMX_SABER_WAVE_TAG_FAMILY = 0x5600,
  MMX_SABER_WAVE_TAG_FAMILY_MASK = 0xff00,
  MMX_SABER_WAVE_COLLISION_POINTER = 0xffa0,
  MMX_SABER_WAVE_COLLISION_ROM_OFFSET = 0x37fa0,
  MMX_SABER_WAVE_COLLISION_RECORD_BYTES = 4,
  MMX_SABER_WAVE_DAMAGE = 6,
  MMX_SABER_WAVE_PULSE_FRAMES = 4,
  MMX_SABER_WAVE_MAX_PULSES = 3,
  MMX_SABER_WAVE_LIFETIME = 96,
  MMX_SABER_WAVE_SPEED = 8,
  MMX_SABER_WAVE_MAX_VIEW_MARGIN = 272,
  MMX_SABER_WAVE_SPAWN_OFFSET_X = 24,
  MMX_SABER_WAVE_SPAWN_OFFSET_Y = -6,
  MMX_SABER_WAVE_SLOT_AGE = 0x2a,
  MMX_SABER_WAVE_SLOT_STATE = 0x2b,
  MMX_SABER_WAVE_SLOT_TARGET = 0x2c,
  MMX_SABER_WAVE_SLOT_PULSES = 0x2e,
  MMX_SABER_WAVE_SLOT_COUNTDOWN = 0x2f,
  MMX_SABER_WAVE_SLOT_TARGET_KIND = 0x30,
  MMX_SABER_WAVE_SLOT_TARGET_STATE = 0x31,
  MMX_SABER_WAVE_SLOT_STAGE = 0x32,
  MMX_SABER_WAVE_SLOT_TARGET_SUBSTATE = 0x33,
  MMX_SABER_WAVE_SLOT_TARGET_FLAGS = 0x34,
  MMX_SABER_WAVE_SLOT_BIRTH = 0x35,
  MMX_SABER_WAVE_SLOT_STATE_TRAVEL = 1,
  MMX_SABER_WAVE_SLOT_STATE_CUTTING = 2
};

/* The reservation is deliberately host-side only; the tag, inactive marker,
 * stage, and later live object are all kept in the ordinary guest slot. */
bool MmxSaberWaveRuntimeReserve(uint8_t *ram, unsigned *slot);
bool MmxSaberWaveRuntimeReleaseReservation(uint8_t *ram, unsigned slot);
bool MmxSaberWaveRuntimePublish(uint8_t *ram, unsigned slot);

/* Called from the frame bridge so a stage change cancels pending combo state
 * and retires waves even when the native weapon loop skips the projectile
 * pool. Returns true when the observed stage changed or a slot disagreed. */
bool MmxSaberWaveRuntimeObserveStage(uint8_t *ram);
void MmxSaberWaveRuntimeRetireAll(uint8_t *ram);
void MmxSaberWaveRuntimeReset(uint8_t *ram);

bool MmxSaberWaveRuntimeOwns(const uint8_t *ram, unsigned slot);
bool MmxSaberWaveRuntimeActive(const uint8_t *ram);
typedef struct MmxSaberWaveRuntimeLiveWave {
  int16_t world_x, world_y;
  uint8_t age;
  /* This is the old wave compositor's mirror bit from slot+$11. */
  bool facing_left;
} MmxSaberWaveRuntimeLiveWave;
unsigned MmxSaberWaveRuntimeLiveWaves(MmxSaberWaveRuntimeLiveWave *out,
                                      unsigned max);
unsigned MmxSaberWaveRuntimeWeaponTick(uint8_t *ram, unsigned slot,
                                       unsigned value);
unsigned MmxSaberWaveRuntimeDamage(uint8_t *ram, unsigned enemy,
                                   unsigned slot, unsigned value);
unsigned MmxSaberWaveRuntimeHitbox(const uint8_t *ram, unsigned enemy,
                                   unsigned slot, unsigned value);

/* The validated x3-saber-wave-v1.bin record is supplied by the asset owner.
 * Collision installation itself remains on the existing Saber extension path. */
void MmxSaberWaveRuntimeSetCollisionRecord(const uint8_t *record,
                                            size_t size);
void MmxSaberWaveRuntimeCollisionRom(uint8_t *rom, size_t size);
