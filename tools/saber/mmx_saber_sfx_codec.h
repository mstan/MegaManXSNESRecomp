#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Converter-only S-DSP primitives.  The PCM passed to the BRR routines is in
 * the S-DSP's signed 15-bit sample domain (-16384..16383); the codec returns
 * the same domain as signed 16-bit storage. */
typedef struct MmxSaberSfxBrrStats {
  size_t block_count;
  unsigned filter_counts[4];
  unsigned shift_counts[13];
} MmxSaberSfxBrrStats;

bool MmxSaberSfxBrrEncode(const int16_t *samples, size_t frame_count,
                          uint8_t **out_brr, size_t *out_bytes,
                          MmxSaberSfxBrrStats *stats);
bool MmxSaberSfxBrrDecode(const uint8_t *brr, size_t brr_bytes,
                          int16_t *out_samples, size_t frame_count);

/* The published S-DSP table is indexed by the 9-bit phase in the hardware's
 * 4-tap interpolator. */
uint16_t MmxSaberSfxGaussianTableValue(unsigned index);
int16_t MmxSaberSfxGaussianInterpolate(const int16_t taps[4],
                                       unsigned phase);
bool MmxSaberSfxGaussianRender(const int16_t *samples, size_t frame_count,
                               int16_t **out_samples, size_t *out_frames);

/* Deterministic windowed-sinc resampler.  It produces rounded-nearest mono
 * 16 kHz PCM and normalizes edge kernels to avoid endpoint gain changes. */
bool MmxSaberSfxResampleMono16k(const int16_t *samples, size_t frame_count,
                                uint32_t sample_rate, int16_t **out_samples,
                                size_t *out_frames);

