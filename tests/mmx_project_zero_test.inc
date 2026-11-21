#include "mmx_project_zero.h"

static int project_zero_jump_height(bool coop) {
  int start[2] = {0}, peak[2] = {0};
  for (unsigned tick = 0; tick < 60; ++tick) {
    if (coop) {
      MmxCoopState state = MmxCoopGetState();
      for (unsigned seat = 0; seat < 2; ++seat) {
        const uint8_t *body = state.players[seat].body;
        int y = body[8] | body[9] << 8;
        if (tick == 0 || y < peak[seat]) peak[seat] = y;
        if (tick == 0) start[seat] = y;
      }
      coop_frames(1, tick < 28 ? SNES_PAD_B : 0, tick < 28 ? SNES_PAD_B : 0);
    } else {
      int y = g_ram[0xbb0] | g_ram[0xbb1] << 8;
      if (tick == 0 || y < peak[0]) peak[0] = y;
      if (tick == 0) start[0] = y;
      frame(tick < 28 ? SNES_PAD_B : 0);
    }
  }
  if (coop) {
    printf("Co-op jump heights: X=%d, Zero=%d\n", start[0] - peak[0], start[1] - peak[1]);
    check(start[0] - peak[0] > 20 && start[1] - peak[1] > start[0] - peak[0],
        "only the Zero seat gets the donor jump during co-op");
  }
  return start[0] - peak[0];
}

static void project_zero_checks(const char *rom, const char *ips) {
  const bool coop = getenv("MMX_PROJECT_ZERO_COOP") != NULL;
  const char *package = coop ? "megaman-x.coop" : "megaman-x.character.zero";
  const char *feature = coop ? "coop" : "zero";
  const char *root = getenv("MMX_PROJECT_ZERO_CATALOG");
  check(root && snes_mod_runtime_initialize_c(root, "megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),
      "Project Zero catalog initializes");
  RecompLauncherCGameInfo info = {.mods = snes_mod_runtime_launcher_provider_c()};
  MmxNetplayConfigureLauncher(&info);
  const RecompLauncherCModProvider *p = info.mods;
  check(p->feature_enable(p->ctx, package, feature, 1) &&
      p->feature_set_option(p->ctx, package, feature, "behavior", "x1"),
      "launcher offers X1 Zero");
  check(p->feature_resource_set_path(p->ctx, package, feature, "project-zero-ips", "") &&
      !p->commit(p->ctx, rom) && strstr(p->last_error(p->ctx), "Project Zero v1.0 IPS"),
      "missing IPS is reported");
  check(p->feature_resource_set_path(p->ctx, package, feature, "project-zero-ips", rom) &&
      !p->commit(p->ctx, rom), "a wrong IPS source is rejected");
  check(p->feature_resource_set_path(p->ctx, package, feature, "project-zero-ips", ips) &&
      p->commit(p->ctx, rom), "verified IPS launches without an X3 ROM");

  const unsigned stage_data = 0x376f7;
  uint8_t stock[0x100];
  memcpy(stock, g_snes->cart->rom + stage_data, sizeof(stock));
  snes_mod_runtime_activate_plugins_c();
  check(MmxZeroUsesProjectAssets() && MmxCoopEnabled() == coop, "trusted plugin activates X1 Zero");
  check(g_snes->cart->romImageSize == 0x180000 &&
      !memcmp(stock, g_snes->cart->rom + stage_data, sizeof(stock)),
      "game still runs with stock cartridge data");

  hitbox_boot();
  zero_replay(65);
  check(g_ram[0xd1] == 2 && g_ram[0x1f7a] == 0 && (g_ram[0xbd3] & 4),
      "ordinary Highway route reaches the ground");
  if (coop) {
    MmxCoopState state = MmxCoopGetState();
    check(state.initialized && state.players[1].character == MMX_COOP_ZERO &&
        state.players[1].status == MMX_COOP_ALIVE, "co-op P2 joins as Zero");
    project_zero_jump_height(true);
    check(MmxRendererDraw(s_composed_pixels, (MmxRenderView){256,0,4.0/3.0}, false),
        "co-op draws both players on the Highway");
    return;
  }

  check(!MmxZeroActive(), "Add Zero starts as X by default");
  unsigned x_jump = project_zero_jump_height(false);
  zero_replay(15);
  frame(SNES_PAD_SELECT);
  zero_replay(120);
  check(MmxZeroActive() && !MmxZeroSwapping(), "Select swaps X for X1 Zero");
  unsigned zero_jump = project_zero_jump_height(false);
  printf("Solo jump heights: X=%u, Zero=%u\n", x_jump, zero_jump);
  check(x_jump > 20 && zero_jump > x_jump, "the Project Zero jump is higher than X's");
  zero_replay(15);
  frame(SNES_PAD_Y);
  check(zero_projectiles(0) || zero_projectiles(1), "X1 Zero fires the native buster");
  int player_y = g_ram[0xbb0] | g_ram[0xbb1] << 8;
  int shot_y = g_ram[0x1230] | g_ram[0x1231] << 8;
  check(shot_y - player_y == -10, "standing buster shot uses the donor muzzle height");
  zero_replay(15);
  check(MmxRendererDraw(s_composed_pixels, (MmxRenderView){256,0,4.0/3.0}, false),
      "X1 Zero draws on the Highway");
  const uint32_t *stock_pixels = MmxRendererStockFrame();
  unsigned changed = 0;
  if (stock_pixels)
    for (unsigned i = 0; i < 256 * 224; ++i)
      changed += stock_pixels[i] != s_composed_pixels[i];
  check(changed > 100, "composited frame uses the imported character pixels");
  hitbox_bmp(s_composed_pixels, 256, "highway");
  check(RtlSaveSnapshot("project-zero.state") && RtlLoadSnapshot("project-zero.state") &&
      MmxZeroActive(), "current-build snapshot restores X1 Zero");
  frame(SNES_PAD_SELECT);
  zero_replay(120);
  check(!MmxZeroActive() && !MmxZeroSwapping() &&
      g_snes->cart->rom[0x339c6] == 5, "swapping back restores X's native jump");
}
