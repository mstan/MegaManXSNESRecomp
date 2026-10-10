#include "mmx_saber_wave_assets.h"

#include "mmx_saber_wave.h"
#include "sha256.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#define MMX_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MMX_MKDIR(path) mkdir(path, 0755)
#endif

enum {
  MMX_SABER_WAVE_ROM_MAX_BYTES = 8 * 1024 * 1024,
  MMX_SABER_WAVE_TILE_BYTES = 8192,
  MMX_SABER_WAVE_FILE_BYTES = 8028,
  MMX_SABER_WAVE_FRAME_BYTES = 40 * 48,
  MMX_SABER_WAVE_FRAME_OFFSET = 120,
  MMX_SABER_WAVE_STEP_OFFSET = 216,
  MMX_SABER_WAVE_COLLISION_OFFSET = 344,
  MMX_SABER_WAVE_PIXEL_OFFSET = 348
};

typedef struct WaveBuilder {
  char *error;
  size_t error_size;
} WaveBuilder;

typedef struct WaveRom {
  uint8_t *data;
  size_t size;
} WaveRom;

typedef struct WaveTiles {
  uint8_t bytes[MMX_SABER_WAVE_TILE_BYTES];
  uint8_t known[MMX_SABER_WAVE_TILE_BYTES];
} WaveTiles;

typedef struct WavePose {
  int left;
  int top;
  int width;
  int height;
  uint8_t *pixels;
} WavePose;

static const uint8_t k_x3_normalized_sha256[32] = {
  0x65, 0xb0, 0x32, 0x68, 0xaf, 0xac, 0x29, 0x63,
  0x30, 0xe8, 0xff, 0x8d, 0x60, 0xdd, 0x08, 0x25,
  0x87, 0x9e, 0x13, 0xed, 0x65, 0x8b, 0x37, 0x71,
  0x3c, 0x03, 0x4a, 0x3b, 0xd0, 0x74, 0xf1, 0xd7
};

static int fail(WaveBuilder *builder, const char *message) {
  if (builder && builder->error && builder->error_size) {
    snprintf(builder->error, builder->error_size, "%s", message ? message : "");
  }
  return 0;
}

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u24(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16);
}

static void put_u16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static int load_rom(const char *path, WaveRom *rom, WaveBuilder *builder) {
  FILE *file;
  long length;
  size_t size;
  uint8_t digest[32];
  uint8_t *data;

  memset(rom, 0, sizeof(*rom));
  if (!path || !path[0]) return fail(builder, "Select the original source ROM.");
  file = fopen(path, "rb");
  if (!file) return fail(builder, "Cannot open the source ROM.");
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return fail(builder, "Cannot seek the source ROM.");
  }
  length = ftell(file);
  if (length <= 0 || length > MMX_SABER_WAVE_ROM_MAX_BYTES) {
    fclose(file);
    return fail(builder, "Unsupported source ROM size.");
  }
  size = (size_t)length;
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return fail(builder, "Cannot seek the source ROM.");
  }
  data = (uint8_t *)malloc(size);
  if (!data) {
    fclose(file);
    return fail(builder, "Cannot allocate source ROM.");
  }
  if (fread(data, 1, size, file) != size) {
    free(data);
    fclose(file);
    return fail(builder, "Cannot read the source ROM.");
  }
  fclose(file);

  if (size % 32768u == 512u) {
    memmove(data, data + 512, size - 512);
    size -= 512;
  }
  sha256_compute(data, size, digest);
  if (memcmp(digest, k_x3_normalized_sha256, sizeof(digest)) != 0) {
    free(data);
    return fail(builder, "Select the original Mega Man X3 USA ROM.");
  }
  rom->data = data;
  rom->size = size;
  return 1;
}

static void free_rom(WaveRom *rom) {
  if (!rom) return;
  free(rom->data);
  rom->data = NULL;
  rom->size = 0;
}

static int rom_pointer(const WaveRom *rom, uint32_t address, size_t length,
                       const uint8_t **result, WaveBuilder *builder) {
  size_t offset;
  if ((address & 0xffffu) < 0x8000u)
    return fail(builder, "Invalid source ROM address.");
  offset = (size_t)((address >> 16) & 127u) * 32768u +
      (size_t)(address & 32767u);
  if (offset > rom->size || length > rom->size - offset)
    return fail(builder, "Source ROM read exceeds bounds.");
  *result = rom->data + offset;
  return 1;
}

static int rom_integer(const WaveRom *rom, uint32_t address, unsigned bytes,
                       uint32_t *result, WaveBuilder *builder) {
  const uint8_t *source;
  uint32_t value = 0;
  unsigned i;
  if (!bytes || bytes > 4 ||
      !rom_pointer(rom, address, bytes, &source, builder)) return 0;
  for (i = 0; i < bytes; ++i) value |= (uint32_t)source[i] << (i * 8);
  *result = value;
  return 1;
}

static int direct_transfer(const WaveRom *rom, uint32_t address,
                           WaveTiles *tiles, WaveBuilder *builder) {
  unsigned i;
  for (i = 0; i < 64; ++i, address += 6) {
    uint32_t count, source_address, target;
    uint32_t start, length;
    const uint8_t *source;
    if (!rom_integer(rom, address, 1, &count, builder)) return 0;
    if (!count) return 1;
    if (!rom_integer(rom, address + 1, 3, &source_address, builder) ||
        !rom_integer(rom, address + 4, 2, &target, builder)) return 0;
    if ((target & 0x7fffu) < 0x6000u)
      return fail(builder, "Invalid source wave transfer.");
    start = ((target & 0x7fffu) - 0x6000u) * 2u;
    length = count * 16u;
    if (start > MMX_SABER_WAVE_TILE_BYTES ||
        length > MMX_SABER_WAVE_TILE_BYTES - start)
      return fail(builder, "Invalid source wave transfer.");
    if (!rom_pointer(rom, source_address, length, &source, builder)) return 0;
    memcpy(tiles->bytes + start, source, length);
    memset(tiles->known + start, 1, length);
    if (target & 0x8000u) return 1;
  }
  return fail(builder, "Unterminated source wave transfer.");
}

static int wave_pose(const WaveRom *rom, unsigned number, const WaveTiles *tiles,
                     WavePose *pose, WaveBuilder *builder) {
  uint32_t table, address, count;
  const uint8_t *pieces;
  int right = -256, bottom = -256;
  unsigned i;

  memset(pose, 0, sizeof(*pose));
  if (!rom_integer(rom, 0x8d8000u + 0x4fu * 3u, 3, &table, builder) ||
      !rom_integer(rom, table + number * 3u, 3, &address, builder) ||
      !rom_integer(rom, address, 1, &count, builder)) return 0;
  if (count > 64) return fail(builder, "Invalid source sprite piece count.");
  if (!rom_pointer(rom, address + 1, count * 4u, &pieces, builder)) return 0;

  pose->left = 256;
  pose->top = 256;
  for (i = 0; i < count; ++i) {
    int x = (int)(int8_t)pieces[i * 4u + 1];
    int y = (int)(int8_t)pieces[i * 4u + 2];
    int size = pieces[i * 4u] & 32u ? 16 : 8;
    if (pieces[i * 4u] & 14u)
      return fail(builder, "Unexpected source weapon palette.");
    if (x < pose->left) pose->left = x;
    if (y < pose->top) pose->top = y;
    if (x + size > right) right = x + size;
    if (y + size > bottom) bottom = y + size;
  }
  pose->width = right - pose->left;
  pose->height = bottom - pose->top;
  if (pose->width > 256 || pose->height > 256)
    return fail(builder, "Source sprite exceeds extraction bounds.");
  if (pose->width && pose->height) {
    pose->pixels = (uint8_t *)calloc((size_t)pose->width * pose->height, 1);
    if (!pose->pixels) return fail(builder, "Cannot allocate source wave pose.");
  }

  for (i = count; i-- > 0;) {
    unsigned flags = pieces[i * 4u];
    unsigned tile = pieces[i * 4u + 3];
    unsigned size = flags & 32u ? 16u : 8u;
    int x = (int)(int8_t)pieces[i * 4u + 1];
    int y = (int)(int8_t)pieces[i * 4u + 2];
    unsigned dy, dx;
    for (dy = 0; dy < size; ++dy) {
      for (dx = 0; dx < size; ++dx) {
        unsigned tx = flags & 64u ? size - 1u - dx : dx;
        unsigned ty = flags & 128u ? size - 1u - dy : dy;
        unsigned tile_number = (((tile >> 4) + ty / 8u) & 15u) * 16u +
            ((tile + tx / 8u) & 15u);
        unsigned bits = tile_number * 32u + (ty & 7u) * 2u;
        unsigned color = 0;
        unsigned plane;
        int px, py;
        for (plane = 0; plane < 4; ++plane) {
          unsigned index = bits + (plane / 2u) * 16u + plane % 2u;
          if (index >= MMX_SABER_WAVE_TILE_BYTES || !tiles->known[index])
            return fail(builder, "Unresolved inherited source graphics.");
          color |= ((tiles->bytes[index] >> (7u - (tx & 7u))) & 1u) << plane;
        }
        if (!color) continue;
        px = x + (int)dx - pose->left;
        py = y + (int)dy - pose->top;
        if (px < 0 || py < 0 || px >= pose->width || py >= pose->height)
          return fail(builder, "Source sprite exceeds canvas.");
        pose->pixels[(size_t)py * pose->width + (size_t)px] = (uint8_t)color;
      }
    }
  }
  return 1;
}

static int build_wave_bytes(const WaveRom *rom, uint8_t **result,
                            size_t *result_size, WaveBuilder *builder) {
  static const uint32_t transfer_lists[] = {
    0x85e672u, 0x85e67fu, 0x85e68bu, 0x85e697u
  };
  uint8_t *data = NULL;
  WavePose poses[MMX_SABER_WAVE_FRAME_COUNT];
  const uint8_t *palette, *collision;
  uint8_t digest[32];
  unsigned i;

  memset(poses, 0, sizeof(poses));
  data = (uint8_t *)calloc(MMX_SABER_WAVE_FILE_BYTES, 1);
  if (!data) return fail(builder, "Cannot allocate source wave cache.");
  if (!rom_pointer(rom, 0x8cb100u, 32, &palette, builder) ||
      !rom_pointer(rom, 0x86b85fu, 4, &collision, builder)) goto failed;

  memcpy(data, "MMXZWAV1", 8);
  put_u16(data + 8, 1);
  put_u16(data + 10, MMX_SABER_WAVE_HEADER_BYTES);
  put_u32(data + 12, MMX_SABER_WAVE_FILE_BYTES);
  put_u16(data + 48, MMX_SABER_WAVE_FRAME_COUNT);
  put_u16(data + 50, MMX_SABER_WAVE_STEP_COUNT);
  put_u16(data + 52, MMX_SABER_WAVE_PALETTE_COUNT);
  put_u16(data + 54, MMX_SABER_WAVE_CANVAS_WIDTH);
  put_u16(data + 56, MMX_SABER_WAVE_CANVAS_HEIGHT);
  put_u16(data + 58, MMX_SABER_WAVE_COLLISION_BYTES);
  put_u32(data + 60, MMX_SABER_WAVE_HEADER_BYTES);
  put_u32(data + 64, MMX_SABER_WAVE_FRAME_OFFSET);
  put_u32(data + 68, MMX_SABER_WAVE_STEP_OFFSET);
  put_u32(data + 72, MMX_SABER_WAVE_COLLISION_OFFSET);
  put_u32(data + 76, MMX_SABER_WAVE_PIXEL_OFFSET);
  put_u32(data + 80, MMX_SABER_WAVE_FRAME_BYTES * MMX_SABER_WAVE_FRAME_COUNT);
  memcpy(data + MMX_SABER_WAVE_HEADER_BYTES, palette, 32);
  memcpy(data + MMX_SABER_WAVE_COLLISION_OFFSET, collision, 4);

  for (i = 0; i < MMX_SABER_WAVE_FRAME_COUNT; ++i) {
    WaveTiles tiles;
    size_t descriptor = MMX_SABER_WAVE_FRAME_OFFSET + i * 24u;
    size_t pixels = MMX_SABER_WAVE_PIXEL_OFFSET +
        i * MMX_SABER_WAVE_FRAME_BYTES;
    unsigned y;
    memset(&tiles, 0, sizeof(tiles));
    if (!direct_transfer(rom, transfer_lists[i], &tiles, builder) ||
        !wave_pose(rom, 12u + i, &tiles, &poses[i], builder)) goto failed;
    if (poses[i].width > MMX_SABER_WAVE_CANVAS_WIDTH ||
        poses[i].height > MMX_SABER_WAVE_CANVAS_HEIGHT) {
      fail(builder, "Source saber wave exceeds its canvas.");
      goto failed;
    }
    put_u32(data + descriptor, (uint32_t)pixels);
    put_u32(data + descriptor + 4, MMX_SABER_WAVE_FRAME_BYTES);
    put_u16(data + descriptor + 8, MMX_SABER_WAVE_CANVAS_WIDTH);
    put_u16(data + descriptor + 10, MMX_SABER_WAVE_CANVAS_HEIGHT);
    put_u16(data + descriptor + 12, (uint16_t)poses[i].left);
    put_u16(data + descriptor + 14, (uint16_t)poses[i].top);
    put_u16(data + descriptor + 16, (uint16_t)(12u + i));
    for (y = 0; y < (unsigned)poses[i].height; ++y) {
      memcpy(data + pixels + (size_t)y * MMX_SABER_WAVE_CANVAS_WIDTH,
             poses[i].pixels + (size_t)y * poses[i].width,
             (size_t)poses[i].width);
    }
  }
  for (i = 0; i < MMX_SABER_WAVE_STEP_COUNT; ++i) {
    size_t offset = MMX_SABER_WAVE_STEP_OFFSET + i * 8u;
    put_u16(data + offset, (uint16_t)(i % MMX_SABER_WAVE_FRAME_COUNT));
    put_u16(data + offset + 2, 2);
  }
  sha256_compute(data, MMX_SABER_WAVE_FILE_BYTES, digest);
  memcpy(data + 16, digest, sizeof(digest));
  for (i = 0; i < MMX_SABER_WAVE_FRAME_COUNT; ++i) free(poses[i].pixels);
  *result = data;
  *result_size = MMX_SABER_WAVE_FILE_BYTES;
  return 1;

failed:
  for (i = 0; i < MMX_SABER_WAVE_FRAME_COUNT; ++i) free(poses[i].pixels);
  free(data);
  return 0;
}

static int make_one_directory(const char *path, WaveBuilder *builder) {
  struct stat info;
  if (!path || !path[0]) return 1;
  if (stat(path, &info) == 0)
    return S_ISDIR(info.st_mode) ? 1 : fail(builder, "Cache path is not a directory.");
  if (MMX_MKDIR(path) == 0) return 1;
  if (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return 1;
  return fail(builder, "Cannot create local mod cache directory.");
}

static int make_parent_directory(const char *path, WaveBuilder *builder) {
  const char *slash, *backslash, *last;
  char parent[4096];
  size_t length, i;

  slash = strrchr(path, '/');
  backslash = strrchr(path, '\\');
  if (!slash) last = backslash;
  else if (!backslash) last = slash;
  else last = slash > backslash ? slash : backslash;
  if (!last) return 1;
  length = (size_t)(last - path);
  if (!length) {
    strcpy(parent, "/");
    return make_one_directory(parent, builder);
  }
  if (length >= sizeof(parent)) return fail(builder, "Cache path is too long.");
  memcpy(parent, path, length);
  parent[length] = '\0';
  {
    struct stat info;
    if (stat(parent, &info) == 0)
      return S_ISDIR(info.st_mode) ? 1 : fail(builder, "Cache path is not a directory.");
  }
  for (i = 0; i < length; ++i) {
    if (parent[i] != '/' && parent[i] != '\\') continue;
    if (i == 0 || (i == 2 && parent[1] == ':')) continue;
    parent[i] = '\0';
    if (!make_one_directory(parent, builder)) return 0;
    parent[i] = path[i];
  }
  return make_one_directory(parent, builder);
}

static int publish(const char *path, const uint8_t *data, size_t size,
                   WaveBuilder *builder) {
  char temporary[4096];
  unsigned long process_id;
  FILE *file;
  int written;
  size_t written_bytes;
  int close_result;

  if (!make_parent_directory(path, builder)) return 0;
#ifdef _WIN32
  process_id = (unsigned long)GetCurrentProcessId();
#else
  process_id = (unsigned long)getpid();
#endif
  written = snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, process_id);
  if (written < 0 || (size_t)written >= sizeof(temporary))
    return fail(builder, "Cache path is too long.");
  file = fopen(temporary, "wb");
  if (!file) return fail(builder, "Cannot create local mod cache.");
  written_bytes = fwrite(data, 1, size, file);
  close_result = fclose(file);
  if (written_bytes != size || close_result != 0) {
    remove(temporary);
    return fail(builder, "Cannot write local mod cache.");
  }
#ifdef _WIN32
  if (!MoveFileExA(temporary, path,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    remove(temporary);
    return fail(builder, "Cannot publish local mod cache.");
  }
#else
  if (rename(temporary, path) != 0) {
    remove(temporary);
    return fail(builder, "Cannot publish local mod cache.");
  }
#endif
  return 1;
}

static int cache_is_valid(const char *path) {
  char reason[128];
  MmxSaberWave *wave = MmxSaberWaveLoadFile(path, reason, sizeof(reason));
  if (!wave) return 0;
  MmxSaberWaveFree(wave);
  return 1;
}

int MmxSaberWaveAssetsBuild(const char *rom_path, const char *output,
                            char *error, size_t error_size) {
  WaveBuilder builder = {error, error_size};
  WaveRom rom;
  uint8_t *data = NULL;
  size_t size = 0;
  int ok;

  if (error && error_size) error[0] = '\0';
  if (!output || !output[0]) return fail(&builder, "Invalid source wave output.");
  if (!load_rom(rom_path, &rom, &builder)) return 0;
  if (cache_is_valid(output)) {
    free_rom(&rom);
    return 1;
  }
  ok = build_wave_bytes(&rom, &data, &size, &builder);
  free_rom(&rom);
  if (!ok) return 0;
  ok = publish(output, data, size, &builder);
  free(data);
  return ok;
}
