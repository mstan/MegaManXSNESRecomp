#include "mod_runtime.h"
#include <stdlib.h>
#include "mmx_coop.h"
#include "mmx_hitbox_overlay.h"

static int g_mmx_tier2_diagnostics_active;

static void mmx_tier2_diagnostics_reset(void) {
  g_mmx_tier2_diagnostics_active = 0;
  MmxCoopSetDiagnosticsEnabled(false);
  MmxHitboxOverlaySetEnabled(false);
}

/* Netplay diagnostics ride on the same switch: snesrecomp writes
 * saves/netplay/net_diag.jsonl (transport, ICE state, host/STUN/TURN path,
 * selected candidates, admit stalls) when SNES_NET_DIAG is set. It reads the
 * variable once, on the first netplay frame, so it is left set for the rest
 * of the process: a netplay plan that drops this developer mod still logs the
 * match it was enabled for. An explicit SNES_NET_DIAG is never overridden. */
static void mmx_enable_netplay_diagnostics(void) {
  const char *existing = getenv("SNES_NET_DIAG");
  if (existing && existing[0]) return;
#ifdef _WIN32
  _putenv_s("SNES_NET_DIAG", "1");
#else
  setenv("SNES_NET_DIAG", "1", 0);
#endif
}

static void mmx_tier2_diagnostics_activate(void) {
  g_mmx_tier2_diagnostics_active = 1;
  mmx_enable_netplay_diagnostics();
}
static void mmx_coop_diagnostics_activate(void) {
  MmxCoopSetDiagnosticsEnabled(true);
}
static void mmx_hitbox_overlay_activate(void) {
  MmxHitboxOverlaySetEnabled(true);
}

int mmx_tier2_diagnostics_enabled(void) {
  return g_mmx_tier2_diagnostics_active;
}

SNES_MOD_CONSTRUCTOR(mmx_register_tier2_diagnostics_plugin) {
  (void)snes_mod_register_reset_callback(mmx_tier2_diagnostics_reset);
  (void)snes_mod_register_activation_plugin(
      "megaman-x.tier2-diagnostics", mmx_tier2_diagnostics_activate);
  (void)snes_mod_register_activation_plugin(
      "megaman-x.coop-diagnostics", mmx_coop_diagnostics_activate);
  /* Reads RAM and the collision ROM, draws outlines, writes nothing. */
  (void)snes_mod_register_presentation_plugin(
      "megaman-x.hitbox-overlay", mmx_hitbox_overlay_activate);
}
