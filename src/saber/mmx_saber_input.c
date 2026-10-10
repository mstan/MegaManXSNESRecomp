#include "mmx_saber_input.h"

static bool pad_down(uint16_t buttons, uint16_t bit) {
  return (buttons & bit) != 0;
}

static void set_fire(MmxSaberNativePad *native, bool previous, bool held,
                     bool pressed) {
  native->fire_previous =
      (uint8_t)((native->fire_previous & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (previous ? MMX_SABER_NATIVE_FIRE_BIT : 0));
  native->action_held =
      (uint8_t)((native->action_held & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (held ? MMX_SABER_NATIVE_FIRE_BIT : 0));
  native->action_pressed =
      (uint8_t)((native->action_pressed & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (pressed ? MMX_SABER_NATIVE_FIRE_BIT : 0));
}

static void clear_fire(MmxSaberNativePad *native) {
  set_fire(native, false, false, false);
}

static bool attack_locked(MmxSaberPadPhase phase) {
  return phase == SABER_PHASE_STARTUP || phase == SABER_PHASE_ACTIVE ||
      phase == SABER_PHASE_RECOVERY;
}

static bool ground_kind(MmxSaberPadKind kind) {
  return kind == SABER_KIND_GROUND1 || kind == SABER_KIND_GROUND2 ||
      kind == SABER_KIND_GROUND3;
}

static bool slashing(const MmxSaberPadSaber *saber) {
  return saber->phase != SABER_PHASE_IDLE || saber->wave_active ||
      saber->finisher_active;
}

static void apply_motion_masks(MmxSaberNativePad *native,
                               MmxSaberPadSaber saber) {
  if (attack_locked(saber.phase) &&
      (ground_kind(saber.kind) || saber.kind == SABER_KIND_DASH)) {
    native->action_held =
        (uint8_t)(native->action_held &
                  (uint8_t)~MMX_SABER_NATIVE_HORIZONTAL_BITS);
    native->action_pressed =
        (uint8_t)(native->action_pressed &
                  (uint8_t)~MMX_SABER_NATIVE_HORIZONTAL_BITS);
  }

  /* A dash slash retains the native dash-held state, but a new dash edge
   * cannot be the cancellation that leaves the slash.  Jump is the only
   * escape input for this attack kind. */
  if (attack_locked(saber.phase) && saber.kind == SABER_KIND_DASH &&
      saber.phase != SABER_PHASE_STARTUP)
    native->dash_pressed =
        (uint8_t)(native->dash_pressed & (uint8_t)~MMX_SABER_NATIVE_DASH_BIT);

  /* Startup blocks cancellation edges.  Held dash is intentionally retained;
   * the state machine consumes only a press edge for cancellation. */
  if (saber.phase == SABER_PHASE_STARTUP) {
    native->dash_pressed =
        (uint8_t)(native->dash_pressed & (uint8_t)~MMX_SABER_NATIVE_DASH_BIT);
    native->action_pressed =
        (uint8_t)(native->action_pressed & (uint8_t)~MMX_SABER_NATIVE_JUMP_BIT);
  }
}

MmxSaberPadOut MmxSaberComputePad(MmxSaberPhysicalPad phys,
                                  MmxSaberNativePad native_mapped,
                                  MmxSaberPadSaber saber,
                                  MmxSaberPadZero zero) {
  const bool x_held = pad_down(phys.buttons, MMX_SABER_PAD_X);
  const bool x_previous = pad_down(phys.prev_buttons, MMX_SABER_PAD_X);
  const bool x_pressed = x_held && !x_previous;
  const bool y_held = pad_down(phys.buttons, MMX_SABER_PAD_Y);
  const bool y_previous = pad_down(phys.prev_buttons, MMX_SABER_PAD_Y);
  const bool y_pressed = y_held && !y_previous;
  const bool is_slashing = slashing(&saber);
  MmxSaberPadOut out = {
      native_mapped,
      {false, false, false},
      false,
      false,
      saber.release_pending,
      y_pressed,
      y_held};

  /* The old Saber mapping owns the native shoot action, which is bit $40 in
   * the held, previous, and pressed action views.  Remove Y's mapped shoot
   * edge before deciding whether physical X should replace it. */
  clear_fire(&out.native);

  if (zero.dead_or_reset) {
    out.legacy_override = true;
    out.fire_blocked = true;
    out.release_pending = false;
    apply_motion_masks(&out.native, saber);
    return out;
  }

  if (!zero.buster_selected) {
    out.legacy_override = false;
    if (is_slashing) {
      clear_fire(&out.native);
      out.fire_blocked = true;
    } else {
      set_fire(&out.native, x_previous, x_held, x_pressed && !zero.hurt);
      out.fire_blocked = false;
    }
  } else {
    out.legacy_override = true;
    if (is_slashing) {
      /* A hurt frame cannot consume or cancel an existing latch, but it still
       * keeps a physical release buffered while the slash owns the action. */
      if (!zero.hurt && x_pressed && out.release_pending)
        out.release_pending = false;
      if (!x_held)
        out.release_pending = true;
      set_fire(&out.native, false, true, false);
      out.legacy.held = true;
      out.legacy.pressed = false;
      out.legacy.released = false;
      out.fire_blocked = true;
    } else if (zero.hurt) {
      /* Hurt freezes the upstream charge chain: preserve its latch and do not
       * invent a shoot edge. */
      set_fire(&out.native, x_previous, x_held, false);
      out.legacy.held = x_held;
      out.legacy.pressed = false;
      out.legacy.released = !x_held;
      out.fire_blocked = false;
    } else if (out.release_pending && !x_held) {
      /* CR1 is a one-frame legacy release. */
      clear_fire(&out.native);
      out.legacy.held = false;
      out.legacy.pressed = false;
      out.legacy.released = true;
      out.release_pending = false;
      out.fire_blocked = false;
    } else {
      const bool pending_held = out.release_pending && x_held;
      set_fire(&out.native, x_previous, x_held,
               x_pressed && !pending_held);
      out.legacy.held = x_held;
      out.legacy.pressed = x_pressed && !pending_held;
      out.legacy.released = !x_held;
      if (pending_held)
        out.release_pending = false;
      out.fire_blocked = false;
    }
  }

  apply_motion_masks(&out.native, saber);
  return out;
}
