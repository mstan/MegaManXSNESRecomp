#include "mmx_password_save.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
constexpr char kHeader[] = "MMX-PASSWORD 1\n";
fs::path save_path;
std::string saved_digits;
bool active;

bool valid_digits(const std::string &digits) {
  if (digits.size() != 12) return false;
  for (char c : digits) if (c < '1' || c > '8') return false;
  return true;
}

bool write_record(const std::string &digits) {
  fs::path temporary = save_path; temporary += ".tmp";
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  out << kHeader << digits << '\n';
  out.close();
  if (!out) return false;
  std::error_code ec;
  bool exists = fs::exists(save_path, ec);
  if (ec) return false;
  if (exists) {
    fs::path backup = save_path; backup += ".bak";
    fs::copy_file(save_path, backup, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
  }
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), save_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  fs::rename(temporary, save_path, ec);
  return !ec;
#endif
}

void report_error(const char *message) {
  fprintf(stderr, "[mmx-password] %s: %s\n", message, save_path.u8string().c_str());
}
}

extern "C" void MmxPasswordClose(void) {
  active = false;
  saved_digits.clear();
  save_path.clear();
}

extern "C" bool MmxPasswordOpen(const char *path) {
  MmxPasswordClose();
  if (!path || !*path) return false;
  save_path = fs::u8path(path);
  std::error_code ec;
  bool exists = fs::exists(save_path, ec);
  if (ec) { report_error("Cannot access save file"); return false; }
  if (!exists) {
    if (!write_record("")) { report_error("Cannot create save file"); return false; }
  } else {
    auto size = fs::file_size(save_path, ec);
    if (ec || size > 64) { report_error("Unrecognized password save; left unchanged"); return false; }
    std::ifstream in(save_path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), {});
    const std::string prefix(kHeader);
    if (!in || data.size() <= prefix.size() ||
        data.compare(0, prefix.size(), prefix) || data.back() != '\n') {
      report_error("Unrecognized password save; left unchanged"); return false;
    }
    saved_digits = data.substr(prefix.size(), data.size() - prefix.size() - 1);
    if (!saved_digits.empty() && !valid_digits(saved_digits)) {
      saved_digits.clear();
      report_error("Invalid password save; left unchanged"); return false;
    }
  }
  active = true;
  fprintf(stderr, "[mmx-password] Using %s (%s)\n", path,
          saved_digits.empty() ? "empty" : "password saved");
  return true;
}

extern "C" void MmxPasswordPrefill(uint8_t *ram) {
  if (!active || saved_digits.empty()) return;
  /* $00:EF25: the entry screen has just copied its default twelve digits.
   * $1E60 is a password buffer ONLY here; gameplay reuses this memory. */
  for (unsigned i = 0; i < 12; ++i)
    ram[0x1e60 + i] = (uint8_t)(saved_digits[i] - '1');
  fprintf(stderr, "[mmx-password] Prefilled %s\n", saved_digits.c_str());
}

extern "C" void MmxPasswordCapture(const uint8_t *ram) {
  if (!active) return;
  /* $00:F05E: the game's generated-password display is ready. The visible
   * digits are at $7E:FFCB, not the encoder's scratch bytes at $1E60. */
  std::string digits;
  for (unsigned i = 0; i < 12; ++i) {
    if (ram[0xffcb + i] > 7) return;
    digits += (char)('1' + ram[0xffcb + i]);
  }
  if (digits == saved_digits) return;
  if (!write_record(digits)) {
    report_error("Cannot update save; previous file preserved");
    active = false;
    return;
  }
  saved_digits = digits;
  fprintf(stderr, "[mmx-password] Saved %s\n", saved_digits.c_str());
}
