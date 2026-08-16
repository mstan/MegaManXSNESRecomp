#include "mod_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MMX_SRAM_PLUGIN "megaman-x.sram"
#define MMX_PASSWORD_SAVE_BYTES 2048u
#define MMX_PASSWORD_DIGITS 12u
#define MMX_PASSWORD_WRAM 0x1e60u
#define MMX_PASSWORD_SRAM_OFFSET 0x100u
#define MMX_PASSWORD_PREFILL_GRACE_FRAMES 90

extern uint8_t g_ram[0x20000];
void RtlEnsureSaveDir(void);
void RtlSramFilePath(char *buf, size_t buflen);

typedef struct MmxSramPassword {
  char magic[8];
  uint8_t version;
  uint8_t digits[MMX_PASSWORD_DIGITS];
  uint8_t checksum;
} MmxSramPassword;

static const char kMmxSramMagic[8] = {'M', 'M', 'X', 'P', 'A', 'S', 'S', 0};

static int g_mmx_sram_active;
static int g_mmx_password_prefill_frames;
static uint8_t g_mmx_password_save[MMX_PASSWORD_SAVE_BYTES];

static void mmx_sram_frame(void);

int mmx_sram_mod_active(void) {
  return g_mmx_sram_active;
}

static void mmx_sram_reset(void) {
  g_mmx_password_prefill_frames = 0;
}

static void mmx_sram_read_file(void) {
  char path[128];
  RtlSramFilePath(path, sizeof(path));
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  if (fread(g_mmx_password_save, 1, sizeof(g_mmx_password_save), f) !=
      sizeof(g_mmx_password_save))
    memset(g_mmx_password_save, 0, sizeof(g_mmx_password_save));
  fclose(f);
}

static void mmx_sram_write_file(void) {
  char path[128], bak[140];
  RtlEnsureSaveDir();
  RtlSramFilePath(path, sizeof(path));
  snprintf(bak, sizeof(bak), "%s.bak", path);
  rename(path, bak);
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "MMX SRAM mod: unable to write %s\n", path);
    return;
  }
  fwrite(g_mmx_password_save, 1, sizeof(g_mmx_password_save), f);
  fclose(f);
}

static void mmx_sram_activate(void) {
  memset(g_mmx_password_save, 0, sizeof(g_mmx_password_save));
  mmx_sram_read_file();
  g_mmx_sram_active = 1;
  g_mmx_password_prefill_frames = 0;
  (void)snes_mod_register_frame_callback(mmx_sram_frame);
}

static uint8_t mmx_password_checksum(const uint8_t digits[MMX_PASSWORD_DIGITS]) {
  uint8_t checksum = 0x5a;
  for (uint32_t i = 0; i < MMX_PASSWORD_DIGITS; i++)
    checksum = (uint8_t)((checksum * 33u) ^ digits[i]);
  return checksum;
}

static int mmx_sram_password_valid(const MmxSramPassword *slot) {
  if (!slot || memcmp(slot->magic, kMmxSramMagic, sizeof(slot->magic)) != 0 ||
      slot->version != 1)
    return 0;
  for (uint32_t i = 0; i < MMX_PASSWORD_DIGITS; i++) {
    if (slot->digits[i] < 1 || slot->digits[i] > 8)
      return 0;
  }
  return slot->checksum == mmx_password_checksum(slot->digits);
}

static MmxSramPassword *mmx_sram_password_slot(void) {
  if (sizeof(g_mmx_password_save) <
      MMX_PASSWORD_SRAM_OFFSET + sizeof(MmxSramPassword))
    return NULL;
  return (MmxSramPassword *)(g_mmx_password_save + MMX_PASSWORD_SRAM_OFFSET);
}

static int mmx_password_digits_equal(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, MMX_PASSWORD_DIGITS) == 0;
}

static int mmx_live_password_valid(uint8_t digits[MMX_PASSWORD_DIGITS],
                                   int *is_default) {
  int any_non_default = 0;
  for (uint32_t i = 0; i < MMX_PASSWORD_DIGITS; i++) {
    uint8_t zero_based = g_ram[MMX_PASSWORD_WRAM + i];
    if (zero_based > 7)
      return 0;
    digits[i] = (uint8_t)(zero_based + 1);
    if (zero_based != 0)
      any_non_default = 1;
  }
  if (is_default)
    *is_default = !any_non_default;
  return 1;
}

static int mmx_password_screen_id_ok(uint8_t screen_id) {
  return screen_id == 3 || screen_id == 0x23 || screen_id == 0x24;
}

static int mmx_password_prefill_context_visible(void) {
  uint8_t screen_id = g_ram[0x1f7a];
  return g_ram[0x003b] == 0 && g_ram[0x003c] <= 1 &&
         mmx_password_screen_id_ok(screen_id);
}

static int mmx_password_capture_screen_visible(void) {
  if (g_ram[0x003b] != 0 || g_ram[0x003c] != 1 ||
      !mmx_password_screen_id_ok(g_ram[0x1f7a]))
    return 0;

  uint8_t digits[MMX_PASSWORD_DIGITS];
  if (!mmx_live_password_valid(digits, NULL))
    return 0;

  uint8_t cursor_x = g_ram[0x1e4c];
  uint8_t cursor_y = g_ram[0x1e4f];
  if (cursor_x > 3 || cursor_y > 3)
    return 0;

  uint8_t pixel_x = g_ram[0x0e6d];
  return pixel_x == 0x30 || pixel_x == 0x38 || pixel_x == 0x68 ||
         pixel_x == 0x98 || pixel_x == 0xc8 || pixel_x == 0xd0;
}

static void mmx_sram_store_password(const uint8_t digits[MMX_PASSWORD_DIGITS]) {
  MmxSramPassword *slot = mmx_sram_password_slot();
  if (!slot)
    return;
  if (mmx_sram_password_valid(slot) &&
      mmx_password_digits_equal(slot->digits, digits))
    return;
  memcpy(slot->magic, kMmxSramMagic, sizeof(slot->magic));
  slot->version = 1;
  memcpy(slot->digits, digits, MMX_PASSWORD_DIGITS);
  slot->checksum = mmx_password_checksum(slot->digits);
  mmx_sram_write_file();
}

static void mmx_sram_prefill_password(const MmxSramPassword *slot) {
  for (uint32_t i = 0; i < MMX_PASSWORD_DIGITS; i++)
    g_ram[MMX_PASSWORD_WRAM + i] = (uint8_t)(slot->digits[i] - 1);
}

static void mmx_sram_frame(void) {
  if (!g_mmx_sram_active)
    return;
  const MmxSramPassword *slot = mmx_sram_password_slot();
  if (!slot)
    return;

  int slot_valid = mmx_sram_password_valid(slot);
  int prefill_context = mmx_password_prefill_context_visible();

  uint8_t live_digits[MMX_PASSWORD_DIGITS];
  int is_default = 0;
  if (!mmx_live_password_valid(live_digits, &is_default)) {
    if (slot_valid && prefill_context && g_mmx_password_prefill_frames > 0) {
      mmx_sram_prefill_password(slot);
      g_mmx_password_prefill_frames--;
    }
    return;
  }

  if (slot_valid && prefill_context) {
    int live_is_saved = mmx_password_digits_equal(live_digits, slot->digits);
    if (is_default)
      g_mmx_password_prefill_frames = MMX_PASSWORD_PREFILL_GRACE_FRAMES;
    if (g_mmx_password_prefill_frames > 0 &&
        (is_default || live_is_saved)) {
      mmx_sram_prefill_password(slot);
      g_mmx_password_prefill_frames--;
      return;
    }
    if (!is_default && !live_is_saved)
      g_mmx_password_prefill_frames = 0;
  } else {
    g_mmx_password_prefill_frames = 0;
  }

  int visible = mmx_password_capture_screen_visible();
  if (!visible)
    return;

  if (!is_default)
    mmx_sram_store_password(live_digits);
}

SNES_MOD_CONSTRUCTOR(mmx_register_sram_plugin) {
  (void)snes_mod_register_reset_callback(mmx_sram_reset);
  (void)snes_mod_register_activation_plugin(MMX_SRAM_PLUGIN,
                                            mmx_sram_activate);
}
