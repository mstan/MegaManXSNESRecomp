/* ROM-backed checks for the MMX adapters and the real shared desktop host.
 * The caller supplies an empty working directory; only slot 12 is used. */
#define MMX_DESKTOP_ENTRY MmxDesktopMain
#include "desktop/host_main.c"
#include MMX_GAME_MAIN
#include "common/launcher_binds.h"

static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
  printf("ok: %s\n", what);
}
static void frame(unsigned input) {
  MmxBeforeFrame();
  RtlRunFrame(input | (1u << 30));
  CaptureSimulationFrame(1);
}
static void replay(int count) {
  for (int i = 0; i < count; ++i) frame(i < count / 2 ? SNES_PAD_RIGHT : 0);
}
static void same(const void *a, size_t an, const void *b, size_t bn, const char *what) {
  if (an != bn || memcmp(a, b, an)) {
    size_t i = 0;
    while (i < an && i < bn && ((const uint8 *)a)[i] == ((const uint8 *)b)[i]) ++i;
    fprintf(stderr, "first difference at %zu / %zu / %zu\n", i, an, bn);

  }
  check(an && an == bn && !memcmp(a, b, an), what);
}
int main(int argc, char **argv) {
  check(argc == 2 || argc == 3, "ROM supplied");
  SDL_SetMainReady();
  check(snesrecomp_sdl_init(SDL_INIT_EVENTS), "SDL initializes");
  g_audio_mutex = SDL_CreateMutex();
  static const SnesDesktopHostGame game = {
    .native_widescreen = 0, .state_menu_hotkeys = 1,
    .prepare_frame = MmxPrepareFrame, .begin_sim_frame = MmxBeginFrame,
    .end_sim_frame = MmxEndFrame, .draw_frame = MmxDrawFrame,
  };
  g_game = &game;
  ConfigUseStateMenuDefaults();
  FILE *f = fopen("config.ini", "w");
  fputs("[KeyMap]\nLoad = F1,F2,F3,F4,F5,F6,F7,F8,F9,F10\n", f); fclose(f);
  ParseConfigFile("config.ini");
  check(FindCmdForSdlKey(SDLK_F7, 0) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F8, 0) == kKeys_Rewind &&
        FindCmdForSdlKey(SDLK_F11, 0) == kKeys_Load + 6 &&
        FindCmdForSdlKey(SDLK_F12, 0) == kKeys_Load + 7, "F7/F8 and legacy config migration");
  HandleInput(SDLK_F7, 0, true); HandleInput(SDLK_F8, 0, true);
  check(g_savestate_menu_hotkey && g_rewind_hotkey, "host dispatches F7/F8");
  g_savestate_menu_hotkey = g_rewind_hotkey = 0;
  LauncherModel model = {0};
  g_launcher_config_path = "config.ini";
  launcher_binds_set_hotkey(&model, LNG_HK_SAVE_STATE_MENU, SDLK_F9, KMOD_CTRL);
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, SDLK_F9, 0);
  ConfigReloadKeyMap("config.ini");
  check(FindCmdForSdlKey(SDLK_F9, KMOD_CTRL) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F9, 0) == kKeys_Rewind, "launcher rebind and slot collision");
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, 0, 0);
  ConfigReloadKeyMap("config.ini");
  check(!FindCmdForSdlKey(SDLK_F8, 0) &&
        FindCmdForSdlKey(SDLK_F9, 0) != kKeys_Rewind, "launcher clear persists");
  g_launcher_config_path = NULL;
  f = fopen(argv[1], "rb"); check(f != NULL, "ROM opens");
  fseek(f, 0, SEEK_END); long rom_size = ftell(f); rewind(f);
  uint8 *rom = malloc(rom_size);
  check(fread(rom, 1, rom_size, f) == (size_t)rom_size, "ROM reads"); fclose(f);
  if ((rom_size & 0x7fff) == 512) {
    rom_size -= 512;
    memmove(rom, rom + 512, rom_size);
  }
  g_config.new_renderer = true; g_config.widescreen = true;
  g_mmx_custom_renderer = true;
  MmxRendererSetRom(rom, rom_size);
  g_last_drawable_width = 1280; g_last_drawable_height = 720;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer;
  RtlRegisterGame(&kMmxGameInfo);
  check(SnesInit(rom, rom_size) != NULL, "game initializes");
  g_spc_player = SmwSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
  MkDir("saves");
  size_t cap = 2u * 1024u * 1024u;
  uint8 *start = malloc(cap), *expected = malloc(cap), *actual = malloc(cap);
  if (argc == 3) {
    check(RtlLoadSnapshot("saves/save11.sav"), "file loads in new process");
    replay(10);
    size_t n = RtlSaveSnapshotToMemory(actual, cap);
    f = fopen("expected.bin", "rb"); check(f != NULL, "reference opens");
    size_t en = fread(expected, 1, cap, f); fclose(f);
    same(expected, en, actual, n, "new-process replay");
    puts("MMX STATE CHECKS PASSED"); return 0;
  }
  for (int phase = 0; phase < 2; ++phase) {
    for (int i = 0; i < 600; ++i) frame(i == 200 || i == 350 ? SNES_PAD_START : 0);
    size_t n = RtlSaveSnapshotToMemory(start, cap);
    replay(30); size_t en = RtlSaveSnapshotToMemory(expected, cap);
    check(RtlLoadSnapshotFromMemory(start, n), "memory load");
    size_t an = RtlSaveSnapshotToMemory(actual, cap);
    same(start, n, actual, an, "complete immediate restore");
    replay(30); an = RtlSaveSnapshotToMemory(actual, cap);
    same(expected, en, actual, an, "30-frame deterministic replay");
  }
  snes_savestate_menu_poll_open(SNES_PAD_SELECT | SNES_PAD_R);
  check(snes_savestate_menu_is_open(), "save browser opens");
  uint64 clock = g_cpu.master_cycles;
  snes_savestate_menu_handle_key(SDLK_EQUALS, 0);
  SDL_Event key = {0};
  key.type = SDL_KEYDOWN;
#if SNESRECOMP_SDL3
  key.key.key = SDLK_s;
#else
  key.key.keysym.sym = SDLK_s;
#endif
  SDL_PushEvent(&key);
  bool running = true;
  PumpOverlayEvents(&running, snes_savestate_menu_handle_key);
  check(OverlayNavInputs() & SNES_PAD_X, "browser receives keyboard save action");
  snes_savestate_menu_poll_nav(OverlayNavInputs(), 1);
  HandleInput(SDLK_s, 0, false);
  check(clock == g_cpu.master_cycles, "saving does not advance guest");
  snes_savestate_menu_poll_nav(0, 2);
  snes_savestate_menu_poll_nav(SNES_PAD_B, 3);
  check(!snes_savestate_menu_is_open(), "B closes save browser");
  replay(10); size_t en = RtlSaveSnapshotToMemory(expected, cap);
  f = fopen("expected.bin", "wb"); fwrite(expected, 1, en, f); fclose(f);
  snes_rewind_configure();
  for (int i = 0; i < 12; ++i) { frame(0); snes_rewind_note_frame(); }
  size_t n = RtlSaveSnapshotToMemory(start, cap);
  for (int i = 0; i < 6; ++i) { frame(SNES_PAD_RIGHT); snes_rewind_note_frame(); }
  check(snes_rewind_open(), "rewind opens");
  snes_rewind_step(-1); /* default interval is six simulated frames */
  snes_rewind_commit();
  size_t an = RtlSaveSnapshotToMemory(actual, cap);
  same(start, n, actual, an, "rewind restores selected frame");
  snes_rewind_shutdown();
  /* Both meanings of the old game chunk v2 must load without reading EOF.
   * File and memory use the same format discrimination. */
  n = RtlSaveSnapshotToMemory(start, cap);
  size_t chunk = 0;
  for (size_t i = n - 8; i > 8; --i) {
    uint32_t magic; memcpy(&magic, start + i, 4);
    if (magic == 0x4d4d5854u) { chunk = i; break; }
  }
  check(chunk != 0, "game chunk located");
  uint32_t version = 2; memcpy(start + chunk + 4, &version, 4);
  check(RtlLoadSnapshotFromMemory(start, n), "shared-host v2 with execution tail loads");
  f = fopen("main-v2.sav", "wb"); fwrite(start, 1, n, f); fclose(f);
  check(RtlLoadSnapshot("main-v2.sav"), "shared-host v2 file loads");
  check(RtlLoadSnapshotFromMemory(start, chunk + 464), "adaptive v2 without tail loads");
  f = fopen("adaptive-v2.sav", "wb"); fwrite(start, 1, chunk + 464, f); fclose(f);
  check(RtlLoadSnapshot("adaptive-v2.sav"), "adaptive v2 file loads");
  replay(10);
  check(g_mmx_custom_renderer && g_mmx_custom_view.width > 256, "adaptive remains active");
  puts("MMX STATE CHECKS PASSED");
  return 0;
}
