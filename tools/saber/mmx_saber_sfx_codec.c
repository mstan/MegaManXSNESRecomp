#include "mmx_saber_sfx_codec.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  MMX_SABER_SFX_BRR_BLOCK_BYTES = 9,
  MMX_SABER_SFX_BRR_SAMPLES = 16,
  MMX_SABER_SFX_GAUSSIAN_ENTRIES = 512,
  MMX_SABER_SFX_RESAMPLE_RADIUS = 16
};

/* Published S-DSP Gaussian ROM, indexed from phase 0 through phase 0x1ff.
 * The interpolation uses the low 8 phase bits and the mirrored second half
 * exactly as the hardware's four-tap path does. */
static const uint16_t kMmxSaberGaussian[MMX_SABER_SFX_GAUSSIAN_ENTRIES] = {
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
  0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
  0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00A, 0x00A, 0x00A,
  0x00B, 0x00B, 0x00B, 0x00C, 0x00C, 0x00D, 0x00D, 0x00E, 0x00E, 0x00F, 0x00F, 0x00F, 0x010, 0x010, 0x011, 0x011,
  0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01A, 0x01B, 0x01B,
  0x01C, 0x01D, 0x01D, 0x01E, 0x01F, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
  0x029, 0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
  0x03A, 0x03B, 0x03C, 0x03D, 0x03E, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04A, 0x04C, 0x04D,
  0x04E, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05A, 0x05C, 0x05E, 0x05F, 0x061, 0x063, 0x064, 0x066,
  0x068, 0x06A, 0x06B, 0x06D, 0x06F, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07A, 0x07C, 0x07E, 0x080, 0x082, 0x084,
  0x086, 0x089, 0x08B, 0x08D, 0x08F, 0x091, 0x093, 0x096, 0x098, 0x09A, 0x09C, 0x09F, 0x0A1, 0x0A3, 0x0A6, 0x0A8,
  0x0AB, 0x0AD, 0x0AF, 0x0B2, 0x0B4, 0x0B7, 0x0BA, 0x0BC, 0x0BF, 0x0C1, 0x0C4, 0x0C7, 0x0C9, 0x0CC, 0x0CF, 0x0D2,
  0x0D4, 0x0D7, 0x0DA, 0x0DD, 0x0E0, 0x0E3, 0x0E6, 0x0E9, 0x0EC, 0x0EF, 0x0F2, 0x0F5, 0x0F8, 0x0FB, 0x0FE, 0x101,
  0x104, 0x107, 0x10B, 0x10E, 0x111, 0x114, 0x118, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C, 0x130, 0x133, 0x137,
  0x13A, 0x13E, 0x141, 0x145, 0x148, 0x14C, 0x150, 0x153, 0x157, 0x15B, 0x15F, 0x162, 0x166, 0x16A, 0x16E, 0x172,
  0x176, 0x17A, 0x17D, 0x181, 0x185, 0x189, 0x18D, 0x191, 0x195, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE, 0x1B2,
  0x1B7, 0x1BB, 0x1BF, 0x1C3, 0x1C8, 0x1CC, 0x1D0, 0x1D5, 0x1D9, 0x1DD, 0x1E2, 0x1E6, 0x1EB, 0x1EF, 0x1F3, 0x1F8,
  0x1FC, 0x201, 0x205, 0x20A, 0x20F, 0x213, 0x218, 0x21C, 0x221, 0x226, 0x22A, 0x22F, 0x233, 0x238, 0x23D, 0x241,
  0x246, 0x24B, 0x250, 0x254, 0x259, 0x25E, 0x263, 0x267, 0x26C, 0x271, 0x276, 0x27B, 0x280, 0x284, 0x289, 0x28E,
  0x293, 0x298, 0x29D, 0x2A2, 0x2A6, 0x2AB, 0x2B0, 0x2B5, 0x2BA, 0x2BF, 0x2C4, 0x2C9, 0x2CE, 0x2D3, 0x2D8, 0x2DC,
  0x2E1, 0x2E6, 0x2EB, 0x2F0, 0x2F5, 0x2FA, 0x2FF, 0x304, 0x309, 0x30E, 0x313, 0x318, 0x31D, 0x322, 0x326, 0x32B,
  0x330, 0x335, 0x33A, 0x33F, 0x344, 0x349, 0x34E, 0x353, 0x357, 0x35C, 0x361, 0x366, 0x36B, 0x370, 0x374, 0x379,
  0x37E, 0x383, 0x388, 0x38C, 0x391, 0x396, 0x39B, 0x39F, 0x3A4, 0x3A9, 0x3AD, 0x3B2, 0x3B7, 0x3BB, 0x3C0, 0x3C5,
  0x3C9, 0x3CE, 0x3D2, 0x3D7, 0x3DC, 0x3E0, 0x3E5, 0x3E9, 0x3ED, 0x3F2, 0x3F6, 0x3FB, 0x3FF, 0x403, 0x408, 0x40C,
  0x410, 0x415, 0x419, 0x41D, 0x421, 0x425, 0x42A, 0x42E, 0x432, 0x436, 0x43A, 0x43E, 0x442, 0x446, 0x44A, 0x44E,
  0x452, 0x455, 0x459, 0x45D, 0x461, 0x465, 0x468, 0x46C, 0x470, 0x473, 0x477, 0x47A, 0x47E, 0x481, 0x485, 0x488,
  0x48C, 0x48F, 0x492, 0x496, 0x499, 0x49C, 0x49F, 0x4A2, 0x4A6, 0x4A9, 0x4AC, 0x4AF, 0x4B2, 0x4B5, 0x4B7, 0x4BA,
  0x4BD, 0x4C0, 0x4C3, 0x4C5, 0x4C8, 0x4CB, 0x4CD, 0x4D0, 0x4D2, 0x4D5, 0x4D7, 0x4D9, 0x4DC, 0x4DE, 0x4E0, 0x4E3,
  0x4E5, 0x4E7, 0x4E9, 0x4EB, 0x4ED, 0x4EF, 0x4F1, 0x4F3, 0x4F5, 0x4F6, 0x4F8, 0x4FA, 0x4FB, 0x4FD, 0x4FF, 0x500,
  0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50A, 0x50B, 0x50C, 0x50D, 0x50E, 0x50F, 0x510, 0x511, 0x511, 0x512,
  0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};

static int32_t arithmetic_shift_right(int32_t value, unsigned bits) {
  if (!bits || value >= 0) return value >> bits;
  return -(((-value) + ((1 << bits) - 1)) >> bits);
}

static int32_t clip_15(int32_t value) {
  uint32_t low;
  if (value < -32768) value = -32768;
  if (value > 32767) value = 32767;
  low = (uint32_t)value & 0x7fffu;
  return (low & 0x4000u) ? (int32_t)low - 0x8000 : (int32_t)low;
}

static int32_t target_15(int32_t value) {
  if (value < -16384) return -16384;
  if (value > 16383) return 16383;
  return value;
}

static int32_t brr_filter(int filter, int32_t old, int32_t older) {
  switch (filter) {
    case 1:
      return old + arithmetic_shift_right(-old, 4);
    case 2:
      return 2 * old + arithmetic_shift_right(3 * -old, 5) - older +
          arithmetic_shift_right(older, 4);
    case 3:
      return 2 * old + arithmetic_shift_right(13 * -old, 6) - older +
          arithmetic_shift_right(3 * older, 4);
    default:
      return 0;
  }
}

static int32_t brr_decode_one(int nibble, unsigned shift, unsigned filter,
                              int32_t old, int32_t older) {
  int32_t sample = nibble;
  if (shift <= 12) {
    sample = shift ? sample << (shift - 1) : arithmetic_shift_right(sample, 1);
  } else {
    sample = sample < 0 ? -2048 : 0;
  }
  sample += brr_filter((int)filter, old, older);
  if (sample < -32768) sample = -32768;
  if (sample > 32767) sample = 32767;
  return clip_15(sample);
}

static int choose_nibble(int32_t target, unsigned shift, unsigned filter,
                         int32_t old, int32_t older, int32_t *decoded) {
  int selected = -8;
  int32_t selected_sample = brr_decode_one(selected, shift, filter, old, older);
  int64_t selected_error = (int64_t)selected_sample - target;
  selected_error *= selected_error;
  for (int nibble = -7; nibble <= 7; ++nibble) {
    int32_t candidate = brr_decode_one(nibble, shift, filter, old, older);
    int64_t error = (int64_t)candidate - target;
    error *= error;
    if (error < selected_error) {
      selected = nibble;
      selected_sample = candidate;
      selected_error = error;
    }
  }
  if (decoded) *decoded = selected_sample;
  return selected;
}

static int candidate_block(const int16_t *samples, size_t frame_count,
                           size_t start, unsigned shift, unsigned filter,
                           int32_t previous, int32_t older_previous,
                           uint8_t nibbles[MMX_SABER_SFX_BRR_SAMPLES],
                           int32_t *out_old, int32_t *out_older,
                           int64_t *out_error) {
  int32_t old = previous, older = older_previous;
  int64_t error = 0;
  for (unsigned i = 0; i < MMX_SABER_SFX_BRR_SAMPLES; ++i) {
    size_t index = start + i;
    int32_t target = target_15(samples[index < frame_count ? index : frame_count - 1]);
    int32_t decoded;
    int nibble = choose_nibble(target, shift, filter, old, older, &decoded);
    int64_t difference = (int64_t)decoded - target;
    nibbles[i] = (uint8_t)nibble & 0x0fu;
    error += difference * difference;
    older = old;
    old = decoded;
  }
  if (out_old) *out_old = old;
  if (out_older) *out_older = older;
  if (out_error) *out_error = error;
  return 1;
}

bool MmxSaberSfxBrrEncode(const int16_t *samples, size_t frame_count,
                          uint8_t **out_brr, size_t *out_bytes,
                          MmxSaberSfxBrrStats *stats) {
  size_t block_count, byte_count;
  uint8_t *brr;
  int32_t old = 0, older = 0;
  MmxSaberSfxBrrStats local_stats;

  if (!samples || !frame_count || !out_brr || !out_bytes) return false;
  if (frame_count > SIZE_MAX - (MMX_SABER_SFX_BRR_SAMPLES - 1)) return false;
  block_count = (frame_count + MMX_SABER_SFX_BRR_SAMPLES - 1) /
      MMX_SABER_SFX_BRR_SAMPLES;
  if (block_count > SIZE_MAX / MMX_SABER_SFX_BRR_BLOCK_BYTES) return false;
  byte_count = block_count * MMX_SABER_SFX_BRR_BLOCK_BYTES;
  brr = (uint8_t *)calloc(byte_count, 1);
  if (!brr) return false;
  memset(&local_stats, 0, sizeof(local_stats));
  local_stats.block_count = block_count;

  for (size_t block = 0; block < block_count; ++block) {
    uint8_t best_nibbles[MMX_SABER_SFX_BRR_SAMPLES] = {0};
    int best_filter = 0, best_shift = 0;
    int32_t best_old = old, best_older = older;
    int64_t best_error = INT64_MAX;
    unsigned first_filter = block == 0 ? 0 : 0;
    unsigned last_filter = block == 0 ? 0 : 3;
    for (unsigned filter = first_filter; filter <= last_filter; ++filter) {
      for (unsigned shift = 0; shift <= 12; ++shift) {
        uint8_t candidate_nibbles[MMX_SABER_SFX_BRR_SAMPLES];
        int32_t candidate_old, candidate_older;
        int64_t error;
        candidate_block(samples, frame_count,
                        block * MMX_SABER_SFX_BRR_SAMPLES, shift, filter,
                        old, older, candidate_nibbles, &candidate_old,
                        &candidate_older, &error);
        if (error < best_error) {
          best_error = error;
          best_filter = (int)filter;
          best_shift = (int)shift;
          best_old = candidate_old;
          best_older = candidate_older;
          memcpy(best_nibbles, candidate_nibbles, sizeof(best_nibbles));
        }
      }
    }
    {
      size_t cursor = block * MMX_SABER_SFX_BRR_BLOCK_BYTES;
      brr[cursor] = (uint8_t)((unsigned)best_shift << 4) |
          (uint8_t)((unsigned)best_filter << 2) |
          (uint8_t)(block + 1 == block_count ? 1 : 0);
      for (unsigned i = 0; i < MMX_SABER_SFX_BRR_SAMPLES / 2; ++i)
        brr[cursor + 1 + i] = (uint8_t)(best_nibbles[i * 2] << 4) |
            best_nibbles[i * 2 + 1];
    }
    ++local_stats.filter_counts[best_filter];
    ++local_stats.shift_counts[best_shift];
    old = best_old;
    older = best_older;
  }
  *out_brr = brr;
  *out_bytes = byte_count;
  if (stats) *stats = local_stats;
  return true;
}

bool MmxSaberSfxBrrDecode(const uint8_t *brr, size_t brr_bytes,
                          int16_t *out_samples, size_t frame_count) {
  size_t block_count;
  size_t written = 0;
  int32_t old = 0, older = 0;
  if (!brr || !out_samples || !brr_bytes ||
      brr_bytes % MMX_SABER_SFX_BRR_BLOCK_BYTES != 0) return false;
  block_count = brr_bytes / MMX_SABER_SFX_BRR_BLOCK_BYTES;
  if (!frame_count || frame_count > block_count * MMX_SABER_SFX_BRR_SAMPLES)
    return false;
  for (size_t block = 0; block < block_count && written < frame_count; ++block) {
    const uint8_t *source = brr + block * MMX_SABER_SFX_BRR_BLOCK_BYTES;
    unsigned shift = source[0] >> 4;
    unsigned filter = (source[0] >> 2) & 3;
    for (unsigned i = 0; i < MMX_SABER_SFX_BRR_SAMPLES && written < frame_count;
         ++i) {
      uint8_t byte = source[1 + i / 2];
      int nibble = (i & 1) ? (byte & 0x0f) : (byte >> 4);
      int32_t decoded;
      if (nibble >= 8) nibble -= 16;
      decoded = brr_decode_one(nibble, shift, filter, old, older);
      out_samples[written++] = (int16_t)decoded;
      older = old;
      old = decoded;
    }
  }
  return written == frame_count;
}

uint16_t MmxSaberSfxGaussianTableValue(unsigned index) {
  return index < MMX_SABER_SFX_GAUSSIAN_ENTRIES ? kMmxSaberGaussian[index] : 0;
}

static int32_t gaussian_product(uint16_t coefficient, int32_t sample) {
  return arithmetic_shift_right((int32_t)coefficient * sample, 10);
}

int16_t MmxSaberSfxGaussianInterpolate(const int16_t taps[4],
                                       unsigned phase) {
  int32_t out;
  uint16_t wrapped;
  if (!taps) return 0;
  phase &= 0xffu;
  out = gaussian_product(kMmxSaberGaussian[0xffu - phase], taps[0]);
  out += gaussian_product(kMmxSaberGaussian[0x1ffu - phase], taps[1]);
  out += gaussian_product(kMmxSaberGaussian[0x100u + phase], taps[2]);
  wrapped = (uint16_t)out;
  out = wrapped >= 0x8000u ? (int32_t)wrapped - 0x10000 : wrapped;
  out += gaussian_product(kMmxSaberGaussian[phase], taps[3]);
  if (out < -32768) out = -32768;
  if (out > 32767) out = 32767;
  return (int16_t)arithmetic_shift_right(out, 1);
}

static int16_t gaussian_edge_sample(const int16_t *samples, size_t frame_count,
                                    int64_t index) {
  if (index < 0) return samples[0];
  if ((uint64_t)index >= frame_count) return samples[frame_count - 1];
  return samples[index];
}

bool MmxSaberSfxGaussianRender(const int16_t *samples, size_t frame_count,
                               int16_t **out_samples, size_t *out_frames) {
  size_t output_frames;
  int16_t *output;
  if (!samples || !frame_count || !out_samples || !out_frames ||
      frame_count > SIZE_MAX / 2) return false;
  output_frames = frame_count * 2;
  output = (int16_t *)malloc(output_frames * sizeof(*output));
  if (!output) return false;
  for (size_t i = 0; i < output_frames; ++i) {
    size_t source_index = i / 2;
    int64_t base = (int64_t)source_index - 1;
    int16_t taps[4] = {
      gaussian_edge_sample(samples, frame_count, base),
      gaussian_edge_sample(samples, frame_count, base + 1),
      gaussian_edge_sample(samples, frame_count, base + 2),
      gaussian_edge_sample(samples, frame_count, base + 3)
    };
    output[i] = MmxSaberSfxGaussianInterpolate(taps, (i & 1) ? 128 : 0);
  }
  *out_samples = output;
  *out_frames = output_frames;
  return true;
}

static int16_t round_to_s16(double value) {
  double rounded = value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5);
  if (rounded < -32768.0) rounded = -32768.0;
  if (rounded > 32767.0) rounded = 32767.0;
  return (int16_t)rounded;
}

bool MmxSaberSfxResampleMono16k(const int16_t *samples, size_t frame_count,
                                uint32_t sample_rate, int16_t **out_samples,
                                size_t *out_frames) {
  const double pi = 3.141592653589793238462643383279502884;
  const unsigned radius = MMX_SABER_SFX_RESAMPLE_RADIUS;
  double ratio, cutoff;
  uint64_t requested;
  size_t output_frames;
  int16_t *output;

  if (!samples || !frame_count || !sample_rate || !out_samples || !out_frames)
    return false;
  requested = ((uint64_t)frame_count * 16000u + sample_rate / 2u) /
      sample_rate;
  if (!requested) requested = 1;
  if (requested > SIZE_MAX || requested > SIZE_MAX / sizeof(*output))
    return false;
  output_frames = (size_t)requested;
  ratio = 16000.0 / (double)sample_rate;
  cutoff = ratio < 1.0 ? ratio : 1.0;
  output = (int16_t *)malloc(output_frames * sizeof(*output));
  if (!output) return false;

  for (size_t i = 0; i < output_frames; ++i) {
    double center = (double)i / ratio;
    int64_t base = (int64_t)floor(center);
    double weight_sum = 0.0;
    double sample_sum = 0.0;
    for (int offset = -(int)radius + 1; offset <= (int)radius; ++offset) {
      int64_t index = base + offset;
      double distance = (double)index - center;
      double normalized = distance / (double)radius;
      double window, sinc, z, weight;
      size_t clamped;
      if (normalized <= -1.0 || normalized >= 1.0) {
        window = 0.0;
      } else {
        window = 0.5 + 0.5 * cos(pi * normalized);
      }
      z = cutoff * distance;
      sinc = fabs(z) < 1.0e-15 ? 1.0 : sin(pi * z) / (pi * z);
      weight = cutoff * sinc * window;
      clamped = index < 0 ? 0 : (index >= (int64_t)frame_count ?
          frame_count - 1 : (size_t)index);
      weight_sum += weight;
      sample_sum += weight * (double)samples[clamped];
    }
    output[i] = round_to_s16(weight_sum ? sample_sum / weight_sum : 0.0);
  }
  *out_samples = output;
  *out_frames = output_frames;
  return true;
}
