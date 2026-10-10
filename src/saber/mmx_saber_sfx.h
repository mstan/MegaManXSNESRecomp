#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MMXSFX2 is a deliberately small, fixed-layout, little-endian cache.  The
 * cache contains only post-S-DSP mono PCM; OGG/Vorbis and BRR are
 * converter-only. */
enum {
  MMX_SABER_SFX_MAGIC_BYTES = 8,
  MMX_SABER_SFX_VERSION = 2,
  MMX_SABER_SFX_CLIP_COUNT = 3,
  MMX_SABER_SFX_HEADER_BYTES = 24,
  MMX_SABER_SFX_RECORD_BYTES = 20,
  MMX_SABER_SFX_NATIVE_SAMPLE_RATE = 32000,
  MMX_SABER_SFX_CHANNELS = 1,
  MMX_SABER_SFX_MIN_SAMPLE_RATE = 32000,
  MMX_SABER_SFX_MAX_SAMPLE_RATE = 32000,
  MMX_SABER_SFX_MAX_PCM_BYTES = 16u * 1024u * 1024u
};

typedef enum MmxSaberSfxClipId {
  MMX_SABER_SFX_CLIP_SABER_1 = 0,
  MMX_SABER_SFX_CLIP_SABER_2 = 1,
  MMX_SABER_SFX_CLIP_SABER_3 = 2
} MmxSaberSfxClipId;

/* Short aliases keep call sites readable while retaining the cache's names. */
#define MMX_SABER_SFX_CLIP_1 MMX_SABER_SFX_CLIP_SABER_1
#define MMX_SABER_SFX_CLIP_2 MMX_SABER_SFX_CLIP_SABER_2
#define MMX_SABER_SFX_CLIP_3 MMX_SABER_SFX_CLIP_SABER_3

typedef enum MmxSaberSfxAttackCue {
  MMX_SABER_SFX_ATTACK_GROUND_SLASH_1 = 0,
  MMX_SABER_SFX_ATTACK_GROUND_SLASH_2,
  MMX_SABER_SFX_ATTACK_GROUND_SLASH_3,
  MMX_SABER_SFX_ATTACK_X3_FINISHER,
  MMX_SABER_SFX_ATTACK_AIR,
  MMX_SABER_SFX_ATTACK_WALL,
  MMX_SABER_SFX_ATTACK_DASH,
  MMX_SABER_SFX_ATTACK_COUNT
} MmxSaberSfxAttackCue;

#define MMX_SABER_SFX_ATTACK_CUE_GROUND_SLASH_1 \
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_1
#define MMX_SABER_SFX_ATTACK_CUE_GROUND_SLASH_2 \
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_2
#define MMX_SABER_SFX_ATTACK_CUE_GROUND_SLASH_3 \
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_3
#define MMX_SABER_SFX_ATTACK_CUE_X3_FINISHER MMX_SABER_SFX_ATTACK_X3_FINISHER
#define MMX_SABER_SFX_ATTACK_CUE_AIR MMX_SABER_SFX_ATTACK_AIR
#define MMX_SABER_SFX_ATTACK_CUE_WALL MMX_SABER_SFX_ATTACK_WALL
#define MMX_SABER_SFX_ATTACK_CUE_DASH MMX_SABER_SFX_ATTACK_DASH
#define MMX_SABER_ATTACK_GROUND_SLASH_1 MMX_SABER_SFX_ATTACK_GROUND_SLASH_1
#define MMX_SABER_ATTACK_GROUND_SLASH_2 MMX_SABER_SFX_ATTACK_GROUND_SLASH_2
#define MMX_SABER_ATTACK_GROUND_SLASH_3 MMX_SABER_SFX_ATTACK_GROUND_SLASH_3
#define MMX_SABER_ATTACK_X3_FINISHER MMX_SABER_SFX_ATTACK_X3_FINISHER
#define MMX_SABER_ATTACK_AIR MMX_SABER_SFX_ATTACK_AIR
#define MMX_SABER_ATTACK_WALL MMX_SABER_SFX_ATTACK_WALL
#define MMX_SABER_ATTACK_DASH MMX_SABER_SFX_ATTACK_DASH

typedef struct MmxSaberSfxClip {
  uint32_t channels;
  uint32_t sample_rate;
  uint32_t frame_count;
  uint32_t byte_length;
  const int16_t *samples;
} MmxSaberSfxClip;

typedef struct MmxSaberSfx MmxSaberSfx;

/* Parse and copy a complete sidecar.  The input remains caller-owned. */
bool MmxSaberSfxParse(const uint8_t *data, size_t size,
                      MmxSaberSfx **out, char *reason, size_t reason_size);

/* LoadFile uses the same parser and publishes no clips on any failure. */
MmxSaberSfx *MmxSaberSfxLoadFile(const char *path, char *reason,
                                 size_t reason_size);
void MmxSaberSfxFree(MmxSaberSfx *sfx);

const MmxSaberSfxClip *MmxSaberSfxClipAt(const MmxSaberSfx *sfx,
                                         unsigned clip_id);

/* Host audio operations are injected so the parser/loader tests do not link
 * the desktop mixer.  The Saber plugin supplies these after activation; clip
 * registration still happens lazily on the first play request. */
typedef int (*MmxSaberSfxRegisterPcmFn)(const int16_t *samples,
                                        uint32_t frame_count,
                                        uint32_t sample_rate,
                                        uint32_t channels,
                                        void *context);
typedef void (*MmxSaberSfxUnregisterFn)(int clip, void *context);
typedef int (*MmxSaberSfxPlayFn)(int clip, int volume_percent,
                                 void *context);
typedef bool (*MmxSaberSfxSuppressedFn)(void *context);

typedef struct MmxSaberSfxHost {
  MmxSaberSfxRegisterPcmFn register_pcm;
  MmxSaberSfxUnregisterFn unregister_clip;
  MmxSaberSfxPlayFn play;
  MmxSaberSfxSuppressedFn suppressed;
  void *context;
} MmxSaberSfxHost;

typedef void (*MmxSaberSfxWarningFn)(const char *reason, void *context);

void MmxSaberSfxSetHost(const MmxSaberSfxHost *host);
void MmxSaberSfxSetWarningCallback(MmxSaberSfxWarningFn callback,
                                   void *context);
void MmxSaberSfxSetVolume(int volume_percent);
int MmxSaberSfxVolume(void);

/* Runtime sidecar ownership and playback. */
bool MmxSaberSfxLoadRuntime(const char *path, char *reason,
                            size_t reason_size);
void MmxSaberSfxResetRuntime(void);
bool MmxSaberSfxLoaded(void);
void MmxSaberSfxPlay(unsigned clip_id);
void MmxSaberSfxPlayForAttack(MmxSaberSfxAttackCue cue);

/* Test/character code can inspect the map without coupling to mixer handles. */
unsigned MmxSaberSfxAttackClip(MmxSaberSfxAttackCue cue);
unsigned MmxSaberSfxLastClip(void);
unsigned MmxSaberSfxRegisteredClipCount(void);

#ifdef __cplusplus
}
#endif
