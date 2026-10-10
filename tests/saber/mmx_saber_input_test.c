#include <stdio.h>

#include "mmx_saber_input.h"

typedef struct PadCase {
  const char *name;
  MmxSaberPhysicalPad phys;
  MmxSaberNativePad native_mapped;
  MmxSaberPadSaber saber;
  MmxSaberPadZero zero;
  MmxSaberPadOut expected;
} PadCase;

static int failures;

static MmxSaberNativePad mapped_pad(void) {
  return (MmxSaberNativePad){0xd5, 0xc7, 0x44, 0x95, 0xc7};
}

static MmxSaberPhysicalPad physical_pad(bool x, bool x_previous, bool y,
                                         bool y_previous) {
  return (MmxSaberPhysicalPad){
      (uint16_t)((x ? MMX_SABER_PAD_X : 0) |
                 (y ? MMX_SABER_PAD_Y : 0)),
      (uint16_t)((x_previous ? MMX_SABER_PAD_X : 0) |
                 (y_previous ? MMX_SABER_PAD_Y : 0))};
}

static MmxSaberNativePad x_shoot(MmxSaberNativePad native, bool x_previous,
                                 bool x_held, bool x_pressed) {
  native.fire_previous =
      (uint8_t)((native.fire_previous & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (x_previous ? MMX_SABER_NATIVE_FIRE_BIT : 0));
  native.action_held =
      (uint8_t)((native.action_held & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (x_held ? MMX_SABER_NATIVE_FIRE_BIT : 0));
  native.action_pressed =
      (uint8_t)((native.action_pressed & (uint8_t)~MMX_SABER_NATIVE_FIRE_BIT) |
                (x_pressed ? MMX_SABER_NATIVE_FIRE_BIT : 0));
  return native;
}

static MmxSaberNativePad no_fire(MmxSaberNativePad native) {
  return x_shoot(native, false, false, false);
}

static MmxSaberNativePad mask_ground(MmxSaberNativePad native) {
  native.action_held =
      (uint8_t)(native.action_held & (uint8_t)~MMX_SABER_NATIVE_HORIZONTAL_BITS);
  native.action_pressed =
      (uint8_t)(native.action_pressed & (uint8_t)~MMX_SABER_NATIVE_HORIZONTAL_BITS);
  return native;
}

static MmxSaberNativePad mask_dash_cancel(MmxSaberNativePad native) {
  native.dash_pressed =
      (uint8_t)(native.dash_pressed & (uint8_t)~MMX_SABER_NATIVE_DASH_BIT);
  return native;
}

static MmxSaberNativePad mask_startup(MmxSaberNativePad native) {
  native.dash_pressed =
      (uint8_t)(native.dash_pressed & (uint8_t)~MMX_SABER_NATIVE_DASH_BIT);
  native.action_pressed =
      (uint8_t)(native.action_pressed & (uint8_t)~MMX_SABER_NATIVE_JUMP_BIT);
  return native;
}

static MmxSaberPadOut expected(MmxSaberNativePad native, bool held, bool pressed,
                               bool released, bool override, bool blocked,
                               bool pending, bool saber_pressed,
                               bool saber_held) {
  return (MmxSaberPadOut){native,
                          {held, pressed, released},
                          override,
                          blocked,
                          pending,
                          saber_pressed,
                          saber_held};
}

static bool native_equal(MmxSaberNativePad a, MmxSaberNativePad b) {
  return a.dash_held == b.dash_held && a.action_held == b.action_held &&
      a.fire_previous == b.fire_previous && a.dash_pressed == b.dash_pressed &&
      a.action_pressed == b.action_pressed;
}

static bool out_equal(MmxSaberPadOut a, MmxSaberPadOut b) {
  return native_equal(a.native, b.native) && a.legacy.held == b.legacy.held &&
      a.legacy.pressed == b.legacy.pressed &&
      a.legacy.released == b.legacy.released &&
      a.legacy_override == b.legacy_override &&
      a.fire_blocked == b.fire_blocked &&
      a.release_pending == b.release_pending &&
      a.saber_pressed == b.saber_pressed && a.saber_held == b.saber_held;
}

static void check_table(void) {
  const MmxSaberNativePad base = mapped_pad();
  const PadCase cases[] = {
      {"R1 dead/reset", physical_pad(true, true, true, false), base,
       {SABER_PHASE_ACTIVE, SABER_KIND_GROUND1, false, false, true},
       {true, false, true, true},
       expected(no_fire(mask_ground(base)), false, false, false, true, true,
                false, true, true)},
      {"R2 special idle", physical_pad(true, false, false, false), base,
       {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, false},
       {false, false, false, true},
       expected(x_shoot(base, false, true, true), false, false, false, false,
                false, false, false, false)},
      {"R3 special slashing", physical_pad(true, true, false, false), base,
       {SABER_PHASE_ACTIVE, SABER_KIND_GROUND1, false, false, false},
       {false, false, false, true},
       expected(no_fire(mask_ground(base)), false, false, false, false, true,
                false, false, false)},
      {"R4 buster idle", physical_pad(true, false, true, true), base,
       {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, false},
       {true, false, false, true},
       expected(x_shoot(base, false, true, true), true, true, false, true,
                false, false, false, true)},
      {"R5 buster slash held", physical_pad(true, true, false, false), base,
       {SABER_PHASE_ACTIVE, SABER_KIND_GROUND2, false, false, false},
       {true, false, false, true},
       expected(mask_ground(x_shoot(base, false, true, false)), true, false,
                false, true, true, false, false, false)},
      {"R6 buster slash released", physical_pad(false, true, false, false),
       base, {SABER_PHASE_RECOVERY, SABER_KIND_GROUND3, false, false, false},
       {true, false, false, true},
       expected(mask_ground(x_shoot(base, false, true, false)), true, false,
                false, true, true, true, false, false)},
      {"R7 pending slash pressed again", physical_pad(true, false, false, false),
       base, {SABER_PHASE_ACTIVE, SABER_KIND_AIR, false, false, true},
       {true, false, false, false},
       expected(x_shoot(base, false, true, false), true, false, false, true,
                true, false, false, false)},
      {"R8 pending idle release", physical_pad(false, true, false, false), base,
       {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, true},
       {true, false, false, true},
       expected(no_fire(base), false, false, true, true, false, false, false,
                false)},
      {"R9 pending idle held", physical_pad(true, false, false, false), base,
       {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, true},
       {true, false, false, true},
       expected(x_shoot(base, false, true, false), true, false, false, true,
                false, false, false, false)},
      {"R10 hurt suppresses press", physical_pad(true, false, false, false),
       base, {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, true},
       {true, true, false, true},
       expected(x_shoot(base, false, true, false), true, false, false, true,
                false, true, false, false)},
      {"R11 ground horizontal mask", physical_pad(false, false, false, false),
       base, {SABER_PHASE_ACTIVE, SABER_KIND_GROUND1, false, false, false},
       {true, false, false, true},
       expected(mask_ground(x_shoot(base, false, true, false)), true, false,
                false, true, true, true, false, false)},
      {"R11 dash keeps held, only jump escape",
       physical_pad(false, false, false, false), base,
       {SABER_PHASE_ACTIVE, SABER_KIND_DASH, false, false, false},
       {true, false, false, true},
       expected(mask_dash_cancel(mask_ground(x_shoot(base, false, true, false))),
                true, false, false, true, true, true, false, false)},
      {"R11 startup cancellation mask", physical_pad(false, false, false, false),
       base, {SABER_PHASE_STARTUP, SABER_KIND_AIR, false, false, false},
       {true, false, false, true},
       expected(mask_startup(x_shoot(base, false, true, false)), true, false,
                false, true, true, true, false, false)},
      {"R11 air movement passes", physical_pad(false, false, false, false), base,
       {SABER_PHASE_RECOVERY, SABER_KIND_AIR, false, false, false},
       {true, false, false, true},
       expected(x_shoot(base, false, true, false), true, false, false, true,
                true, true, false, false)},
      {"R12 Y is Saber, not native fire",
       physical_pad(false, false, true, false), base,
       {SABER_PHASE_IDLE, SABER_KIND_NONE, false, false, false},
       {false, false, false, true},
       expected(no_fire(base), false, false, false, false, false, false, true,
                true)},
      {"R13 no invented shoot press", physical_pad(false, true, false, true),
       base, {SABER_PHASE_ACTIVE, SABER_KIND_WALL, true, true, false},
       {true, false, false, true},
       expected(x_shoot(base, false, true, false), true, false, false, true,
                true, true, false, false)},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const MmxSaberPadOut actual = MmxSaberComputePad(
        cases[i].phys, cases[i].native_mapped, cases[i].saber, cases[i].zero);
    if (!out_equal(actual, cases[i].expected)) {
      printf("FAIL: table %s\n", cases[i].name);
      ++failures;
    } else {
      printf("ok: table %s\n", cases[i].name);
    }
  }
}

static void check_exhaustive_properties(void) {
  const MmxSaberNativePad mapped = mapped_pad();
  unsigned combinations = 0;
  unsigned release_transitions = 0;

  for (int phase = SABER_PHASE_IDLE; phase <= SABER_PHASE_RECOVERY; ++phase)
    for (int kind = SABER_KIND_NONE; kind <= SABER_KIND_SABER_LAND; ++kind)
      for (int wave = 0; wave <= 1; ++wave)
        for (int finisher = 0; finisher <= 1; ++finisher)
          for (int buster = 0; buster <= 1; ++buster)
            for (int hurt = 0; hurt <= 1; ++hurt)
              for (int dead = 0; dead <= 1; ++dead)
                for (int x = 0; x <= 1; ++x)
                  for (int x_previous = 0; x_previous <= 1; ++x_previous)
                    for (int y = 0; y <= 1; ++y)
                      for (int y_previous = 0; y_previous <= 1; ++y_previous)
                        for (int pending = 0; pending <= 1; ++pending) {
                          const MmxSaberPhysicalPad phys =
                              physical_pad(x != 0, x_previous != 0, y != 0,
                                           y_previous != 0);
                          const MmxSaberPadSaber saber = {
                              (MmxSaberPadPhase)phase,
                              (MmxSaberPadKind)kind,
                              wave != 0,
                              finisher != 0,
                              pending != 0};
                          const MmxSaberPadZero zero = {
                              buster != 0, hurt != 0, dead != 0, true};
                          const MmxSaberPadOut out =
                              MmxSaberComputePad(phys, mapped, saber, zero);
                          const bool x_press = x != 0 && x_previous == 0;
                          const bool is_slashing = phase != SABER_PHASE_IDLE ||
                              wave != 0 || finisher != 0;

                          ++combinations;
                          if (!x_press &&
                              (((out.native.action_pressed &
                                 MMX_SABER_NATIVE_FIRE_BIT) != 0) ||
                               out.legacy.pressed)) {
                            printf("FAIL: property no synthetic press at %d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
                                   phase, kind, wave, finisher, buster, hurt,
                                   dead, x, x_previous, y, y_previous, pending);
                            ++failures;
                          }
                          if ((is_slashing || dead != 0) && !out.fire_blocked) {
                            printf("FAIL: property fire block at %d/%d/%d/%d/%d/%d/%d\n",
                                   phase, kind, wave, finisher, buster, hurt,
                                   dead);
                            ++failures;
                          }
                          if (!buster && !dead && !is_slashing) {
                            const MmxSaberNativePad expected_native = x_shoot(
                                mapped, x_previous != 0, x != 0,
                                x_press && !hurt);
                            if (!native_equal(out.native, expected_native)) {
                              printf("FAIL: property special native mapping at %d/%d/%d/%d/%d/%d/%d\n",
                                     phase, kind, wave, finisher, hurt, x,
                                     x_previous);
                              ++failures;
                            }
                          }
                          if (!is_slashing && !dead && !pending &&
                              out.release_pending) {
                            printf("FAIL: property pending set while idle at %d/%d/%d/%d/%d/%d\n",
                                   phase, kind, buster, hurt, x, x_previous);
                            ++failures;
                          }

                          /* Start with a pending buster release and run two
                           * identical frames.  The first eligible frame is
                           * the only frame allowed to consume the latch. */
                          if (!is_slashing && !dead && buster && !hurt && !x) {
                            MmxSaberPadSaber pending_saber = saber;
                            MmxSaberPadZero pending_zero = zero;
                            pending_saber.release_pending = true;
                            pending_zero.buster_selected = true;
                            const MmxSaberPadOut first = MmxSaberComputePad(
                                phys, mapped, pending_saber, pending_zero);
                            pending_saber.release_pending = first.release_pending;
                            const MmxSaberPadOut second = MmxSaberComputePad(
                                phys, mapped, pending_saber, pending_zero);
                            if (first.release_pending) {
                              printf("FAIL: property R8 did not consume latch at %d/%d/%d/%d/%d/%d/%d/%d\n",
                                     phase, kind, wave, finisher, hurt, dead,
                                     x, x_previous);
                              ++failures;
                            } else if (!first.legacy.released ||
                                       (first.native.action_held &
                                        MMX_SABER_NATIVE_FIRE_BIT) != 0 ||
                                       (first.native.action_pressed &
                                        MMX_SABER_NATIVE_FIRE_BIT) != 0) {
                              printf("FAIL: property R8 output at %d/%d/%d/%d/%d/%d/%d/%d\n",
                                     phase, kind, wave, finisher, hurt, dead,
                                     x, x_previous);
                              ++failures;
                            }
                            if (second.release_pending) {
                              printf("FAIL: property release latch not consumed at %d/%d/%d/%d/%d/%d/%d/%d\n",
                                     phase, kind, wave, finisher, hurt, dead,
                                     x, x_previous);
                              ++failures;
                            }
                            if (!first.release_pending)
                              ++release_transitions;
                          }
                        }

  if (failures == 0)
    printf("ok: exhaustive properties (%u combinations, %u release transitions)\n",
           combinations, release_transitions);
}

static void check_cr1_sequence(void) {
  MmxSaberPadSaber saber = {SABER_PHASE_STARTUP, SABER_KIND_GROUND1, false,
                            false, false};
  const MmxSaberPadZero zero = {true, false, false, true};
  const MmxSaberNativePad mapped = mapped_pad();
  MmxSaberPhysicalPad phys = physical_pad(true, false, false, false);
  MmxSaberPadOut out = MmxSaberComputePad(phys, mapped, saber, zero);
  unsigned release_events = 0;

  saber.phase = SABER_PHASE_ACTIVE;
  phys = physical_pad(true, true, false, false);
  out = MmxSaberComputePad(phys, mapped, saber, zero);
  saber.kind = SABER_KIND_GROUND2;
  phys = physical_pad(true, true, false, false);
  out = MmxSaberComputePad(phys, mapped, saber, zero);
  saber.phase = SABER_PHASE_RECOVERY;
  saber.kind = SABER_KIND_GROUND3;
  phys = physical_pad(false, true, false, false);
  out = MmxSaberComputePad(phys, mapped, saber, zero);
  if (!out.release_pending || out.legacy.released ||
      (out.native.action_pressed & MMX_SABER_NATIVE_FIRE_BIT) != 0) {
    printf("FAIL: CR1 release was not buffered during combo\n");
    ++failures;
    return;
  }

  saber.phase = SABER_PHASE_IDLE;
  saber.kind = SABER_KIND_NONE;
  saber.release_pending = out.release_pending;
  out = MmxSaberComputePad(phys, mapped, saber, zero);
  if (out.release_pending || !out.legacy.released ||
      out.native.action_held & MMX_SABER_NATIVE_FIRE_BIT ||
      out.native.action_pressed & MMX_SABER_NATIVE_FIRE_BIT)
    ++failures;
  else
    ++release_events;

  saber.release_pending = out.release_pending;
  out = MmxSaberComputePad(phys, mapped, saber, zero);
  if (out.release_pending ||
      (out.native.action_pressed & MMX_SABER_NATIVE_FIRE_BIT) != 0)
    ++failures;
  if (release_events != 1) {
    printf("FAIL: CR1 expected one release event, got %u\n", release_events);
    ++failures;
  } else {
    printf("ok: CR1 one release on first idle frame\n");
  }
}

static void check_startup_edges(void) {
  MmxSaberNativePad mapped = mapped_pad();
  mapped.dash_pressed = MMX_SABER_NATIVE_DASH_BIT | 0x15;
  mapped.action_pressed = MMX_SABER_NATIVE_JUMP_BIT | 0x47;
  const MmxSaberPadOut out = MmxSaberComputePad(
      physical_pad(false, false, false, false), mapped,
      (MmxSaberPadSaber){SABER_PHASE_STARTUP, SABER_KIND_AIR, false, false,
                          false},
      (MmxSaberPadZero){false, false, false, false});
  if ((out.native.dash_pressed & MMX_SABER_NATIVE_DASH_BIT) != 0 ||
      (out.native.action_pressed & MMX_SABER_NATIVE_JUMP_BIT) != 0) {
    printf("FAIL: startup cancellation edges survived\n");
    ++failures;
  } else {
    printf("ok: startup masks jump/dash press\n");
  }
}

int main(void) {
  check_table();
  check_exhaustive_properties();
  check_cr1_sequence();
  check_startup_edges();
  if (failures != 0) {
    printf("FAIL: mmx_saber_input (%d failures)\n", failures);
    return 1;
  }
  printf("ok: mmx_saber_input\n");
  return 0;
}
