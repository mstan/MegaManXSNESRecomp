#include "mmx_hadouken.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "mmx_mod_constructor.h"
#include "snes/interp_bridge.h"
#include "common_rtl.h"

static bool enabled;
static void hook(CpuState *cpu, uint32_t pc);
void MmxHadoukenSetEnabled(bool active) {
  enabled=active;
  if(active) interp_bridge_set_pre_opcode_hook(0x01a062,hook);
}
void MmxHadoukenInput(uint8_t r[0x20000]) {
  bool pressed=(r[0xbe3]&0x40)!=0;
  if (!enabled || MmxZeroActive() || r[0xd1]!=2 || r[0xd2]!=4 || r[0xd3]!=4 ||
      !pressed || r[0xbdb] || MmxWeaponsGetState().weapon) return;
  /* Complete the input recognizer only. $81:A0EA still checks acquisition,
   * full health, riding/attack restrictions and the native eligible actions. */
  r[0xc27]=6; r[0xc28]=20;
  r[0xbe3]|=0x40;
}
static void hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (cpu->D==0xba8) MmxHadoukenInput(g_ram);
}
static void activate(void) {
  MmxHadoukenSetEnabled(true);
}
static void reset(void) { enabled=false; }
SNES_MOD_CONSTRUCTOR(mmx_register_hadouken_plugin) {
  snes_mod_register_reset_callback(reset);
  snes_mod_register_activation_plugin("megaman-x.hadouken",activate);
}
