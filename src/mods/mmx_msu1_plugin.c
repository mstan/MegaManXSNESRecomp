#include "mod_runtime.h"
#include "common_rtl.h"
#include "snes/msu1.h"
#include "../mmx_msu1.h"
#include <string.h>

#include <stdbool.h>
#include <stdint.h>

#include "snes/interp_bridge.h"

#define MMX_MSU_PLUGIN "megaman-x.msu1"

#define MMX_MSU_VOLUME_FULL 0x50
#define MMX_MSU_VOLUME_DUCK 0x20
#define MMX_MSU_FADE_STEP   0x01

#define MSU_STATUS_ERROR 0x08
#define MSU_STATUS_PLAY  0x10
#define MSU_CONTROL_PLAY 0x01
#define MSU_CONTROL_REPEAT 0x02

enum {
  kFadeIdle = 0,
  kFadeOut = 1,
  kFadeIn = 2,
};

static int g_mmx_msu1_active;
static uint8_t g_mmx_msu1_fade_state;
static uint8_t g_mmx_msu1_fade_volume;
/* Loading before the local music mod is activated must not lose the restored
 * timeline. A subsequent native song command supersedes this bookmark. */
static MmxMsu1State g_mmx_msu1_pending;

MmxMsu1State MmxMsu1GetState(void) {
  MmxMsu1State state = {0};
  if (RtlIsRollbackSave()) return state;
  if (g_mmx_msu1_pending.valid) return g_mmx_msu1_pending;
  if (!g_mmx_msu1_active || !msu1_enabled()) return state;
  state.valid = 1;
  state.fade_state = g_mmx_msu1_fade_state;
  state.fade_volume = g_mmx_msu1_fade_volume;
  msu1_capture_playback(state.playback);
  return state;
}

bool MmxMsu1ValidState(const MmxMsu1State *state) {
  if (!state || state->valid > 1 || state->reserved) return false;
  if (!state->valid) {
    const MmxMsu1State empty = {0};
    return memcmp(state, &empty, sizeof(empty)) == 0;
  }
  return state->fade_state <= kFadeIn &&
      state->fade_volume <= MMX_MSU_VOLUME_FULL &&
      msu1_playback_state_valid(state->playback);
}

void MmxMsu1StateLoaded(const MmxMsu1State *state) {
  if (RtlIsRollbackLoad() || !MmxMsu1ValidState(state)) return;
  g_mmx_msu1_pending = *state;
  if (!state->valid || !g_mmx_msu1_active || !msu1_enabled()) return;
  /* The shared provider reopens through the current local pack, retaining
   * user preferences and exact source cursor/resampling phase. */
  (void)msu1_restore_playback(state->playback);
  g_mmx_msu1_fade_state = state->fade_state;
  g_mmx_msu1_fade_volume = state->fade_volume;
  memset(&g_mmx_msu1_pending, 0, sizeof(g_mmx_msu1_pending));
}


static int mmx_msu1_track_loops(uint8_t track) {
  switch (track) {
    case 0x00:
    case 0x0f:
    case 0x11:
    case 0x12:
    case 0x17:
    case 0x1e:
      return 0;
    default:
      return 1;
  }
}

int mmx_msu1_music_command(uint8_t command) {
  /*
   * Called unconditionally from the MSU1-MUSIC and MSU1-STAGE hooks that
   * apply_overrides.py bakes into the generated C, so this gate is what
   * makes a disabled mod stock. A non-zero return tells the generated hook to
   * RTS before posting the SPC music command, so returning "claimed" while the
   * chip is inert silently deletes the music: msu1_write is a no-op and
   * msu1_read reports 0, so the MSU_STATUS_ERROR fallback below never trips.
   * Same contract the widescreen hooks follow (MmxWsSpawnWide() etc.): a
   * baked-in hook must be inert when its mod is off.
   */
  if (command >= 0x10 && command <= 0x30)
    memset(&g_mmx_msu1_pending, 0, sizeof(g_mmx_msu1_pending));
  if (!g_mmx_msu1_active || !msu1_enabled())
    return 0;

  if (command < 0x10 || command > 0x30)
    return 0;

  const uint8_t track = (uint8_t)(command - 0x10);
  const uint8_t control = (uint8_t)(MSU_CONTROL_PLAY |
      (mmx_msu1_track_loops(track) ? MSU_CONTROL_REPEAT : 0));

  msu1_write(0x2004, track);
  msu1_write(0x2005, 0);
  if (msu1_read(0x2000) & MSU_STATUS_ERROR) {
    msu1_write(0x2007, 0);
    return 0;
  }

  g_mmx_msu1_fade_state = kFadeIdle;
  g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_FULL;
  msu1_write(0x2006, MMX_MSU_VOLUME_FULL);
  msu1_write(0x2007, control);
  return 1;
}

static void mmx_msu1_tick(void) {
  if (!g_mmx_msu1_active || !msu1_enabled() ||
      g_mmx_msu1_fade_state == kFadeIdle)
    return;

  if (g_mmx_msu1_fade_state == kFadeOut) {
    if (g_mmx_msu1_fade_volume <= MMX_MSU_FADE_STEP) {
      g_mmx_msu1_fade_volume = 0;
      g_mmx_msu1_fade_state = kFadeIdle;
      msu1_write(0x2006, 0);
      msu1_write(0x2007, 0);
      return;
    }
    g_mmx_msu1_fade_volume =
        (uint8_t)(g_mmx_msu1_fade_volume - MMX_MSU_FADE_STEP);
    msu1_write(0x2006, g_mmx_msu1_fade_volume);
    return;
  }

  if (g_mmx_msu1_fade_volume >= MMX_MSU_VOLUME_FULL - MMX_MSU_FADE_STEP) {
    g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_FULL;
    g_mmx_msu1_fade_state = kFadeIdle;
    msu1_write(0x2006, MMX_MSU_VOLUME_FULL);
    return;
  }
  g_mmx_msu1_fade_volume =
      (uint8_t)(g_mmx_msu1_fade_volume + MMX_MSU_FADE_STEP);
  msu1_write(0x2006, g_mmx_msu1_fade_volume);
}

static int mmx_msu1_apu_write(uint16_t reg, uint8_t value) {
  if (!g_mmx_msu1_active || !msu1_enabled() || reg != 0x2140)
    return 0;

  /*
   * MMX waits for the SPC/APU port echo after writes to $2140. Observe the
   * music protocol and drive MSU-1, but always allow the APU write through.
   */
  switch (value) {
    case 0xf5:
      /* A capsule's $F6 fade finishes by stopping playback. The selected
       * track and cursor still exist: $F5 resumes them even with PLAY clear.
       * A missing PCM must continue to use the original SPC protocol. */
      if (msu1_read(0x2000) & MSU_STATUS_ERROR)
        return 0;
      msu1_write(0x2007, MSU_CONTROL_PLAY | MSU_CONTROL_REPEAT);
      g_mmx_msu1_fade_state = kFadeIn;
      g_mmx_msu1_fade_volume = 0;
      msu1_write(0x2006, 0);
      return 0;
    case 0xf6:
      if (msu1_read(0x2000) & MSU_STATUS_PLAY) {
        g_mmx_msu1_fade_state = kFadeOut;
        g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_FULL;
      } else {
        msu1_write(0x2007, 0);
      }
      return 0;
    case 0xfe:
      g_mmx_msu1_fade_state = kFadeIdle;
      g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_FULL;
      msu1_write(0x2006, MMX_MSU_VOLUME_FULL);
      return 0;
    case 0xff:
      g_mmx_msu1_fade_state = kFadeIdle;
      g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_DUCK;
      msu1_write(0x2006, MMX_MSU_VOLUME_DUCK);
      return 0;
    default:
      return 0;
  }
}

static void mmx_msu1_stage_music_hook(CpuState *cpu, uint32_t pc24) {
  (void)pc24;
  if (!g_mmx_msu1_active || !msu1_enabled())
    return;

  /*
   * DarkShock's legacy patch replaces the level-load JMP $87B0 with a JMP to
   * its MSU routine, which then RTSes to the caller when MSU handled the
   * command. MMX's scheduler commonly reaches this site through the
   * interpreter tier, so mirror that exact return shape here.
   * Source: https://github.com/mlarouche/MegamanX-MSU1
   */
  if (mmx_msu1_music_command((uint8_t)(cpu->A & 0xff))) {
    cpu->S = (uint16_t)(cpu->S + 1);
    const uint16_t rpcl = (uint16_t)cpu_read8(cpu, 0x00, cpu->S);
    cpu->S = (uint16_t)(cpu->S + 1);
    const uint16_t rpch = (uint16_t)cpu_read8(cpu, 0x00, cpu->S);
    const uint32_t rpc = (uint32_t)((((rpch << 8) | rpcl) + 1) & 0xffffu);
    interp_bridge_pre_opcode_redirect(((uint32_t)cpu->PB << 16) | rpc);
  }
}

static void mmx_msu1_reset(void) {
  g_mmx_msu1_active = 0;
  g_mmx_msu1_fade_state = kFadeIdle;
  g_mmx_msu1_fade_volume = 0;
}

static void mmx_msu1_activate(void) {
  g_mmx_msu1_active = 1;
  g_mmx_msu1_fade_state = kFadeIdle;
  g_mmx_msu1_fade_volume = MMX_MSU_VOLUME_FULL;
  if (g_mmx_msu1_pending.valid)
    MmxMsu1StateLoaded(&g_mmx_msu1_pending);
  (void)snes_mod_register_frame_callback(mmx_msu1_tick);
  (void)snes_mod_register_apu_write_callback(mmx_msu1_apu_write);
  interp_bridge_set_pre_opcode_hook(0x809a2d, mmx_msu1_stage_music_hook);
}

SNES_MOD_CONSTRUCTOR(mmx_register_msu1_plugin) {
  (void)snes_mod_register_reset_callback(mmx_msu1_reset);
  (void)snes_mod_register_activation_plugin(MMX_MSU_PLUGIN,
                                            mmx_msu1_activate);
}
