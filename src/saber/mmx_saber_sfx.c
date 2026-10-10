#include "mmx_saber_sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t kMmxSaberSfxMagic[MMX_SABER_SFX_MAGIC_BYTES] =
    {'M', 'M', 'X', 'S', 'F', 'X', '2', 0};

struct MmxSaberSfx {
  MmxSaberSfxClip clips[MMX_SABER_SFX_CLIP_COUNT];
};

static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
      (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void set_reason(char *reason, size_t reason_size, const char *text) {
  if (!reason || !reason_size) return;
  snprintf(reason, reason_size, "%s", text ? text : "invalid sidecar");
}

static bool reject(MmxSaberSfx **out, char *reason, size_t reason_size,
                   const char *text) {
  if (out) *out = NULL;
  set_reason(reason, reason_size, text);
  return false;
}

void MmxSaberSfxFree(MmxSaberSfx *sfx) {
  if (!sfx) return;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i)
    free((void *)sfx->clips[i].samples);
  free(sfx);
}

bool MmxSaberSfxParse(const uint8_t *data, size_t size,
                      MmxSaberSfx **out, char *reason, size_t reason_size) {
  uint16_t header_bytes, record_bytes, clip_count;
  uint32_t file_bytes, total_pcm_bytes;
  size_t table_bytes, cursor;
  uint32_t total = 0;
  MmxSaberSfxClip parsed[MMX_SABER_SFX_CLIP_COUNT];
  const uint8_t *source[MMX_SABER_SFX_CLIP_COUNT];
  MmxSaberSfx *published;

  if (out) *out = NULL;
  if (reason && reason_size) reason[0] = '\0';
  if (!out || !data) return reject(out, reason, reason_size, "null input");
  if (size < MMX_SABER_SFX_HEADER_BYTES)
    return reject(out, reason, reason_size, "truncated header");
  if (memcmp(data, kMmxSaberSfxMagic, sizeof(kMmxSaberSfxMagic)))
    return reject(out, reason, reason_size, "bad magic");
  if (get16(data + 8) != MMX_SABER_SFX_VERSION)
    return reject(out, reason, reason_size, "unsupported version");
  header_bytes = get16(data + 10);
  clip_count = get16(data + 12);
  record_bytes = get16(data + 14);
  file_bytes = get32(data + 16);
  total_pcm_bytes = get32(data + 20);
  if (header_bytes != MMX_SABER_SFX_HEADER_BYTES)
    return reject(out, reason, reason_size, "bad header size");
  if (clip_count != MMX_SABER_SFX_CLIP_COUNT)
    return reject(out, reason, reason_size, "clip count");
  if (record_bytes != MMX_SABER_SFX_RECORD_BYTES)
    return reject(out, reason, reason_size, "record size");
  if ((size_t)file_bytes != size)
    return reject(out, reason, reason_size, "file size");
  if (total_pcm_bytes > MMX_SABER_SFX_MAX_PCM_BYTES)
    return reject(out, reason, reason_size, "aggregate PCM cap");
  if ((uint64_t)header_bytes +
          (uint64_t)clip_count * (uint64_t)record_bytes > SIZE_MAX)
    return reject(out, reason, reason_size, "table overflow");
  table_bytes = (size_t)header_bytes +
      (size_t)clip_count * (size_t)record_bytes;
  if (table_bytes > size)
    return reject(out, reason, reason_size, "truncated records");

  memset(parsed, 0, sizeof(parsed));
  memset(source, 0, sizeof(source));
  cursor = table_bytes;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    const uint8_t *record = data + MMX_SABER_SFX_HEADER_BYTES +
        (size_t)i * MMX_SABER_SFX_RECORD_BYTES;
    uint64_t expected_bytes;
    parsed[i].channels = get32(record + 0);
    parsed[i].sample_rate = get32(record + 4);
    parsed[i].frame_count = get32(record + 8);
    parsed[i].byte_length = get32(record + 12);
    if (get32(record + 16) != 0)
      return reject(out, reason, reason_size, "reserved record bytes");
    if (parsed[i].channels != MMX_SABER_SFX_CHANNELS)
      return reject(out, reason, reason_size, "channels/layout");
    if (parsed[i].sample_rate < MMX_SABER_SFX_MIN_SAMPLE_RATE ||
        parsed[i].sample_rate > MMX_SABER_SFX_MAX_SAMPLE_RATE)
      return reject(out, reason, reason_size, "sample rate/layout");
    if (!parsed[i].frame_count)
      return reject(out, reason, reason_size, "empty clip");
    expected_bytes = (uint64_t)parsed[i].frame_count *
        (uint64_t)parsed[i].channels * sizeof(int16_t);
    if (expected_bytes > UINT32_MAX ||
        parsed[i].byte_length != (uint32_t)expected_bytes)
      return reject(out, reason, reason_size, "PCM length overflow");
    if ((uint64_t)total + parsed[i].byte_length >
        MMX_SABER_SFX_MAX_PCM_BYTES)
      return reject(out, reason, reason_size, "aggregate PCM cap");
    if (cursor > size || parsed[i].byte_length > size - cursor)
      return reject(out, reason, reason_size, "truncated PCM");
    source[i] = data + cursor;
    cursor += parsed[i].byte_length;
    total += parsed[i].byte_length;
  }
  if (cursor != size)
    return reject(out, reason, reason_size, "trailing PCM");
  if (total != total_pcm_bytes)
    return reject(out, reason, reason_size, "aggregate PCM length");

  published = (MmxSaberSfx *)calloc(1, sizeof(*published));
  if (!published)
    return reject(out, reason, reason_size, "out of memory");
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    const size_t sample_count = parsed[i].byte_length / sizeof(int16_t);
    int16_t *samples = (int16_t *)malloc(parsed[i].byte_length);
    if (!samples) {
      MmxSaberSfxFree(published);
      return reject(out, reason, reason_size, "out of memory");
    }
    for (size_t j = 0; j < sample_count; ++j)
      samples[j] = (int16_t)get16(source[i] + j * sizeof(int16_t));
    published->clips[i] = parsed[i];
    published->clips[i].samples = samples;
  }
  *out = published;
  return true;
}

MmxSaberSfx *MmxSaberSfxLoadFile(const char *path, char *reason,
                                 size_t reason_size) {
  FILE *file = NULL;
  long end;
  size_t size;
  uint8_t *data = NULL;
  MmxSaberSfx *sfx = NULL;
  int read_ok;

  if (reason && reason_size) reason[0] = '\0';
  file = path ? fopen(path, "rb") : NULL;
  if (!file) {
    set_reason(reason, reason_size, "open failed");
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0 ||
      (uint64_t)end > (uint64_t)MMX_SABER_SFX_HEADER_BYTES +
          (uint64_t)MMX_SABER_SFX_CLIP_COUNT * MMX_SABER_SFX_RECORD_BYTES +
          MMX_SABER_SFX_MAX_PCM_BYTES ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    set_reason(reason, reason_size, "file bounds");
    return NULL;
  }
  size = (size_t)end;
  data = (uint8_t *)malloc(size ? size : 1);
  read_ok = data && (!size || fread(data, 1, size, file) == size) &&
      !ferror(file);
  if (fclose(file) != 0) read_ok = 0;
  if (!read_ok) {
    free(data);
    set_reason(reason, reason_size, "read failed");
    return NULL;
  }
  if (!MmxSaberSfxParse(data, size, &sfx, reason, reason_size)) {
    free(data);
    return NULL;
  }
  free(data);
  return sfx;
}

const MmxSaberSfxClip *MmxSaberSfxClipAt(const MmxSaberSfx *sfx,
                                         unsigned clip_id) {
  if (!sfx || clip_id >= MMX_SABER_SFX_CLIP_COUNT) return NULL;
  return &sfx->clips[clip_id];
}

typedef struct MmxSaberSfxRuntime {
  MmxSaberSfx *sfx;
  MmxSaberSfxHost host;
  MmxSaberSfxWarningFn warning;
  void *warning_context;
  int registered[MMX_SABER_SFX_CLIP_COUNT];
  int volume_percent;
  unsigned last_clip;
  bool registered_ready;
  bool warning_reported;
} MmxSaberSfxRuntime;

static MmxSaberSfxRuntime g_mmx_saber_sfx_runtime = {
  .volume_percent = 50,
  .last_clip = MMX_SABER_SFX_CLIP_COUNT
};

static void saber_sfx_warn_once(const char *reason) {
  if (g_mmx_saber_sfx_runtime.warning_reported) return;
  g_mmx_saber_sfx_runtime.warning_reported = true;
  if (g_mmx_saber_sfx_runtime.warning)
    g_mmx_saber_sfx_runtime.warning(
        reason && reason[0] ? reason : "invalid sidecar",
        g_mmx_saber_sfx_runtime.warning_context);
}

static void saber_sfx_unregister_clips(void) {
  if (g_mmx_saber_sfx_runtime.host.unregister_clip) {
    for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
      if (g_mmx_saber_sfx_runtime.registered[i])
        g_mmx_saber_sfx_runtime.host.unregister_clip(
            g_mmx_saber_sfx_runtime.registered[i],
            g_mmx_saber_sfx_runtime.host.context);
      g_mmx_saber_sfx_runtime.registered[i] = 0;
    }
  } else {
    memset(g_mmx_saber_sfx_runtime.registered, 0,
           sizeof(g_mmx_saber_sfx_runtime.registered));
  }
  g_mmx_saber_sfx_runtime.registered_ready = false;
}

void MmxSaberSfxSetHost(const MmxSaberSfxHost *host) {
  saber_sfx_unregister_clips();
  memset(&g_mmx_saber_sfx_runtime.host, 0,
         sizeof(g_mmx_saber_sfx_runtime.host));
  if (host) g_mmx_saber_sfx_runtime.host = *host;
}

void MmxSaberSfxSetWarningCallback(MmxSaberSfxWarningFn callback,
                                   void *context) {
  g_mmx_saber_sfx_runtime.warning = callback;
  g_mmx_saber_sfx_runtime.warning_context = context;
  g_mmx_saber_sfx_runtime.warning_reported = false;
}

void MmxSaberSfxSetVolume(int volume_percent) {
  if (volume_percent < 0) volume_percent = 0;
  if (volume_percent > 200) volume_percent = 200;
  g_mmx_saber_sfx_runtime.volume_percent = volume_percent;
}

int MmxSaberSfxVolume(void) {
  return g_mmx_saber_sfx_runtime.volume_percent;
}

void MmxSaberSfxResetRuntime(void) {
  saber_sfx_unregister_clips();
  MmxSaberSfxFree(g_mmx_saber_sfx_runtime.sfx);
  g_mmx_saber_sfx_runtime.sfx = NULL;
  g_mmx_saber_sfx_runtime.last_clip = MMX_SABER_SFX_CLIP_COUNT;
  g_mmx_saber_sfx_runtime.warning_reported = false;
}

bool MmxSaberSfxLoadRuntime(const char *path, char *reason,
                            size_t reason_size) {
  MmxSaberSfx *sfx;
  MmxSaberSfxResetRuntime();
  sfx = MmxSaberSfxLoadFile(path, reason, reason_size);
  if (!sfx) {
    saber_sfx_warn_once(reason && reason[0] ? reason : "open failed");
    return false;
  }
  g_mmx_saber_sfx_runtime.sfx = sfx;
  return true;
}

bool MmxSaberSfxLoaded(void) {
  return g_mmx_saber_sfx_runtime.sfx != NULL;
}

static bool saber_sfx_register_clips(void) {
  int staged[MMX_SABER_SFX_CLIP_COUNT] = {0};
  if (g_mmx_saber_sfx_runtime.registered_ready) return true;
  if (!g_mmx_saber_sfx_runtime.sfx) return false;
  if (!g_mmx_saber_sfx_runtime.host.register_pcm) {
    saber_sfx_warn_once("host clip registration is unavailable");
    return false;
  }
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    const MmxSaberSfxClip *clip =
        MmxSaberSfxClipAt(g_mmx_saber_sfx_runtime.sfx, i);
    if (!clip || !(staged[i] = g_mmx_saber_sfx_runtime.host.register_pcm(
                       clip->samples, clip->frame_count, clip->sample_rate,
                       clip->channels, g_mmx_saber_sfx_runtime.host.context))) {
      for (unsigned j = 0; j < i; ++j) {
        if (staged[j] && g_mmx_saber_sfx_runtime.host.unregister_clip)
          g_mmx_saber_sfx_runtime.host.unregister_clip(
              staged[j], g_mmx_saber_sfx_runtime.host.context);
      }
      saber_sfx_warn_once("host clip registration failed");
      return false;
    }
  }
  memcpy(g_mmx_saber_sfx_runtime.registered, staged, sizeof(staged));
  g_mmx_saber_sfx_runtime.registered_ready = true;
  return true;
}

void MmxSaberSfxPlay(unsigned clip_id) {
  if (clip_id >= MMX_SABER_SFX_CLIP_COUNT) return;
  g_mmx_saber_sfx_runtime.last_clip = clip_id;
  if (!g_mmx_saber_sfx_runtime.sfx) return;
  if (g_mmx_saber_sfx_runtime.host.suppressed &&
      g_mmx_saber_sfx_runtime.host.suppressed(
          g_mmx_saber_sfx_runtime.host.context))
    return;
  if (!saber_sfx_register_clips()) return;
  if (g_mmx_saber_sfx_runtime.host.play)
    (void)g_mmx_saber_sfx_runtime.host.play(
        g_mmx_saber_sfx_runtime.registered[clip_id],
        g_mmx_saber_sfx_runtime.volume_percent,
        g_mmx_saber_sfx_runtime.host.context);
}

unsigned MmxSaberSfxAttackClip(MmxSaberSfxAttackCue cue) {
  static const unsigned kAttackClips[MMX_SABER_SFX_ATTACK_COUNT] = {
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_2,
    MMX_SABER_SFX_CLIP_SABER_3,
    MMX_SABER_SFX_CLIP_SABER_3,
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_2
  };
  return cue < MMX_SABER_SFX_ATTACK_COUNT ? kAttackClips[cue] :
      MMX_SABER_SFX_CLIP_COUNT;
}

void MmxSaberSfxPlayForAttack(MmxSaberSfxAttackCue cue) {
  const unsigned clip = MmxSaberSfxAttackClip(cue);
  if (clip < MMX_SABER_SFX_CLIP_COUNT) MmxSaberSfxPlay(clip);
}

unsigned MmxSaberSfxLastClip(void) {
  return g_mmx_saber_sfx_runtime.last_clip;
}

unsigned MmxSaberSfxRegisteredClipCount(void) {
  unsigned count = 0;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i)
    count += g_mmx_saber_sfx_runtime.registered[i] != 0;
  return count;
}
