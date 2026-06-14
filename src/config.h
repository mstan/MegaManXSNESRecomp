#pragma once
#include "types.h"
#include <SDL_keycode.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,

  kKeys_ControlsP2,
  kKeys_ControlsP2_Last = kKeys_ControlsP2 + 11,

  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  bool extend_y;
  bool no_sprite_limits;
  bool display_perf_title;

  /* Oracle-build only. When false, main.c skips snes_oracle_init_default
   * and calls snes_oracle_set_disabled_by_game so the dispatcher refuses
   * every emu_* command with a structured warning naming the reason. For
   * MMX, this defaults to OFF in mmx.ini because the freeze repro path
   * is save-state load, which the from-boot oracle cannot follow — a
   * prior session wasted real time chasing false divergences before
   * noticing. See snes_oracle_backend.h header doc. */
  bool enable_snes9x_oracle;

  char *memory_buffer;
  const char *shader;

  bool enable_gamepad[2];
  int gamepad_deadzone;

  // Which players have keyboard controls
  uint8 has_keyboard_controls;
} Config;

enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);
// Persist the launcher-editable settings back into `filename` (or config.ini)
// with a surgical, comment-preserving in-place update. Called after the GUI
// launcher returns PLAY.
void WriteConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
