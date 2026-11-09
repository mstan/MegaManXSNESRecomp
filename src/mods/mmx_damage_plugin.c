#include "mmx_damage.h"
#include "mod_runtime.h"
#include "snes/interp_bridge.h"
#include <stdint.h>

extern uint8_t g_ram[0x20000];
extern void MmxZeroRegisterHooks(void);

static void received_hook(CpuState *cpu, uint32_t pc) {
  bool contact = (pc & 65535) == 0x9d59;
  unsigned original = g_ram[0];
  bool lethal = (contact ? g_ram[(cpu->D + 0x26) & 65535] : g_ram[0xbce]) >= 127;
  unsigned damage = MmxDamageReceived(g_ram, original, lethal);
  if (damage == original) return;
  /* Post-SBC / pre-STA: keep guest instruction timing, carry/overflow and
   * both accumulator bytes identical to a native SBC using the new operand. */
  unsigned hp = g_ram[0xbcf] & 127, result;
  if (cpu->_flag_D) {
    unsigned complement = damage ^ 255;
    int decimal = (hp & 15) + (complement & 15) + 1;
    if (decimal < 16) decimal = (decimal - 6) & (decimal < 6 ? 15 : 31);
    decimal += (hp & 240) + (complement & 240);
    cpu->_flag_V = ((hp & 128) == (complement & 128)) && ((complement & 128) != (decimal & 128));
    if (decimal < 256) decimal -= 96;
    cpu->_flag_C = decimal > 255; result = (unsigned)decimal & 255;
  } else {
    result = (hp - damage) & 255;
    cpu->_flag_C = hp >= damage;
    cpu->_flag_V = ((hp ^ damage) & (hp ^ result) & 128) != 0;
  }
  cpu->A = (cpu->A & 0xff00) | result;
  cpu->_flag_Z = result == 0; cpu->_flag_N = (result & 128) != 0;
  cpu->P = (cpu->P & ~0xc3) | cpu->_flag_C | (cpu->_flag_Z ? 2 : 0) |
      (cpu->_flag_V ? 64 : 0) | (cpu->_flag_N ? 128 : 0);
}

static unsigned multiplier(const char *package) {
  char value[32] = {0};
  snes_mod_runtime_feature_option_value_c(package, "damage", "multiplier", value, sizeof(value));
  return MmxDamageParseMultiplier(value);
}
static void activate_dealt(void) {
  MmxDamageSetDealt(multiplier("megaman-x.gameplay.damage-dealt"));
  MmxZeroRegisterHooks();
}
static void activate_received(void) {
  MmxDamageSetReceived(multiplier("megaman-x.gameplay.damage-received"));
  interp_bridge_add_pre_opcode_hook(0x849d59, received_hook);
  interp_bridge_add_pre_opcode_hook(0x849f62, received_hook);
}
SNES_MOD_CONSTRUCTOR(mmx_register_damage_plugins) {
  snes_mod_register_reset_callback(MmxDamageReset);
  snes_mod_register_activation_plugin("megaman-x.damage-dealt", activate_dealt);
  snes_mod_register_activation_plugin("megaman-x.damage-received", activate_received);
}
