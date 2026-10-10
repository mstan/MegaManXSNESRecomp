#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MMX_RUSH_AUDIO_CUES=32 };
typedef struct MmxBossRushAudioCue {
  uint32_t generation,frame;
  uint8_t command, pan, seat, kind;
  uint16_t object, reserved;
} MmxBossRushAudioCue;
/* Guest requests survive snapshots; PCM, playback cursors and acknowledgements
 * belong to presentation. The desktop delivers only after a committed frame. */
typedef struct MmxBossRushAudioState {
  uint32_t sequence,frame;
  MmxBossRushAudioCue cues[MMX_RUSH_AUDIO_CUES];
} MmxBossRushAudioState;
MmxBossRushAudioState MmxBossRushAudioGetState(void);
bool MmxBossRushAudioValidState(const MmxBossRushAudioState *state);
bool MmxBossRushAudioSetState(const MmxBossRushAudioState *state);
void MmxBossRushAudioReset(void);
void MmxBossRushAudioTick(void);
void MmxBossRushAudioLoaded(void);
void MmxBossRushAudioPrepare(const uint8_t *rom,size_t size);
bool MmxBossRushAudioQueue(unsigned seat,unsigned object,unsigned command,unsigned pan,uint32_t generation);
void MmxBossRushAudioPresent(void);
/* ROM-backed diagnostics; no extracted clips are shipped. */
const int16_t *MmxBossRushAudioClip(unsigned command,uint32_t *frames);
