#include "mod_runtime.h"
#include "common_rtl.h"
#include "cpu_state.h"

#include <stdint.h>
#include <stdlib.h>

#define MMX_BUSTER_DAMAGE_PACKAGE "megaman-x.dev.buster-damage"
#define MMX_BUSTER_DAMAGE_FEATURE "buster-damage"
#define MMX_BUSTER_DAMAGE_OPTION  "damage"
#define MMX_BUSTER_DAMAGE_PLUGIN  "megaman-x.buster-damage"

enum {
  kSlotStart = 0x0d00,
  kSlotEnd = 0x1fe0,
  kSlotStride = 0x20,
  kSlotHpOffset = 0x09,
  kSlotCount = ((kSlotEnd - kSlotStart) / kSlotStride) + 1,
  kMaxStockDamageDelta = 0x20,
};

static int g_mmx_buster_damage_active;
static uint8 g_mmx_buster_damage_value;
static uint8 g_mmx_buster_damage_cap[kSlotCount];
static uint8 g_mmx_buster_damage_cap_valid[kSlotCount];

static uint8 mmx_buster_damage_parse(const char *text, uint8 fallback) {
  if (!text || !text[0])
    return fallback;
  char *end = NULL;
  long value = strtol(text, &end, 0);
  if (!end || *end || value < 0)
    return fallback;
  if (value > 255)
    value = 255;
  return (uint8)value;
}

static int mmx_buster_damage_is_hp_addr(uint32 ram_off) {
  if (ram_off < kSlotStart + kSlotHpOffset ||
      ram_off > kSlotEnd + kSlotHpOffset)
    return 0;
  return ((ram_off - (kSlotStart + kSlotHpOffset)) % kSlotStride) == 0;
}

static unsigned mmx_buster_damage_slot(uint32 ram_off) {
  return (unsigned)((ram_off - (kSlotStart + kSlotHpOffset)) / kSlotStride);
}

static uint8 mmx_buster_damage_filter(
    uint32 ram_off, uint8 old_value, uint8 new_value) {
  if (!g_mmx_buster_damage_active ||
      !mmx_buster_damage_is_hp_addr(ram_off))
    return new_value;

  const unsigned slot = mmx_buster_damage_slot(ram_off);
  if (slot >= kSlotCount)
    return new_value;

  if (old_value == 0)
    g_mmx_buster_damage_cap_valid[slot] = 0;

  if (g_mmx_buster_damage_cap_valid[slot] &&
      old_value == g_mmx_buster_damage_cap[slot] &&
      new_value > g_mmx_buster_damage_cap[slot])
    return g_mmx_buster_damage_cap[slot];

  if (new_value >= old_value)
    return new_value;

  if (new_value == 0) {
    g_mmx_buster_damage_cap_valid[slot] = 0;
    return new_value;
  }

  const uint8 stock_delta = (uint8)(old_value - new_value);
  if (stock_delta > kMaxStockDamageDelta)
    return new_value;

  const uint8 target = g_mmx_buster_damage_value >= old_value
      ? 1
      : (uint8)(old_value - g_mmx_buster_damage_value);
  g_mmx_buster_damage_cap[slot] = target;
  g_mmx_buster_damage_cap_valid[slot] = 1;

  return target;
}

static void mmx_buster_damage_reset(void) {
  g_mmx_buster_damage_active = 0;
  g_mmx_buster_damage_value = 0;
  for (unsigned i = 0; i < kSlotCount; i++) {
    g_mmx_buster_damage_cap[i] = 0;
    g_mmx_buster_damage_cap_valid[i] = 0;
  }
  cpu_set_wram_write8_filter_hook(NULL);
}

static void mmx_buster_damage_activate(void) {
  const char *configured = snes_mod_runtime_committed_option_value_c(
      MMX_BUSTER_DAMAGE_PACKAGE,
      MMX_BUSTER_DAMAGE_FEATURE,
      MMX_BUSTER_DAMAGE_OPTION);
  const char *env = getenv("MMX_BUSTER_DAMAGE");
  g_mmx_buster_damage_value = mmx_buster_damage_parse(
      env && env[0] ? env : configured, 255);
  g_mmx_buster_damage_active = 1;
  cpu_set_wram_write8_filter_hook(mmx_buster_damage_filter);
}

SNES_MOD_CONSTRUCTOR(mmx_register_buster_damage_plugin) {
  (void)snes_mod_register_reset_callback(mmx_buster_damage_reset);
  (void)snes_mod_register_activation_plugin(MMX_BUSTER_DAMAGE_PLUGIN,
                                            mmx_buster_damage_activate);
}
