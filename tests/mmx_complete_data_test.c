#include "mmx_complete.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* The table-only test has no machine or practice menu. */
void MmxCompleteDebugReset(void) {}
int main(int argc, char **argv) {
  assert(argc >= 2);
  FILE *f = fopen(argv[1], "rb"); assert(f);
  fseek(f, 0, SEEK_END); size_t size = (size_t)ftell(f); rewind(f);
  uint8_t *rom = malloc(size), *stock = malloc(size); assert(rom && stock);
  assert(fread(stock, 1, size, f) == size); fclose(f);
  if (size % 32768 == 512) { size -= 512; memmove(stock, stock + 512, size); }
  memcpy(rom, stock, size); MmxCompleteBindRom(rom, size);
  if (argc == 3) {
    unsigned mask = (unsigned)strtoul(argv[2], NULL, 0);
    for (unsigned i = 0; i < 5; ++i)
      assert(MmxCompleteSetEnabled((MmxCompleteFeature)i, (mask & (1u << i)) != 0));
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    assert(fwrite(rom, 1, size, stdout) == size);
    free(rom); free(stock); return 0;
  }
  for (unsigned mask = 0; mask < 32; ++mask) {
    for (unsigned i = 0; i < 5; ++i) {
      bool on = (mask & (1u << i)) != 0;
      assert(MmxCompleteSetEnabled((MmxCompleteFeature)i, on));
      assert(MmxCompleteEnabled((MmxCompleteFeature)i) == on);
    }
    MmxCompleteReset();
    assert(!memcmp(rom, stock, size));
  }
  /* Overlap refusal is atomic; unrelated cartridge edits are preserved. */
  rom[0x3719d] ^= 1;
  assert(!MmxCompleteSetEnabled(MMX_COMPLETE_DAMAGE, true));
  assert(!memcmp(rom + 0x370ab, stock + 0x370ab, 2));
  assert(rom[0x3719d] == (uint8_t)(stock[0x3719d] ^ 1));
  rom[0x3719d] ^= 1;
  assert(MmxCompleteSetEnabled(MMX_COMPLETE_DAMAGE, true));
  rom[0x123] ^= 1;
  MmxCompleteReset(); assert(rom[0x123] == (uint8_t)(stock[0x123] ^ 1));
  rom[0x123] ^= 1;
  assert(!memcmp(rom, stock, size));
  MmxCompleteBindRom(rom, 10);
  assert(!MmxCompleteSetEnabled(MMX_COMPLETE_SCRIPT, true));
  free(rom); free(stock);
  puts("MMX COMPLETE DATA CHECKS PASSED");
  return 0;
}
