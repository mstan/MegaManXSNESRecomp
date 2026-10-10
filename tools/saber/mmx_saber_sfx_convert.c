#include "mmx_saber_sfx.h"
#include "mmx_saber_sfx_codec.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_vorbis is deliberately a source on this target only.  The game target
 * sees the sidecar parser, never this declaration or the decoder object. */
extern int stb_vorbis_decode_memory(const unsigned char *mem, int len,
                                    int *channels, int *sample_rate,
                                    short **output);

_Static_assert(sizeof(short) == sizeof(int16_t),
               "stb_vorbis short output must be 16-bit");

enum { MAX_OGG_BYTES = 64 * 1024 * 1024 };

typedef struct DecodedClip {
  int16_t *samples;
  uint32_t channels;
  uint32_t sample_rate;
  uint32_t frame_count;
  uint32_t byte_length;
  MmxSaberSfxBrrStats brr_stats;
  uint32_t source_rate;
  size_t source_frames;
} DecodedClip;

static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}
static void put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static void free_clips(DecodedClip clips[MMX_SABER_SFX_CLIP_COUNT]) {
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    free(clips[i].samples);
    clips[i].samples = NULL;
  }
}

static int read_file(const char *path, uint8_t **out, size_t *out_size) {
  FILE *file = NULL;
  long end;
  size_t size;
  uint8_t *data;
  if (!path || !out || !out_size) return 0;
  *out = NULL;
  *out_size = 0;
  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (end = ftell(file)) < 0 || end > MAX_OGG_BYTES ||
      fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
    return 0;
  }
  size = (size_t)end;
  data = (uint8_t *)malloc(size ? size : 1);
  if (!data || (size && fread(data, 1, size, file) != size) ||
      ferror(file)) {
    free(data);
    fclose(file);
    return 0;
  }
  if (fclose(file) != 0) {
    free(data);
    return 0;
  }
  *out = data;
  *out_size = size;
  return 1;
}

static int path_join(char *out, size_t cap, const char *dir,
                     const char *leaf) {
  int written;
  size_t dir_len;
  if (!out || !cap || !dir || !leaf) return 0;
  dir_len = strlen(dir);
  if (dir_len && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\'))
    written = snprintf(out, cap, "%s%s", dir, leaf);
  else
    written = snprintf(out, cap, "%s/%s", dir, leaf);
  return written >= 0 && (size_t)written < cap;
}

static int16_t downmix_sample(const short *decoded, size_t frame,
                              unsigned channels) {
  if (channels == 1) return decoded[frame];
  /* The converter accepts stb_vorbis' mono/stereo output only.  Symmetric
   * integer averaging keeps the downmix independent of the host FPU. */
  return (int16_t)(((int32_t)decoded[frame * 2] +
                    (int32_t)decoded[frame * 2 + 1]) / 2);
}

static int32_t to_dsp15(int16_t sample) {
  int32_t value = sample;
  if (value >= 0) return value / 2;
  return -(((-value) + 1) / 2);
}

static int decode_clip(const char *path, DecodedClip *clip) {
  uint8_t *encoded = NULL;
  size_t encoded_size = 0;
  int channels = 0, sample_rate = 0, frames;
  short *decoded = NULL;
  int16_t *mono = NULL, *resampled = NULL, *dsp_input = NULL;
  int16_t *brr_decoded = NULL, *rendered = NULL;
  uint8_t *brr = NULL;
  size_t mono_bytes, resampled_frames, brr_bytes, brr_frames, rendered_frames;
  MmxSaberSfxBrrStats stats;

  if (!clip || !read_file(path, &encoded, &encoded_size) ||
      encoded_size > INT_MAX) {
    free(encoded);
    return 0;
  }
  frames = stb_vorbis_decode_memory(encoded, (int)encoded_size, &channels,
                                    &sample_rate, &decoded);
  free(encoded);
  if (frames <= 0 || (channels != 1 && channels != 2) ||
      sample_rate < 8000 || sample_rate > 192000 || !decoded) {
    free(decoded);
    return 0;
  }
  if ((size_t)frames > SIZE_MAX / sizeof(*mono)) {
    free(decoded);
    return 0;
  }
  mono_bytes = (size_t)frames * sizeof(*mono);
  mono = (int16_t *)malloc(mono_bytes);
  if (!mono) goto fail;
  for (size_t i = 0; i < (size_t)frames; ++i)
    mono[i] = downmix_sample(decoded, i, (unsigned)channels);
  free(decoded);
  decoded = NULL;

  if (!MmxSaberSfxResampleMono16k(mono, (size_t)frames,
                                  (uint32_t)sample_rate, &resampled,
                                  &resampled_frames)) goto fail;
  free(mono);
  mono = NULL;
  if (resampled_frames > SIZE_MAX / sizeof(*dsp_input)) goto fail;
  dsp_input = (int16_t *)malloc(resampled_frames * sizeof(*dsp_input));
  if (!dsp_input) goto fail;
  for (size_t i = 0; i < resampled_frames; ++i)
    dsp_input[i] = (int16_t)to_dsp15(resampled[i]);

  if (!MmxSaberSfxBrrEncode(dsp_input, resampled_frames, &brr, &brr_bytes,
                            &stats)) goto fail;
  free(dsp_input);
  dsp_input = NULL;
  if (brr_bytes % 9 != 0) goto fail;
  brr_frames = (brr_bytes / 9) * 16;
  if (brr_frames > SIZE_MAX / sizeof(*brr_decoded)) goto fail;
  brr_decoded = (int16_t *)malloc(brr_frames * sizeof(*brr_decoded));
  if (!brr_decoded || !MmxSaberSfxBrrDecode(brr, brr_bytes, brr_decoded,
                                             brr_frames)) goto fail;
  if (!MmxSaberSfxGaussianRender(brr_decoded, resampled_frames, &rendered,
                                 &rendered_frames)) goto fail;
  if (rendered_frames > UINT32_MAX ||
      rendered_frames > UINT32_MAX / sizeof(int16_t)) goto fail;

  free(resampled);
  free(brr_decoded);
  free(brr);
  memset(clip, 0, sizeof(*clip));
  clip->samples = rendered;
  clip->channels = MMX_SABER_SFX_CHANNELS;
  clip->sample_rate = MMX_SABER_SFX_NATIVE_SAMPLE_RATE;
  clip->frame_count = (uint32_t)rendered_frames;
  clip->byte_length = (uint32_t)(rendered_frames * sizeof(int16_t));
  clip->brr_stats = stats;
  clip->source_rate = (uint32_t)sample_rate;
  clip->source_frames = (size_t)frames;
  return 1;

fail:
  free(decoded);
  free(mono);
  free(resampled);
  free(dsp_input);
  free(brr_decoded);
  free(rendered);
  free(brr);
  return 0;
}

static int build_sidecar(const DecodedClip clips[MMX_SABER_SFX_CLIP_COUNT],
                         uint8_t **out, size_t *out_size) {
  uint64_t total = 0;
  size_t size, cursor;
  uint8_t *data;
  if (!clips || !out || !out_size) return 0;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    if (clips[i].channels != MMX_SABER_SFX_CHANNELS ||
        clips[i].sample_rate != MMX_SABER_SFX_NATIVE_SAMPLE_RATE ||
        !clips[i].frame_count) return 0;
    total += clips[i].byte_length;
    if (total > MMX_SABER_SFX_MAX_PCM_BYTES) return 0;
  }
  size = MMX_SABER_SFX_HEADER_BYTES +
      (size_t)MMX_SABER_SFX_CLIP_COUNT * MMX_SABER_SFX_RECORD_BYTES +
      (size_t)total;
  if (size > UINT32_MAX) return 0;
  data = (uint8_t *)calloc(size, 1);
  if (!data) return 0;
  memcpy(data, "MMXSFX2", 7);
  put16(data + 8, MMX_SABER_SFX_VERSION);
  put16(data + 10, MMX_SABER_SFX_HEADER_BYTES);
  put16(data + 12, MMX_SABER_SFX_CLIP_COUNT);
  put16(data + 14, MMX_SABER_SFX_RECORD_BYTES);
  put32(data + 16, (uint32_t)size);
  put32(data + 20, (uint32_t)total);
  cursor = MMX_SABER_SFX_HEADER_BYTES +
      (size_t)MMX_SABER_SFX_CLIP_COUNT * MMX_SABER_SFX_RECORD_BYTES;
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    uint8_t *record = data + MMX_SABER_SFX_HEADER_BYTES +
        (size_t)i * MMX_SABER_SFX_RECORD_BYTES;
    put32(record + 0, clips[i].channels);
    put32(record + 4, clips[i].sample_rate);
    put32(record + 8, clips[i].frame_count);
    put32(record + 12, clips[i].byte_length);
    for (uint32_t j = 0; j < clips[i].byte_length / sizeof(int16_t); ++j)
      put16(data + cursor + (size_t)j * sizeof(int16_t),
            (uint16_t)clips[i].samples[j]);
    cursor += clips[i].byte_length;
  }
  *out = data;
  *out_size = size;
  return true;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
  FILE *file;
  int ok;
  if (!path || !data) return 0;
  file = fopen(path, "wb");
  if (!file) return 0;
  ok = fwrite(data, 1, size, file) == size;
  if (fclose(file) != 0) ok = 0;
  return ok;
}

static void usage(const char *program) {
  fprintf(stderr, "usage: %s --source-dir DIR --out FILE\n", program);
}

int main(int argc, char **argv) {
  const char *source_dir = NULL, *output_path = NULL;
  const char *names[MMX_SABER_SFX_CLIP_COUNT] = {
    "saber_1.ogg", "saber_2.ogg", "saber_3.ogg"
  };
  DecodedClip clips[MMX_SABER_SFX_CLIP_COUNT] = {0};
  uint8_t *sidecar = NULL;
  size_t sidecar_size = 0;
  char path[4096];
  int ok = 0;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--source-dir") && i + 1 < argc)
      source_dir = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc)
      output_path = argv[++i];
    else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!source_dir || !output_path) {
    usage(argv[0]);
    return 2;
  }
  for (unsigned i = 0; i < MMX_SABER_SFX_CLIP_COUNT; ++i) {
    if (!path_join(path, sizeof(path), source_dir, names[i]) ||
        !decode_clip(path, &clips[i])) {
      fprintf(stderr, "mmx_saber_sfx_convert: failed to decode clip %u\n",
              i + 1);
      goto done;
    }
    fprintf(stderr,
        "mmx_saber_sfx_convert: clip %u input=%zu@%uHz, output=%u@%uHz, "
        "BRR blocks=%zu filters=%u/%u/%u/%u\n", i + 1,
        clips[i].source_frames, clips[i].source_rate, clips[i].frame_count,
        clips[i].sample_rate, clips[i].brr_stats.block_count,
        clips[i].brr_stats.filter_counts[0], clips[i].brr_stats.filter_counts[1],
        clips[i].brr_stats.filter_counts[2], clips[i].brr_stats.filter_counts[3]);
  }
  if (!build_sidecar(clips, &sidecar, &sidecar_size) ||
      !write_file(output_path, sidecar, sidecar_size)) {
    fprintf(stderr, "mmx_saber_sfx_convert: failed to write output\n");
    goto done;
  }
  ok = 1;
done:
  free(sidecar);
  free_clips(clips);
  return ok ? 0 : 1;
}
