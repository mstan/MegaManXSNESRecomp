/* Saber Zero obeys X1's fire lock: no swing where the native player cannot
 * shoot (Vile's electric restraint, ground shock), as for a hit. */
#include "saber/mmx_saber_attack.h"
#include "saber/mmx_saber_frame.h"

static bool saber_swinging(void) {
  return MmxSaberAttackSnapshotGet().phase != SABER_PHASE_IDLE;
}
static void saber_lock_checks(const char *assets, uint8_t *start, size_t cap) {
  check(assets && MmxZeroLoad(assets), "Saber lock test loads owned X3 Zero");
  MmxZeroSetModern(false); MmxZeroResetState(); MmxZeroRegisterHooks();
  MmxSaberFrameReset(); MmxZeroSetExtension(MmxSaberFrameExtension());
  hitbox_boot();
  coop_frames(30, 0, 0);
  check(MmxZeroActive(), "Highway starts as Saber Zero");
  size_t n = RtlSaveSnapshotToMemory(start, cap);

  /* Ordinary movement never enters a fire-locked action, so the lock cannot
   * take the Saber away from normal play. */
  static const struct { unsigned input, frames; } moves[] = {
    {SNES_PAD_RIGHT, 30}, {0, 10}, {SNES_PAD_B, 20}, {0, 40},
    {SNES_PAD_A, 25}, {0, 20}, {SNES_PAD_A | SNES_PAD_RIGHT, 6},
    {SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_B, 20}, {SNES_PAD_RIGHT, 40},
    {SNES_PAD_LEFT | SNES_PAD_B, 20}, {0, 40}};
  bool seen[256] = {false};
  for (unsigned m = 0; m < sizeof(moves) / sizeof(moves[0]); ++m)
    for (unsigned f = 0; f < moves[m].frames; ++f) {
      frame(moves[m].input | (f & 8 ? SNES_PAD_Y : 0));
      seen[g_ram[0xbaa]] = true;
    }
  fprintf(stderr, "Saber movement actions:");
  bool ordinary = true;
  for (unsigned a = 0; a < 256; ++a) if (seen[a]) {
    fprintf(stderr, " %02x", a);
    ordinary &= MmxSaberFrameNativeFireAction((uint8_t)a) || a == 0x0e;
  }
  fprintf(stderr, "\n");
  check(ordinary, "walking, jumping and dashing stay in fire-capable actions");

  check(RtlLoadSnapshotFromMemory(start, n), "Saber lock start restores");
  frame(SNES_PAD_Y); frame(0); frame(0);
  check(saber_swinging(), "Y swings the Saber while standing");

  /* $84:9F19-style forced action: Vile's restraint is action $32. */
  check(RtlLoadSnapshotFromMemory(start, n), "Saber lock start restores");
  g_ram[0xbaa] = 0x32; g_ram[0xbab] = 0;
  unsigned restrained = 0, swung = 0;
  for (unsigned f = 0; f < 40; ++f) {
    frame(f & 1 ? 0 : SNES_PAD_Y);
    restrained += g_ram[0xbaa] == 0x32;
    swung += g_ram[0xbaa] == 0x32 && saber_swinging();
  }
  fprintf(stderr, "restraint frames=%u swinging=%u\n", restrained, swung);
  check(restrained >= 10 && !swung, "Vile's electric restraint locks the Saber");

  check(RtlLoadSnapshotFromMemory(start, n), "Saber lock start restores");
  frame(SNES_PAD_Y); frame(0);
  check(saber_swinging(), "a swing is in progress before the stun");
  g_ram[0xbaa] = 0x32; g_ram[0xbab] = 0;
  frame(0);
  check(g_ram[0xbaa] != 0x32 || !saber_swinging(), "the stun ends a swing in progress");

  check(RtlLoadSnapshotFromMemory(start, n), "Saber lock start restores");
  g_ram[0xbaa] = 0x36; g_ram[0xbab] = 0;
  restrained = swung = 0;
  for (unsigned f = 0; f < 20; ++f) {
    frame(f & 1 ? 0 : SNES_PAD_Y);
    restrained += g_ram[0xbaa] == 0x36;
    swung += g_ram[0xbaa] == 0x36 && saber_swinging();
  }
  fprintf(stderr, "ground-shock frames=%u swinging=%u\n", restrained, swung);
  check(restrained > 0 && !swung, "Flame Mammoth's ground shock locks the Saber");
  puts("MMX SABER LOCK CHECKS PASSED");
}
