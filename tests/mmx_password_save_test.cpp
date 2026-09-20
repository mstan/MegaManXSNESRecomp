#include "mmx_password_save.h"
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
static std::string read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

int main() {
  const auto dir = fs::temp_directory_path() / ("mmx-password-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(dir);
  const auto path = dir / "password.srm";
  const auto name = path.u8string();
  std::array<uint8_t, 0x20000> ram;
  ram.fill(0xa5);
  const auto untouched = ram;

  assert(MmxPasswordOpen(name.c_str()));
  assert(read(path) == "MMX-PASSWORD 1\n\n");
  MmxPasswordPrefill(ram.data());
  assert(ram == untouched);  // A new file cannot change the default password.
  MmxPasswordCapture(ram.data());
  assert(read(path) == "MMX-PASSWORD 1\n\n");  // Reject invalid guest digits.

  const std::string digits = "667647273184";
  for (unsigned i = 0; i < 12; ++i) ram[0xffcb + i] = digits[i] - '1';
  const auto before_capture = ram;
  MmxPasswordCapture(ram.data());
  assert(ram == before_capture);  // Saving may not modify any guest RAM.
  assert(read(path) == "MMX-PASSWORD 1\n" + digits + "\n");
  const auto backup = dir / "password.srm.bak";
  assert(read(backup) == "MMX-PASSWORD 1\n\n");
  MmxPasswordCapture(ram.data());
  assert(read(backup) == "MMX-PASSWORD 1\n\n");  // No repeated disk writes.
  MmxPasswordClose();

  assert(MmxPasswordOpen(name.c_str()));  // Fresh session, read from disk.
  ram = untouched;
  auto expected = untouched;
  for (unsigned i = 0; i < 12; ++i) expected[0x1e60 + i] = digits[i] - '1';
  MmxPasswordPrefill(ram.data());
  assert(ram == expected);  // Exactly twelve entry digits, no adjacent memory.
  MmxPasswordClose();
  ram = untouched;
  MmxPasswordPrefill(ram.data());
  MmxPasswordCapture(ram.data());
  assert(ram == untouched);

  { std::ofstream out(path, std::ios::binary); out << "unrelated SRAM"; }
  assert(!MmxPasswordOpen(name.c_str()));
  ram = before_capture;
  MmxPasswordCapture(ram.data());
  MmxPasswordPrefill(ram.data());
  assert(ram == before_capture);
  assert(read(path) == "unrelated SRAM");
  MmxPasswordClose();
  fs::remove(path);
  fs::remove(backup);
  fs::remove(dir);
}
