#include "mod_runtime.h"
#include "host_paths.h"
#include "recomp_launcher.h"
#include "snes/interp_bridge.h"
#include "mmx_password_save.h"
#include <stdio.h>
#include <string.h>

#define PASSWORD_PACKAGE "megaman-x.enhancement.password-save"
extern uint8_t g_ram[0x20000];
static int active;

/* Used by both the interpreter callback and generated native blocks. Never
 * change registers, flags, cycles, control flow, or guest cartridge memory. */
void MmxPasswordHook(CpuState *cpu, uint32_t pc) {
  if (!active || cpu->D != 0x1e48) return;
  switch (pc & 0x7fffff) {
    case 0x00ef25: MmxPasswordPrefill(g_ram); break;
    case 0x00f05e: MmxPasswordCapture(g_ram); break;
  }
}

static void reset(void) {
  active = 0;
  MmxPasswordClose();
}

static void activate(void) {
  char path[4096];
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource = {0};
  if (provider && provider->feature_resource_get &&
      provider->feature_resource_get(provider->ctx, PASSWORD_PACKAGE, "password_save", 0, &resource) &&
      resource.path[0]) {
    /* The existing Mods resource picker persists an absolute filename. */
    if (strlen(resource.path) >= sizeof(path)) return;
    snprintf(path, sizeof(path), "%s", resource.path);
  } else if (!snesrecomp_exe_dir_path("mmx-password.srm", path, sizeof(path))) {
    fprintf(stderr, "[mmx-password] Cannot locate executable directory\n");
    return;
  }
  if (!MmxPasswordOpen(path)) return;
  active = 1;
  interp_bridge_set_pre_opcode_hook(0x00ef25, MmxPasswordHook);
  interp_bridge_set_pre_opcode_hook(0x00f05e, MmxPasswordHook);
}

SNES_MOD_CONSTRUCTOR(mmx_register_password_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.password-save", activate);
}
