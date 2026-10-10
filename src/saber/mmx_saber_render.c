#include "mmx_saber_render.h"
#include "mmx_zero.h"
#include "mmx_saber_wave_runtime.h"

#include <string.h>

static uint16_t flash_palette[256];
static const MmxSaberWave *render_wave;

static void clear_overlay(MmxRenderPlayerOverlay *out) {
  if (out) memset(out, 0, sizeof(*out));
}

static unsigned color_luminance(uint16_t color) {
  unsigned red = color & 31, green = (color >> 5) & 31, blue = (color >> 10) & 31;
  return red * 299 + green * 587 + blue * 114;
}

/* Match oldsaber/saber-zero-variant:src/mmx_renderer.c:1138-1191. The
 * donor palette keeps its role ordering while native Zero supplies the
 * selected 16-entry charge palette. */
static void map_flash_palette(uint16_t *mapped, const uint16_t *palette,
                              uint16_t palette_count, const uint16_t *flash) {
  uint16_t donor_order[256], flash_order[16];
  unsigned donor_count = 0, groups = 0;
  if (!mapped || !palette || !palette_count || !flash) return;
  memcpy(mapped, palette, (size_t)palette_count * sizeof(*mapped));
  mapped[0] = 0;
  for (unsigned i = 1; i < palette_count; ++i) {
    unsigned j = donor_count;
    while (j && (color_luminance(palette[donor_order[j - 1]]) >
                     color_luminance(palette[i]) ||
                 (color_luminance(palette[donor_order[j - 1]]) ==
                      color_luminance(palette[i]) && donor_order[j - 1] > i))) {
      donor_order[j] = donor_order[j - 1];
      --j;
    }
    donor_order[j] = (uint16_t)i;
    ++donor_count;
  }
  for (unsigned i = 0; i < 16; ++i) {
    unsigned j = i;
    while (j && (color_luminance(flash[flash_order[j - 1]]) >
                     color_luminance(flash[i]) ||
                 (color_luminance(flash[flash_order[j - 1]]) ==
                      color_luminance(flash[i]) && flash_order[j - 1] > i))) {
      flash_order[j] = flash_order[j - 1];
      --j;
    }
    flash_order[j] = (uint16_t)i;
  }
  for (unsigned rank = 0; rank < donor_count;) {
    unsigned end = rank + 1;
    unsigned luminance = color_luminance(palette[donor_order[rank]]);
    while (end < donor_count &&
           color_luminance(palette[donor_order[end]]) == luminance) ++end;
    ++groups;
    rank = end;
  }
  for (unsigned group = 0, rank = 0; rank < donor_count; ++group) {
    unsigned end = rank + 1;
    unsigned luminance = color_luminance(palette[donor_order[rank]]);
    while (end < donor_count &&
           color_luminance(palette[donor_order[end]]) == luminance) ++end;
    unsigned flash_rank = groups > 1 ? group * 15 / (groups - 1) : 0;
    for (unsigned i = rank; i < end; ++i)
      mapped[donor_order[i]] = flash[flash_order[flash_rank]];
    rank = end;
  }
}

static bool resolve_snapshot(const MmxSaberAssets *assets,
                             MmxSaberAttackSnapshot snapshot,
                             const MmxZeroState *zero,
                             MmxRenderPlayerOverlay *out) {
  const MmxSaberAttack *attack;
  const MmxSaberAnimation *animation;
  const MmxSaberFrame *frame = NULL;
  uint16_t tick;

  clear_overlay(out);
  if (!out || !assets || snapshot.phase == SABER_PHASE_IDLE)
    return false;

  /* The attack kind remains the owner of timing/collision semantics, while
   * the snapshot animation ID selects that attack's visual donor.  Landing
   * exits through the central attack cleanup before any replacement visual
   * can be selected. */
  attack = MmxSaberAttackRecord(snapshot.kind, snapshot.index);
  if (!attack) return false;
  animation = MmxSaberAssetsAnimationById(assets, snapshot.anim_id);
  if (!animation || !animation->total_ticks) return false;

  /* Match oldsaber/saber-zero-variant:src/mmx_saber.c:1351-1355: an attack
   * can outlive its sidecar animation, so its final sidecar tick is held. */
  tick = snapshot.tick < animation->total_ticks ? snapshot.tick :
      (uint16_t)(animation->total_ticks - 1);
  for (uint16_t i = 0; i < animation->step_count; ++i) {
    const MmxSaberStep *step = MmxSaberAssetsAnimationStep(
        assets, snapshot.anim_id, i);
    if (!step) return false;
    if (tick < step->duration_ticks) {
      frame = MmxSaberAssetsFrameForStep(assets,
                                         snapshot.anim_id, i);
      break;
    }
    tick = (uint16_t)(tick - step->duration_ticks);
  }
  if (!frame || !frame->body.pixels || !frame->body.width ||
      !frame->body.height) return false;

  out->body.pixels = frame->body.pixels;
  out->body.width = frame->body.width;
  out->body.height = frame->body.height;
  out->body.origin_x = frame->body.origin_x;
  out->body.origin_y = frame->body.origin_y;
  out->blade.pixels = frame->blade.pixels;
  out->blade.width = frame->blade.width;
  out->blade.height = frame->blade.height;
  out->blade.origin_x = frame->blade.origin_x;
  out->blade.origin_y = frame->blade.origin_y;
  out->blade_layer = frame->blade_layer;
  out->palette = MmxSaberAssetsPalette(assets);
  out->palette_count = MmxSaberAssetsPaletteCount(assets);
  if (MmxZeroChargeFlashPaletteIndex(zero) >= 0) {
    map_flash_palette(flash_palette, out->palette, out->palette_count,
                      MmxZeroBodyColors(zero));
    out->palette = flash_palette;
  }
  /* The old compositor's mirror expression is the final left-facing bit:
   * oldsaber/saber-zero-variant:src/mmx_renderer.c:1595-1596. */
  out->facing_left = (snapshot.facing & 0x40) != 0 ^
      (animation->facing_xor != 0);
  out->active = out->palette && out->palette_count;
  if (!out->active) clear_overlay(out);
  return out->active;
}

bool MmxSaberRenderResolveSnapshot(const MmxSaberAssets *assets,
                                   MmxSaberAttackSnapshot snapshot,
                                   MmxRenderPlayerOverlay *out) {
  MmxZeroState zero = MmxZeroGetState();
  return resolve_snapshot(assets, snapshot, &zero, out);
}

bool MmxSaberRenderResolveRide(const MmxSaberAssets *assets,
                               const uint8_t *ram,
                               MmxRenderPlayerOverlay *out) {
  const MmxSaberAnimation *animation;
  const MmxSaberFrame *frame;
  unsigned pose;

  clear_overlay(out);
  if (!out || !assets || !ram || ram[0x0baa] != 0x2c)
    return false;

  animation = MmxSaberAssetsAnimationById(assets, 0x006b);
  if (!animation || !animation->step_count) return false;
  pose = ram[0x0bbf] & 0x7f;
  if (pose >= 23 || pose >= animation->step_count) pose = 0;
  frame = MmxSaberAssetsFrameForStep(assets, 0x006b, (uint16_t)pose);
  if (!frame || !frame->body.pixels || !frame->body.width ||
      !frame->body.height)
    return false;

  /* Ride frames are authored as complete cockpit canvases. Publish only the
   * body plane, with the ride palette and native player facing; the renderer
   * supplies the normal player anchor and priority for this pilot submission. */
  out->body.pixels = frame->body.pixels;
  out->body.width = frame->body.width;
  out->body.height = frame->body.height;
  out->body.origin_x = frame->body.origin_x;
  out->body.origin_y = frame->body.origin_y;
  out->palette = MmxSaberAssetsPalette(assets);
  out->palette_count = MmxSaberAssetsPaletteCount(assets);
  /* Ride art is authored in the opposite horizontal orientation from the
   * native pilot/armor facing, like the Saber attack sheets. */
  out->facing_left = (ram[0x0bb9] & 0x40) != 0 ^
      (animation->facing_xor != 0);
  out->active = out->palette && out->palette_count;
  if (!out->active) clear_overlay(out);
  return out->active;
}

bool MmxSaberRenderResolve(const MmxSaberAssets *assets,
                           const MmxZeroState *zero,
                           MmxRenderPlayerOverlay *out) {
  MmxZeroState live;
  if (!zero) {
    live = MmxZeroGetState();
    zero = &live;
  }
  return resolve_snapshot(assets, MmxSaberAttackGetSnapshot(), zero, out);
}

bool MmxSaberRenderResolveWaveSnapshot(const MmxSaberWave *wave,
                                       uint8_t age, int16_t world_x,
                                       int16_t world_y, bool facing_left,
                                       MmxRenderWorldSprite *out) {
  const MmxSaberWaveFrame *frame;
  unsigned total_ticks, step_count, step;

  if (out) memset(out, 0, sizeof(*out));
  if (!out || !wave) return false;
  total_ticks = MmxSaberWaveTotalTicks(wave);
  step_count = MmxSaberWaveStepCount(wave);
  if (!total_ticks || !step_count) return false;

  /* Match the old renderer's wave row: age wraps at the 32-tick sidecar
   * cycle, then the sidecar frame advances every two ticks.
   * oldsaber/saber-zero-variant:src/mmx_renderer.c:987-988. */
  step = ((unsigned)age % total_ticks) / 2u % step_count;
  frame = MmxSaberWaveFrameForStep(wave, (uint16_t)step);
  if (!frame || !frame->pixels || !frame->width || !frame->height)
    return false;

  out->pixels = frame->pixels;
  out->width = frame->width;
  out->height = frame->height;
  out->origin_x = frame->origin_x;
  out->origin_y = frame->origin_y;
  out->world_x = world_x;
  out->world_y = world_y;
  out->facing_left = facing_left;
  out->palette = MmxSaberWavePalette(wave);
  out->palette_count = MmxSaberWavePaletteCount(wave);
  out->z = 0xa680;
  return out->palette && out->palette_count;
}

void MmxSaberRenderSetWave(const MmxSaberWave *wave) {
  render_wave = wave;
}

unsigned MmxSaberRenderProvideWorldSprites(MmxRenderWorldSprite *out,
                                           unsigned max) {
  MmxSaberWaveRuntimeLiveWave live[8];
  unsigned live_count, count = 0;
  if (!render_wave || !out || !max) return 0;
  live_count = MmxSaberWaveRuntimeLiveWaves(live,
                                             max < 8 ? max : 8);
  for (unsigned i = 0; i < live_count; ++i) {
    if (MmxSaberRenderResolveWaveSnapshot(render_wave, live[i].age,
            live[i].world_x, live[i].world_y, live[i].facing_left,
            &out[count]))
      ++count;
  }
  return count;
}
