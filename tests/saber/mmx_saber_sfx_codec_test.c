#include "mmx_saber_sfx_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int condition, const char *what) {
  if (!condition) fprintf(stderr, "FAIL: %s\n", what);
  return condition;
}

static int brr_round_trip_tests(void) {
  int16_t signal[32];
  int16_t *decoded = NULL;
  uint8_t *brr = NULL;
  size_t brr_bytes = 0;
  MmxSaberSfxBrrStats stats;

  for (unsigned i = 0; i < 16; ++i)
    signal[i] = (int16_t)((int)i * 700 - 5000);
  for (unsigned i = 16; i < 32; ++i)
    signal[i] = (int16_t)(4000 + (int)(i - 16) * 20);
  if (!check(MmxSaberSfxBrrEncode(signal, 32, &brr, &brr_bytes, &stats),
             "BRR encoding succeeds"))
    return 0;
  if (!check(brr_bytes == 18 && stats.block_count == 2,
             "BRR uses two nine-byte blocks") ||
      !check((brr[0] & 0x0c) == 0,
             "first BRR block is forced to filter 0") ||
      !check(stats.filter_counts[1] + stats.filter_counts[2] +
                 stats.filter_counts[3] > 0,
             "later BRR blocks select predictive filters")) {
    free(brr);
    return 0;
  }
  decoded = (int16_t *)malloc(32 * sizeof(*decoded));
  if (!check(decoded != NULL &&
                 MmxSaberSfxBrrDecode(brr, brr_bytes, decoded, 32),
             "BRR decoding succeeds")) {
    free(decoded);
    free(brr);
    return 0;
  }
  for (unsigned i = 0; i < 32; ++i) {
    if (!check(abs((int)decoded[i] - (int)signal[i]) < 2500,
               "BRR round-trip error stays bounded")) {
      free(decoded);
      free(brr);
      return 0;
    }
  }
  free(decoded);
  free(brr);

  {
    uint8_t known[18] = {0};
    int16_t output[32];
    known[0] = 0xc0; /* shift 12, filter 0 */
    for (unsigned i = 1; i < 8; ++i) known[i] = 0x77;
    known[8] = 0x87; /* end of block: -8, +7 */
    known[9] = 0xcc; /* shift 12, filter 3 */
    for (unsigned i = 10; i < 18; ++i) known[i] = 0x77;
    if (!check(MmxSaberSfxBrrDecode(known, sizeof(known), output, 32),
               "known BRR stream decodes") ||
        !check(output[0] == 14336, "BRR shift decode matches hardware") ||
        !check(output[14] == -16384 && output[15] == 14336,
               "BRR clamp behavior matches hardware") ||
        !check(output[16] == -1,
               "BRR 15-bit wrap behavior matches hardware"))
      return 0;
  }
  puts("PASS Saber SFX BRR round trip/filter selection and S-DSP clamp-wrap");
  return 1;
}

static int gaussian_tests(void) {
  int16_t input[] = {1000, 2000, 3000, 4000};
  int16_t *output = NULL;
  size_t output_frames = 0;
  if (!check(MmxSaberSfxGaussianTableValue(0) == 0x000,
             "Gaussian table starts at zero") ||
      !check(MmxSaberSfxGaussianTableValue(16) == 0x001,
             "Gaussian table spot value 16") ||
      !check(MmxSaberSfxGaussianTableValue(0x100) == 0x176,
             "Gaussian table midpoint spot value") ||
      !check(MmxSaberSfxGaussianTableValue(0x1ff) == 0x519,
             "Gaussian table final spot value") ||
      !check(MmxSaberSfxGaussianRender(input, 4, &output, &output_frames),
             "Gaussian rendering succeeds") ||
      !check(output_frames == 8, "Gaussian render doubles placement frames")) {
    free(output);
    return 0;
  }
  free(output);
  puts("PASS Saber SFX Gaussian table spot values and 16k-to-32k placement");
  return 1;
}

static int resampler_tests(void) {
  int16_t input[9];
  int16_t *a = NULL, *b = NULL;
  size_t a_frames = 0, b_frames = 0;
  for (unsigned i = 0; i < 9; ++i) input[i] = (int16_t)(i * 1000);
  if (!check(MmxSaberSfxResampleMono16k(input, 9, 48000, &a, &a_frames),
             "resampler succeeds") ||
      !check(MmxSaberSfxResampleMono16k(input, 9, 48000, &b, &b_frames),
             "resampler repeats deterministically") ||
      !check(a_frames == 3 && b_frames == a_frames,
             "resampler output frame count") ||
      !check(!memcmp(a, b, a_frames * sizeof(*a)),
             "resampler output bytes are deterministic") ||
      !check(a[0] == 299 && a[1] == 2914 && a[2] == 6129,
             "resampler spot values match the reference")) {
    free(a);
    free(b);
    return 0;
  }
  free(a);
  free(b);
  puts("PASS Saber SFX windowed-sinc resampler is deterministic at 16 kHz");
  return 1;
}

int main(void) {
  if (!brr_round_trip_tests() || !gaussian_tests() || !resampler_tests())
    return 1;
  return 0;
}
