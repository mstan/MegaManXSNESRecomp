#include "mmx_saber_assets.h"

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MMX_SABER_HEADER_BYTES 112u
#define MMX_SABER_MAX_FILE_BYTES (64u * 1024u * 1024u)
#define MMX_SABER_WHOLE_SHA_OFFSET 68u
#define MMX_SABER_SHA_BYTES 32u

typedef struct MmxSaberSource {
  uint16_t id;
  uint8_t sha256[32];
} MmxSaberSource;

struct MmxSaberAssets {
  uint16_t source_count;
  uint16_t palette_count;
  uint16_t animation_count;
  uint16_t step_count;
  uint16_t frame_count;
  uint32_t pixel_bytes;
  uint8_t manifest_sha256[32];
  MmxSaberSource *sources;
  uint16_t *palette;
  MmxSaberAnimation *animations;
  MmxSaberStep *steps;
  MmxSaberFrame *frames;
  uint8_t *pixels;
};

static void set_reason(char *reason, size_t reason_size, const char *message) {
  size_t length;
  if (!reason || !reason_size) return;
  if (!message) message = "";
  length = strlen(message);
  if (length >= reason_size) length = reason_size - 1;
  memcpy(reason, message, length);
  reason[length] = '\0';
}

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static int16_t read_i16(const uint8_t *p) {
  return (int16_t)read_u16(p);
}

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool checked_add(size_t a, size_t b, size_t *out) {
  if (a > SIZE_MAX - b) return false;
  *out = a + b;
  return true;
}

static bool checked_mul(size_t a, size_t b, size_t *out) {
  if (a && b > SIZE_MAX / a) return false;
  *out = a * b;
  return true;
}

static bool section_end(size_t start, size_t count, size_t item_size,
                        size_t file_size, size_t *end) {
  size_t bytes;
  return checked_mul(count, item_size, &bytes) &&
      checked_add(start, bytes, end) && *end <= file_size;
}

static bool plane_in_blob(uint32_t offset, uint32_t length, uint32_t pixel_bytes) {
  return offset <= pixel_bytes && length <= pixel_bytes - offset;
}

static bool source_exists(const MmxSaberAssets *assets, uint16_t id) {
  uint16_t i;
  for (i = 0; i < assets->source_count; ++i)
    if (assets->sources[i].id == id) return true;
  return false;
}

static const MmxSaberAnimation *find_animation(const MmxSaberAssets *assets,
                                               uint16_t id) {
  uint16_t i;
  for (i = 0; i < assets->animation_count; ++i)
    if (assets->animations[i].id == id) return &assets->animations[i];
  return NULL;
}

void MmxSaberAssetsFree(MmxSaberAssets *assets) {
  if (!assets) return;
  free(assets->sources);
  free(assets->palette);
  free(assets->animations);
  free(assets->steps);
  free(assets->frames);
  free(assets->pixels);
  free(assets);
}

static MmxSaberAssets *fail(MmxSaberAssets *assets, char *reason,
                            size_t reason_size, const char *message) {
  MmxSaberAssetsFree(assets);
  set_reason(reason, reason_size, message);
  return NULL;
}

MmxSaberAssets *MmxSaberAssetsParse(const uint8_t *data, size_t size,
                                    const uint8_t expected_manifest_sha[32],
                                    char *reason, size_t reason_size) {
  MmxSaberAssets *assets = NULL;
  size_t pos, end, pad, i;
  uint16_t source_count, palette_count, animation_count, step_count, frame_count;
  uint32_t pixel_bytes;
  uint8_t digest[32];
  uint8_t *hash_input;

  set_reason(reason, reason_size, "");
  if (!data) return fail(NULL, reason, reason_size, "null input");
  if (size > MMX_SABER_MAX_FILE_BYTES)
    return fail(NULL, reason, reason_size, "file too large");
  if (size < MMX_SABER_HEADER_BYTES)
    return fail(NULL, reason, reason_size, "header truncated");

  if (memcmp(data, "MMXSABR\0", 8) != 0)
    return fail(NULL, reason, reason_size, "magic mismatch");
  if (read_u16(data + 8) != 1)
    return fail(NULL, reason, reason_size, "version mismatch");
  if (read_u16(data + 10) != MMX_SABER_HEADER_BYTES)
    return fail(NULL, reason, reason_size, "header size mismatch");
  if (read_u32(data + 12) != (uint32_t)size)
    return fail(NULL, reason, reason_size, "file size mismatch");
  if (read_u32(data + 16) != 0 || read_u16(data + 30) != 0)
    return fail(NULL, reason, reason_size, "unknown flags or reserved field");
  for (i = 100; i < MMX_SABER_HEADER_BYTES; ++i)
    if (data[i] != 0)
      return fail(NULL, reason, reason_size, "reserved field nonzero");

  source_count = read_u16(data + 20);
  palette_count = read_u16(data + 22);
  animation_count = read_u16(data + 24);
  step_count = read_u16(data + 26);
  frame_count = read_u16(data + 28);
  pixel_bytes = read_u32(data + 32);
  if (source_count < 1 || source_count > 32 ||
      palette_count < 2 || palette_count > 256 ||
      animation_count < 1 || animation_count > 64 ||
      step_count < 1 || step_count > 4096 ||
      frame_count < 1 || frame_count > 1024)
    return fail(NULL, reason, reason_size, "invalid counts");
  if (pixel_bytes > MMX_SABER_MAX_FILE_BYTES)
    return fail(NULL, reason, reason_size, "pixel blob too large");

  if (expected_manifest_sha &&
      memcmp(data + 36, expected_manifest_sha, MMX_SABER_SHA_BYTES) != 0)
    return fail(NULL, reason, reason_size, "manifest hash mismatch");

  hash_input = (uint8_t *)malloc(size);
  if (!hash_input)
    return fail(NULL, reason, reason_size, "allocation failed");
  memcpy(hash_input, data, size);
  memset(hash_input + MMX_SABER_WHOLE_SHA_OFFSET, 0, MMX_SABER_SHA_BYTES);
  sha256_compute(hash_input, size, digest);
  free(hash_input);
  if (memcmp(data + MMX_SABER_WHOLE_SHA_OFFSET, digest, MMX_SABER_SHA_BYTES) != 0)
    return fail(NULL, reason, reason_size, "whole-file hash mismatch");

  assets = (MmxSaberAssets *)calloc(1, sizeof(*assets));
  if (!assets) return fail(NULL, reason, reason_size, "allocation failed");
  assets->source_count = source_count;
  assets->palette_count = palette_count;
  assets->animation_count = animation_count;
  assets->step_count = step_count;
  assets->frame_count = frame_count;
  assets->pixel_bytes = pixel_bytes;
  memcpy(assets->manifest_sha256, data + 36, MMX_SABER_SHA_BYTES);

  pos = MMX_SABER_HEADER_BYTES;
  if (!section_end(pos, source_count, 36, size, &end))
    return fail(assets, reason, reason_size, "source table truncated or overflow");
  assets->sources = (MmxSaberSource *)calloc(source_count, sizeof(*assets->sources));
  if (!assets->sources) return fail(assets, reason, reason_size, "allocation failed");
  for (i = 0; i < source_count; ++i) {
    const uint8_t *entry = data + pos + i * 36;
    uint16_t id = read_u16(entry);
    if (!id) return fail(assets, reason, reason_size, "source id zero");
    if (read_u16(entry + 2) != 0)
      return fail(assets, reason, reason_size, "source flags nonzero");
    if (i && source_exists(assets, id))
      return fail(assets, reason, reason_size, "duplicate source id");
    assets->sources[i].id = id;
    memcpy(assets->sources[i].sha256, entry + 4, MMX_SABER_SHA_BYTES);
  }
  pos = end;

  if (!section_end(pos, palette_count, 2, size, &end))
    return fail(assets, reason, reason_size, "palette section truncated or overflow");
  pad = (4u - (end & 3u)) & 3u;
  if (!checked_add(end, pad, &end) || end > size)
    return fail(assets, reason, reason_size, "palette padding truncated");
  for (i = pos + (size_t)palette_count * 2; i < end; ++i)
    if (data[i] != 0)
      return fail(assets, reason, reason_size, "palette padding nonzero");
  assets->palette = (uint16_t *)calloc(palette_count, sizeof(*assets->palette));
  if (!assets->palette) return fail(assets, reason, reason_size, "allocation failed");
  for (i = 0; i < palette_count; ++i) {
    uint16_t color = read_u16(data + pos + i * 2);
    if (i == 0 && color != 0)
      return fail(assets, reason, reason_size, "palette zero entry invalid");
    if (color & 0x8000u)
      return fail(assets, reason, reason_size, "palette entry has bit 15");
    assets->palette[i] = color;
  }
  pos = end;

  if (!section_end(pos, animation_count, 16, size, &end))
    return fail(assets, reason, reason_size, "animation table truncated or overflow");
  assets->animations = (MmxSaberAnimation *)calloc(animation_count,
                                                    sizeof(*assets->animations));
  if (!assets->animations) return fail(assets, reason, reason_size, "allocation failed");
  for (i = 0; i < animation_count; ++i) {
    const uint8_t *entry = data + pos + i * 16;
    uint16_t id = read_u16(entry);
    uint16_t first_step = read_u16(entry + 2);
    uint16_t animation_steps = read_u16(entry + 4);
    uint8_t facing_xor = entry[8];
    if (!id) return fail(assets, reason, reason_size, "animation id zero");
    if (i && find_animation(assets, id))
      return fail(assets, reason, reason_size, "duplicate animation id");
    if (entry[9] != 0 || read_u16(entry + 10) != 0 || read_u32(entry + 12) != 0)
      return fail(assets, reason, reason_size, "animation flags or reserved nonzero");
    if (facing_xor > 1)
      return fail(assets, reason, reason_size, "animation facing invalid");
    if (!animation_steps || first_step >= step_count ||
        (uint32_t)first_step + animation_steps > step_count)
      return fail(assets, reason, reason_size, "animation step reference invalid");
    assets->animations[i].id = id;
    assets->animations[i].first_step = first_step;
    assets->animations[i].step_count = animation_steps;
    assets->animations[i].total_ticks = read_u16(entry + 6);
    assets->animations[i].facing_xor = facing_xor;
  }
  pos = end;

  if (!section_end(pos, step_count, 8, size, &end))
    return fail(assets, reason, reason_size, "step table truncated or overflow");
  assets->steps = (MmxSaberStep *)calloc(step_count, sizeof(*assets->steps));
  if (!assets->steps) return fail(assets, reason, reason_size, "allocation failed");
  for (i = 0; i < step_count; ++i) {
    const uint8_t *entry = data + pos + i * 8;
    uint16_t frame_index = read_u16(entry);
    uint16_t duration = read_u16(entry + 2);
    if (read_u32(entry + 4) != 0)
      return fail(assets, reason, reason_size, "step reserved nonzero");
    if (frame_index >= frame_count)
      return fail(assets, reason, reason_size, "step frame reference invalid");
    if (!duration)
      return fail(assets, reason, reason_size, "step duration zero");
    assets->steps[i].frame_index = frame_index;
    assets->steps[i].duration_ticks = duration;
  }
  pos = end;

  for (i = 0; i < animation_count; ++i) {
    size_t j;
    uint32_t total = 0;
    assets->animations[i].steps = assets->steps + assets->animations[i].first_step;
    for (j = 0; j < assets->animations[i].step_count; ++j) {
      total += assets->animations[i].steps[j].duration_ticks;
      if (total > UINT16_MAX)
        return fail(assets, reason, reason_size, "animation total ticks overflow");
    }
    if (total != assets->animations[i].total_ticks)
      return fail(assets, reason, reason_size, "animation total ticks mismatch");
  }

  if (!section_end(pos, frame_count, 40, size, &end))
    return fail(assets, reason, reason_size, "frame table truncated or overflow");
  pos = end;
  if ((size_t)pixel_bytes > size - pos)
    return fail(assets, reason, reason_size, "pixel blob truncated");
  if ((size_t)pixel_bytes != size - pos)
    return fail(assets, reason, reason_size, "trailing bytes");
  for (i = 0; i < pixel_bytes; ++i)
    if (data[pos + i] >= palette_count)
      return fail(assets, reason, reason_size, "pixel index out of range");
  if (pixel_bytes) {
    assets->pixels = (uint8_t *)malloc(pixel_bytes);
    if (!assets->pixels) return fail(assets, reason, reason_size, "allocation failed");
    memcpy(assets->pixels, data + pos, pixel_bytes);
  }

  assets->frames = (MmxSaberFrame *)calloc(frame_count, sizeof(*assets->frames));
  if (!assets->frames) return fail(assets, reason, reason_size, "allocation failed");
  for (i = 0; i < frame_count; ++i) {
    const uint8_t *entry = data + (end - (size_t)frame_count * 40) + i * 40;
    uint32_t body_offset = read_u32(entry);
    uint32_t body_length = read_u32(entry + 4);
    uint16_t body_width = read_u16(entry + 8);
    uint16_t body_height = read_u16(entry + 10);
    uint32_t blade_offset = read_u32(entry + 16);
    uint32_t blade_length = read_u32(entry + 20);
    uint16_t blade_width = read_u16(entry + 24);
    uint16_t blade_height = read_u16(entry + 26);
    uint8_t blade_layer = entry[36];

    if (!body_width || !body_height)
      return fail(assets, reason, reason_size, "body plane missing");
    if (body_width > 256 || body_height > 256 || blade_width > 256 || blade_height > 256)
      return fail(assets, reason, reason_size, "plane dimension invalid");
    if ((uint64_t)body_width * body_height != body_length ||
        !plane_in_blob(body_offset, body_length, pixel_bytes))
      return fail(assets, reason, reason_size,
                  (uint64_t)body_width * body_height != body_length ?
                  "plane length mismatch" : "plane outside pixel blob");
    if (blade_layer > 2)
      return fail(assets, reason, reason_size, "blade layer invalid");
    if (blade_layer == 0) {
      if (blade_offset || blade_length || blade_width || blade_height ||
          read_i16(entry + 28) || read_i16(entry + 30))
        return fail(assets, reason, reason_size, "absent blade plane malformed");
    } else {
      if (!blade_width || !blade_height)
        return fail(assets, reason, reason_size, "blade plane missing");
      if ((uint64_t)blade_width * blade_height != blade_length)
        return fail(assets, reason, reason_size, "plane length mismatch");
      if (!plane_in_blob(blade_offset, blade_length, pixel_bytes))
        return fail(assets, reason, reason_size, "plane outside pixel blob");
    }
    if (entry[37] != 0 || read_u16(entry + 38) != 0)
      return fail(assets, reason, reason_size, "frame flags or reserved nonzero");
    if (!source_exists(assets, read_u16(entry + 32)))
      return fail(assets, reason, reason_size, "frame source reference invalid");

    assets->frames[i].body.pixels = assets->pixels + body_offset;
    assets->frames[i].body.width = body_width;
    assets->frames[i].body.height = body_height;
    assets->frames[i].body.origin_x = read_i16(entry + 12);
    assets->frames[i].body.origin_y = read_i16(entry + 14);
    assets->frames[i].blade.pixels = blade_layer ? assets->pixels + blade_offset : NULL;
    assets->frames[i].blade.width = blade_width;
    assets->frames[i].blade.height = blade_height;
    assets->frames[i].blade.origin_x = read_i16(entry + 28);
    assets->frames[i].blade.origin_y = read_i16(entry + 30);
    assets->frames[i].source_id = read_u16(entry + 32);
    assets->frames[i].source_frame_id = read_u16(entry + 34);
    assets->frames[i].blade_layer = blade_layer;
  }
  return assets;
}

MmxSaberAssets *MmxSaberAssetsLoadFile(const char *path,
                                       const uint8_t expected_manifest_sha[32],
                                       char *reason, size_t reason_size) {
  FILE *file;
  long length;
  size_t size;
  uint8_t *data;
  MmxSaberAssets *assets;

  set_reason(reason, reason_size, "");
  if (!path) return fail(NULL, reason, reason_size, "null file path");
  file = fopen(path, "rb");
  if (!file) return fail(NULL, reason, reason_size, "cannot open file");
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot seek file");
  }
  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot size file");
  }
  if ((unsigned long)length > MMX_SABER_MAX_FILE_BYTES) {
    fclose(file);
    return fail(NULL, reason, reason_size, "file too large");
  }
  size = (size_t)length;
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot seek file");
  }
  data = (uint8_t *)malloc(size ? size : 1);
  if (!data) {
    fclose(file);
    return fail(NULL, reason, reason_size, "allocation failed");
  }
  if (size && fread(data, 1, size, file) != size) {
    free(data);
    fclose(file);
    return fail(NULL, reason, reason_size, "cannot read file");
  }
  fclose(file);
  assets = MmxSaberAssetsParse(data, size, expected_manifest_sha, reason, reason_size);
  free(data);
  return assets;
}

const MmxSaberAnimation *MmxSaberAssetsAnimationById(const MmxSaberAssets *assets,
                                                     uint16_t animation_id) {
  return assets ? find_animation(assets, animation_id) : NULL;
}

const MmxSaberStep *MmxSaberAssetsAnimationStep(const MmxSaberAssets *assets,
                                                uint16_t animation_id,
                                                uint16_t step_index) {
  const MmxSaberAnimation *animation = MmxSaberAssetsAnimationById(assets, animation_id);
  if (!animation || step_index >= animation->step_count) return NULL;
  return &animation->steps[step_index];
}

const MmxSaberFrame *MmxSaberAssetsFrameForStep(const MmxSaberAssets *assets,
                                                uint16_t animation_id,
                                                uint16_t step_index) {
  const MmxSaberStep *step = MmxSaberAssetsAnimationStep(assets, animation_id, step_index);
  if (!step || step->frame_index >= assets->frame_count) return NULL;
  return &assets->frames[step->frame_index];
}

const uint16_t *MmxSaberAssetsPalette(const MmxSaberAssets *assets) {
  return assets ? assets->palette : NULL;
}

uint16_t MmxSaberAssetsPaletteCount(const MmxSaberAssets *assets) {
  return assets ? assets->palette_count : 0;
}

const uint8_t *MmxSaberAssetsManifestSha256(const MmxSaberAssets *assets) {
  return assets ? assets->manifest_sha256 : NULL;
}
