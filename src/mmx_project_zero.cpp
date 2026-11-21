#include "mmx_project_zero.h"
#include "rom_patch.h"
#include "sha256.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr size_t kPatchSize = 585458;
constexpr size_t kStockSize = 0x180000;
constexpr size_t kPatchedSize = 0x200000;
constexpr unsigned kBodyDma = 0x2a597;
constexpr unsigned kCaptureDma = 0x2a886;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

bool matches(const uint8_t *data, size_t size, std::string_view expected) {
  uint8_t digest[32];
  sha256_compute(data, size, digest);
  char hex[65];
  for (unsigned i = 0; i < sizeof(digest); ++i)
    std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return expected == hex;
}

class Donor {
  const uint8_t *data_;
public:
  explicit Donor(const uint8_t *data) : data_(data) {}

  const uint8_t *read(size_t offset, size_t size) const {
    require(offset <= kPatchedSize && size <= kPatchedSize - offset,
            "Project Zero sprite data exceeds the donor image.");
    return data_ + offset;
  }

  unsigned word(size_t offset) const {
    const uint8_t *p = read(offset, 2);
    return p[0] | (unsigned(p[1]) << 8);
  }

  size_t pointer(size_t offset) const {
    const uint8_t *p = read(offset, 3);
    require(p[1] & 0x80, "Invalid Project Zero sprite address.");
    return ((p[2] & 127) << 15) | ((p[1] & 127) << 8) | p[0];
  }

  void draw(unsigned group, unsigned frame, unsigned dma, unsigned upload,
            uint8_t *pixels) const {
    std::array<uint8_t, 0x800> tiles{};
    size_t entry = dma + word(dma + upload * 2);
    for (unsigned i = 0;; ++i, entry += 5) {
      const uint8_t *record = read(entry, 5);
      unsigned length = record[0] * 16;
      unsigned target = (record[4] & 127) * 512 - 0xc000;
      require(i < 8 && length && target <= tiles.size() && length <= tiles.size() - target,
              "Invalid Project Zero sprite transfer.");
      const uint8_t *source = read(pointer(entry + 1), length);
      std::copy_n(source, length, tiles.begin() + target);
      if (record[4] & 128) break;
    }

    size_t layout = pointer(pointer(0x68000 + group * 3) + frame * 3);
    unsigned pieces = *read(layout, 1);
    require(pieces && pieces <= 16, "Invalid Project Zero sprite layout.");
    const uint8_t *piece = read(layout + 1, pieces * 4);
    for (unsigned i = pieces; i-- > 0;) {
      const uint8_t *p = piece + i * 4;
      int x = 64 + int8_t(p[0]), y = 64 + int8_t(p[1]);
      unsigned size = p[3] & 32 ? 16 : 8;
      require(x >= 0 && x + int(size) <= MMX_ZERO_WIDTH &&
              y >= 0 && y + int(size) <= MMX_ZERO_HEIGHT,
              "Project Zero sprite exceeds the player canvas.");
      for (unsigned dy = 0; dy < size; ++dy) {
        for (unsigned dx = 0; dx < size; ++dx) {
          unsigned tx = p[3] & 64 ? size - 1 - dx : dx;
          unsigned ty = p[3] & 128 ? size - 1 - dy : dy;
          unsigned tile = (p[2] & 0xf0) + ((p[2] + tx / 8) & 15) + (ty / 8) * 16;
          unsigned bits = tile * 32 + (ty & 7) * 2;
          require(bits + 17 < tiles.size(), "Invalid Project Zero sprite tile.");
          unsigned color = 0;
          for (unsigned plane = 0; plane < 4; ++plane)
            color |= ((tiles[bits + (plane / 2) * 16 + plane % 2] >> (7 - tx % 8)) & 1) << plane;
          if (color) pixels[(y + dy) * MMX_ZERO_WIDTH + x + dx] = uint8_t(16 + color);
        }
      }
    }
  }
};
}

MmxProjectZeroAssets *MmxProjectZeroRead(const char *path,
    const uint8_t *rom, size_t rom_size, char *error, size_t error_size) {
  try {
    require(rom && rom_size == kStockSize &&
        matches(rom, rom_size, "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),
        "X1 Zero requires the original Mega Man X USA Rev 1 ROM.");
    require(path && *path, "Select the Mega Man X: Project Zero v1.0 IPS patch.");
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    require(file && file.tellg() == kPatchSize, "Select the Mega Man X: Project Zero v1.0 IPS patch.");
    std::vector<uint8_t> patch(kPatchSize);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(patch.data()), patch.size());
    require(file && matches(patch.data(), patch.size(),
        "060bc0b1900ac3541f0e5cebc48109a200a169290489662f6ede07660cc44ec5"),
        "This file differs from the supported Project Zero v1.0 IPS patch.");

    uint8_t *output = nullptr;
    size_t output_size = 0;
    RomPatchStatus status = snes_rom_patch_apply(rom, rom_size, patch.data(), patch.size(),
        kPatchedSize, &output, &output_size);
    std::unique_ptr<uint8_t, decltype(&std::free)> image(output, std::free);
    require(status == kRomPatch_Ok && output_size == kPatchedSize,
            "Cannot read the Project Zero donor image.");
    Donor donor(image.get());
    std::unique_ptr<MmxProjectZeroAssets, decltype(&std::free)> art(
        static_cast<MmxProjectZeroAssets *>(std::calloc(1, sizeof(MmxProjectZeroAssets))), std::free);
    require(bool(art), "Cannot allocate Project Zero player assets.");

    for (unsigned pose = 0; pose < MMX_PROJECT_ZERO_BODY_POSES; ++pose) {
      unsigned upload = pose;
      // These X1 poses reuse tiles already resident from the preceding frame.
      if (pose == 0x53) continue; // Native blank held-item pose.
      if (pose == 2) upload = 0;
      if (pose >= 0x3f && pose <= 0x41) upload = 0x3e;
      if (pose == 0x4c) upload = 0x4b;
      donor.draw(0, pose, kBodyDma, upload, art->body[pose]);
    }
    for (unsigned pose = 0; pose < MMX_PROJECT_ZERO_CAPTURE_POSES; ++pose)
      donor.draw(0x66, pose, kCaptureDma, pose == 1 ? 0 : pose, art->capture[pose]);

    // Center the opaque body at the native pause preview actor's Y=$00A0.
    // Keep its weapon gauge below the character.
    int top = MMX_ZERO_HEIGHT, bottom = -1;
    for (int y = 0; y < MMX_ZERO_HEIGHT; ++y) {
      const uint8_t *row = art->body[0] + y * MMX_ZERO_WIDTH;
      if (std::any_of(row, row + MMX_ZERO_WIDTH, [](uint8_t color) { return color != 0; })) {
        top = std::min(top, y);
        bottom = y;
      }
    }
    require(bottom >= top, "Missing Project Zero idle graphics.");
    art->menu_y = 160 + 64 - (top + bottom) / 2;

    // X1 loads the normal buster palette through $86:8233 into CGRAM $90.
    unsigned palette = 0x30000 + (donor.word(0x30233) & 0x7fff);
    const uint8_t *transfer = donor.read(palette, 4);
    require(transfer[0] == 16 && transfer[3] == 0x90,
            "Unexpected Project Zero player palette transfer.");
    unsigned colors = 0x28000 + (donor.word(palette + 1) & 0x7fff);
    for (unsigned i = 0; i < 16; ++i)
      art->colors[16 + i] = donor.word(colors + 2 * i) & 0x7fff;

    art->jump_speed = donor.word(0x339c5);
    art->muzzle_y[0] = *donor.read(0x33e3a, 1);
    art->muzzle_y[1] = *donor.read(0x33e74, 1);
    if (error && error_size) error[0] = 0;
    return art.release();
  } catch (const std::exception &e) {
    if (error && error_size) std::snprintf(error, error_size, "%s", e.what());
    return nullptr;
  }
}
