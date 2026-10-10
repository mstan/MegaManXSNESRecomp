/* Saber-owned reference checks for the unmodified X3 Zero and native X1
 * weapon paths. The runner supplies a copied catalog, an isolated cache, the
 * original ROMs, and the standing Highway save0 fixture. */
#define MMX_DESKTOP_ENTRY MmxDesktopMain
#include "desktop/host_main.c"
#include MMX_GAME_MAIN
#include <stdint.h>
#include "mmx_renderer.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#include "saber/mmx_saber_assets.h"
#include "saber/mmx_saber_attack.h"
#include "saber/mmx_saber_combo.h"
#include "saber/mmx_saber_frame.h"
#include "saber/mmx_saber_hitbox_debug.h"
#include "saber/mmx_saber_plugin.h"
#include "saber/mmx_saber_priority.h"
#include "saber/mmx_saber_sfx.h"
#include "saber/mmx_saber_tuning.h"
#include "saber/mmx_saber_wave.h"
#include "saber/mmx_saber_wave_runtime.h"
#include "mmx_wide_policy.h"
#include "mod_runtime.h"
#include "recomp_launcher.h"
#include "snes/interp_bridge.h"

/* Test-only renderer probe; the implementation stays out of the public API. */
extern bool MmxRendererRidePilotBoundsForTest(int native_box[4],
                                              int drawn_box[4]);
extern bool MmxRendererRidePilotFacingForTest(bool *native_hflip,
                                              bool *drawn_mirror);

enum {
  SABER_CHARGE_TIER_1_FRAME = 21,
  SABER_CHARGE_TIER_2_FRAME = 81,
  SABER_CHARGE_TIER_3_FRAME = 141,
  SABER_CHARGE_FULL_FRAME = 201,
  SABER_TIER_4_RELEASE_CLASS = 1,
  SABER_TIER_6_RELEASE_CLASS = 3,
  SABER_TIER_8_RELEASE_CLASS = 3,
  SABER_FULL_RELEASE_CLASS = 3,
  SABER_FULL_RELEASE_SHOTS = 2,
  SABER_FIRE_WAVE_FRAMES = 90,
  SABER_FIRE_WAVE_BIRTHS = 4,
  SABER_FIRE_WAVE_LIVE_FRAMES = 41,
  SABER_FIRE_WAVE_LIVE_SAMPLES = 85,
  SABER_FIRE_WAVE_PEAK = 4,
  /* These are measured from the pure upstream X3 Zero group below. */
  X3_ZERO_FIRE_WAVE_BIRTHS = 4,
  X3_ZERO_FIRE_WAVE_LIVE_FRAMES = 39,
  X3_ZERO_FIRE_WAVE_LIVE_SAMPLES = 79,
  X3_ZERO_FIRE_WAVE_PEAK = 4,
  SABER_ONE_SHOT_FRAMES = 60,
  SABER_ONE_SHOT_PROJECTILES = 1,
  X3_ZERO_STORM_TORNADO_PROJECTILES = 1,
  /* X1's native command-6 charged buster path publishes class 2. */
  SABER_X1_CHARGED_RELEASE_CLASS = 2,
  /* Independent oracle copied from oldsaber/saber-zero-variant:
   * src/mmx_saber.c:266-273. Keep these literals separate from the new table
   * so a timing-table mutation cannot make the test pass. */
  OLD_SABER_GROUND1_STARTUP = 4,
  OLD_SABER_GROUND1_ACTIVE = 8,
  OLD_SABER_GROUND1_RECOVERY = 18,
  OLD_SABER_GROUND1_TOTAL = 30,
  /* Combo windows and phase lengths copied from the old table, not from
   * src/saber/mmx_saber_attack.c: oldsaber src/mmx_saber.c:260-319. */
  OLD_SABER_GROUND1_CHAIN_OPEN = 12,
  OLD_SABER_GROUND1_CHAIN_CLOSE = 29,
  OLD_SABER_GROUND1_BUFFER_OPEN = 4,
  OLD_SABER_GROUND1_BUFFER_CLOSE = 11,
  OLD_SABER_GROUND2_STARTUP = 0,
  OLD_SABER_GROUND2_ACTIVE = 12,
  OLD_SABER_GROUND2_RECOVERY = 18,
  OLD_SABER_GROUND2_TOTAL = 30,
  OLD_SABER_GROUND2_CHAIN_OPEN = 12,
  OLD_SABER_GROUND2_CHAIN_CLOSE = 29,
  OLD_SABER_GROUND2_BUFFER_OPEN = 0,
  OLD_SABER_GROUND2_BUFFER_CLOSE = 11,
  OLD_SABER_GROUND3_STARTUP = 0,
  OLD_SABER_GROUND3_ACTIVE = 14,
  OLD_SABER_GROUND3_RECOVERY = 25,
  OLD_SABER_GROUND3_TOTAL = 39,
  /* Air record copied from oldsaber/saber-zero-variant:
   * src/mmx_saber.c:322-339. */
  OLD_SABER_AIR_STARTUP = 4,
  OLD_SABER_AIR_ACTIVE = 8,
  OLD_SABER_AIR_RECOVERY = 6,
  OLD_SABER_AIR_TOTAL = 18,
  /* Wall record copied from oldsaber/saber-zero-variant:
   * src/mmx_saber.c:307-324. */
  OLD_SABER_WALL_STARTUP = 0,
  OLD_SABER_WALL_ACTIVE = 12,
  OLD_SABER_WALL_RECOVERY = 8,
  OLD_SABER_WALL_TOTAL = 20,
  /* Dash record copied from oldsaber/saber-zero-variant:
   * src/mmx_saber.c:362-379. */
  OLD_SABER_DASH_STARTUP = 2,
  OLD_SABER_DASH_ACTIVE = 10,
  OLD_SABER_DASH_RECOVERY = 18,
  OLD_SABER_DASH_TOTAL = 30,
  OLD_SABER_LAND_TOTAL = 18,
};

static const char *const kMmxRomDigest =
    "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32";

static const uint8_t kSaberManifestSha[32] = {
  0x4e, 0x29, 0x1e, 0x5f, 0x03, 0x57, 0xaf, 0xa0,
  0x35, 0x76, 0x14, 0xe0, 0xc4, 0x97, 0xf9, 0xb3,
  0x65, 0xee, 0x99, 0xe3, 0x70, 0x64, 0x5d, 0x84,
  0x99, 0xb2, 0xe4, 0xfd, 0x07, 0xf8, 0x0f, 0xd0,
};

static const uint8_t kRideManifestSha[32] = {
  0xdf, 0x56, 0x36, 0x93, 0x59, 0x9e, 0x7d, 0x4d,
  0x37, 0xcb, 0xb7, 0x28, 0x7c, 0x4d, 0x95, 0x3e,
  0xbf, 0x23, 0xdb, 0x14, 0xa5, 0xc4, 0xbd, 0x29,
  0x91, 0x91, 0xf8, 0xf4, 0xb1, 0xff, 0x7d, 0xac,
};

static unsigned projectiles(unsigned kind);
static bool saber_test_grounded(void);
static void load_fixture(const char *fixture);
static bool saber_track_x1_charged;
static bool saber_saw_x1_charged;
static unsigned zero_state_reset_calls;

typedef struct RidePilotMeasuredPoses {
  bool neutral_left;
  bool walk_left;
  bool punch_left;
  bool walk_right;
  bool punch_right;
  bool jump_left;
  bool landing_left;
} RidePilotMeasuredPoses;

static RidePilotMeasuredPoses ride_pilot_measured;
static bool ride_pilot_orientation_seen[2];

static void zero_state_reset_probe(uint8_t *ram) {
  (void)ram;
  zero_state_reset_calls++;
}

typedef struct {
  bool override_zero;
  unsigned chosen_damage;
  unsigned current_frame;
  unsigned calls;
  unsigned enemy;
  unsigned projectile;
  unsigned value;
  unsigned first_positive_frame;
  unsigned first_positive_enemy;
  unsigned first_positive_projectile;
  unsigned first_positive_value;
  unsigned first_positive_timer;
  unsigned zero_frame;
  unsigned zero_enemy;
  unsigned zero_projectile;
  unsigned zero_value;
  unsigned override_frame;
  unsigned override_enemy;
  unsigned override_projectile;
  unsigned override_value;
  unsigned override_timer;
  unsigned zero_response_calls;
  unsigned damage_calls;
  unsigned zero_damage_calls;
  unsigned zero_damage_frame;
  unsigned zero_damage_enemy;
  unsigned zero_damage_projectile;
  unsigned zero_damage_value;
  unsigned last_frame;
  unsigned last_enemy;
  unsigned last_projectile;
  unsigned last_value;
  unsigned last_timer;
  unsigned zero_timer;
} ZeroResponseProbe;

static ZeroResponseProbe zero_response_probe;

static unsigned zero_response_probe_callback(uint8_t *ram, unsigned enemy,
                                             unsigned projectile,
                                             unsigned value) {
  zero_response_probe.last_frame = zero_response_probe.current_frame;
  zero_response_probe.last_enemy = enemy;
  zero_response_probe.last_projectile = projectile;
  zero_response_probe.last_value = value;
  zero_response_probe.last_timer = ram[enemy + 0x35];
  if (!zero_response_probe.calls) {
    zero_response_probe.enemy = enemy;
    zero_response_probe.projectile = projectile;
    zero_response_probe.value = value;
  }
  if (value && !zero_response_probe.first_positive_frame) {
    zero_response_probe.first_positive_frame = zero_response_probe.current_frame;
    zero_response_probe.first_positive_enemy = enemy;
    zero_response_probe.first_positive_projectile = projectile;
    zero_response_probe.first_positive_value = value;
    zero_response_probe.first_positive_timer = ram[enemy + 0x35];
  }
  if (!value) {
    ++zero_response_probe.zero_response_calls;
    if (!zero_response_probe.zero_frame) {
      zero_response_probe.zero_frame = zero_response_probe.current_frame;
      zero_response_probe.zero_enemy = enemy;
      zero_response_probe.zero_projectile = projectile;
      zero_response_probe.zero_value = value;
      zero_response_probe.zero_timer = ram[enemy + 0x35];
    }
    if (zero_response_probe.override_zero &&
        !zero_response_probe.override_frame) {
      zero_response_probe.override_frame = zero_response_probe.current_frame;
      zero_response_probe.override_enemy = enemy;
      zero_response_probe.override_projectile = projectile;
      zero_response_probe.override_value = value;
      zero_response_probe.override_timer = ram[enemy + 0x35];
    }
  }
  ++zero_response_probe.calls;
  return zero_response_probe.override_zero && !value ? 1 : value;
}

static unsigned zero_response_probe_damage(uint8_t *ram, unsigned enemy,
                                           unsigned projectile,
                                           unsigned value) {
  (void)ram;
  (void)enemy;
  (void)projectile;
  ++zero_response_probe.damage_calls;
  if (!value) {
    ++zero_response_probe.zero_damage_calls;
    if (!zero_response_probe.zero_damage_frame) {
      zero_response_probe.zero_damage_frame = zero_response_probe.current_frame;
      zero_response_probe.zero_damage_enemy = enemy;
      zero_response_probe.zero_damage_projectile = projectile;
      zero_response_probe.zero_damage_value = value;
    }
    if (zero_response_probe.chosen_damage)
      return zero_response_probe.chosen_damage;
  }
  return value;
}

static const MmxZeroExtension zero_response_extension = {
  .damage = zero_response_probe_damage,
  .response = zero_response_probe_callback,
};

static const MmxZeroExtension zero_response_observer_extension = {
  .response = zero_response_probe_callback,
};

typedef struct {
  unsigned births;
  unsigned live_frames;
  unsigned live_samples;
  unsigned peak;
} FireWaveCounts;

typedef struct {
  FireWaveCounts fire_wave;
  unsigned storm_tornado_projectiles;
} SpecialCounts;

typedef struct {
  unsigned shots;
  int y[2];
  bool airborne_release;
} BurstHeight;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
  }
  printf("ok: %s\n", what);
}

static int readable_file(const char *path) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return 0;
  fclose(f);
  return 1;
}

static void frame(unsigned input) {
  MmxBeforeFrame();
  RtlRunFrame(input | (1u << 30));
  CaptureSimulationFrame(1);
  if (saber_track_x1_charged && projectiles(SABER_X1_CHARGED_RELEASE_CLASS))
    saber_saw_x1_charged = true;
}

static void idle(unsigned count) {
  while (count--) frame(0);
}

static bool render_plane_matches(const MmxRenderPlayerOverlayPlane *actual,
                                 const MmxSaberPlane *expected) {
  return (actual->pixels != NULL) == (expected->pixels != NULL) &&
      actual->width == expected->width && actual->height == expected->height &&
      actual->origin_x == expected->origin_x &&
      actual->origin_y == expected->origin_y;
}

static MmxSaberAssets *load_render_oracle(void) {
  const char *cache = getenv("MMX_SABER_TEST_CACHE");
  char path[4096];
  char reason[128] = {0};
  FILE *file;
  MmxSaberAssets *assets;

  check(cache && cache[0] &&
            snprintf(path, sizeof(path), "%s/mmx-source/saber-v1.bin",
                     cache) < (int)sizeof(path),
        "renderer group receives the isolated Saber sidecar path");
  file = fopen(path, "rb");
  check(file != NULL, "renderer group opens the Saber sidecar oracle");
  fclose(file);
  assets = MmxSaberAssetsLoadFile(path, kSaberManifestSha,
                                  reason, sizeof(reason));
  check(assets != NULL, reason[0] ? reason :
        "renderer group parses the Saber sidecar oracle");
  return assets;
}

static MmxSaberAssets *load_ride_render_oracle(void) {
  const char *cache = getenv("MMX_SABER_TEST_CACHE");
  char path[4096];
  char reason[128] = {0};
  FILE *file;
  MmxSaberAssets *assets;

  check(cache && cache[0] &&
            snprintf(path, sizeof(path), "%s/mmx-source/ride-zero-v1.bin",
                     cache) < (int)sizeof(path),
        "Ride pilot group receives the isolated ride sidecar path");
  file = fopen(path, "rb");
  check(file != NULL, "Ride pilot group opens the ride sidecar oracle");
  fclose(file);
  assets = MmxSaberAssetsLoadFile(path, kRideManifestSha,
                                  reason, sizeof(reason));
  check(assets != NULL, reason[0] ? reason :
        "Ride pilot group parses the ride sidecar oracle");
  return assets;
}

static void saber_render_snapshot_checks(const char *fixture) {
  const MmxSaberAnimation *animation;
  MmxSaberAssets *oracle = load_render_oracle();
  MmxRenderPlayerOverlay before;
  MmxRenderPlayerOverlay after;

  load_fixture(fixture);
  MmxSaberFrameReset();
  MmxRendererBeginFrame(g_ram);
  check(!MmxRendererPlayerOverlaySnapshot().active,
        "idle BeginFrame snapshots an inactive player overlay");

  /* Change the live attack twice after BeginFrame. The second change crosses
   * the first sidecar step boundary, so a live read would expose frame 2. */
  frame(SNES_PAD_Y);
  MmxRendererBeginFrame(g_ram);
  before = MmxRendererPlayerOverlaySnapshot();
  MmxSaberAttackStep(false, true, true, 0, 0);
  MmxSaberAttackStep(false, true, true, 0, 0);
  after = MmxRendererPlayerOverlaySnapshot();
  check(before.active && after.active && before.body.pixels == after.body.pixels &&
            before.body.origin_x == after.body.origin_x &&
            before.body.origin_y == after.body.origin_y,
        "BeginFrame holds one overlay snapshot across a live state change");

  load_fixture(fixture);
  MmxSaberFrameReset();
  animation = MmxSaberAssetsAnimationById(oracle, 1);
  check(animation != NULL && animation->facing_xor == 1,
        "renderer oracle contains donor animation 1");
  for (unsigned tick = 0; tick < OLD_SABER_GROUND1_TOTAL; ++tick) {
    const MmxSaberFrame *expected;
    MmxSaberAttackSnapshot attack;
    MmxRenderPlayerOverlay actual;

    frame(tick == 0 ? SNES_PAD_Y : 0);
    MmxRendererBeginFrame(g_ram);
    attack = MmxSaberAttackSnapshotGet();
    actual = MmxRendererPlayerOverlaySnapshot();
    expected = MmxSaberAssetsFrameForStep(oracle, 1, (uint16_t)(tick / 2));
    check(expected != NULL && attack.anim_id == 1 && attack.tick == tick &&
              actual.active && render_plane_matches(&actual.body, &expected->body) &&
              render_plane_matches(&actual.blade, &expected->blade) &&
              actual.blade_layer == expected->blade_layer &&
              actual.palette_count == MmxSaberAssetsPaletteCount(oracle) &&
              actual.facing_left == (((attack.facing & 0x40) != 0) ^
                                     (animation->facing_xor != 0)),
          "ground slash snapshot matches each old animation-1 sidecar frame");
  }

  MmxSaberAttackReset();
  MmxRendererBeginFrame(g_ram);
  check(!MmxRendererPlayerOverlaySnapshot().active,
        "idle after a slash snapshots an inactive overlay");
  MmxRendererSetPlayerOverlayProvider(NULL);
  check(!MmxRendererPlayerOverlaySnapshot().active,
        "clearing the provider clears the overlay snapshot immediately");
  MmxSaberAssetsFree(oracle);
  puts("ok: saber-render-snapshot");
}

static uint32_t ride_pilot_render_output[256 * 224];

static uint64_t renderer_frame_hash(const uint32_t *pixels) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (unsigned i = 0; i < 256 * 224; ++i) {
    hash ^= pixels[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static bool ride_pilot_render_bounds(int native_box[4], int drawn_box[4]) {
  check(MmxRendererDraw(ride_pilot_render_output,
                        (MmxRenderView){256, 0, 4.0 / 3.0}, false),
        "Ride pilot renderer draws the captured frame");
  return MmxRendererRidePilotBoundsForTest(native_box, drawn_box);
}

static bool ride_pilot_expected_box(const char *label, unsigned pose,
                                    const int native_box[4], int expected[4]) {
  if (!strcmp(label, "neutral") && pose == 0)
    memcpy(expected, (int[4]){114, 105, 137, 122}, 4 * sizeof(*expected));
  else if ((!strcmp(label, "walk-left") || !strcmp(label, "walk-right")) &&
           pose == 1)
    memcpy(expected, (int[4]){116, 101, 139, 118}, 4 * sizeof(*expected));
  else if ((!strcmp(label, "punch-left") || !strcmp(label, "punch-right")) &&
           pose == 2)
    memcpy(expected, (int[4]){116, 102, 139, 119}, 4 * sizeof(*expected));
  else if (!strcmp(label, "jump") && pose == 6)
    memcpy(expected, (int[4]){114, 97, 137, 114}, 4 * sizeof(*expected));
  else if (!strcmp(label, "landing") && pose == 6)
    memcpy(expected, (int[4]){114, 60, 137, 77}, 4 * sizeof(*expected));
  else
    return false;
  return !memcmp(native_box, expected, 4 * sizeof(*expected));
}

static void ride_pilot_record_box(const char *label, unsigned pose) {
  int native_box[4], drawn_box[4];
  int expected_box[4];
  bool native_hflip, drawn_mirror;
  bool *seen = NULL;
  char message[160];
  if (!ride_pilot_render_bounds(native_box, drawn_box)) return;
  check(MmxRendererRidePilotFacingForTest(&native_hflip, &drawn_mirror),
        "Ride pilot orientation probe finds native OAM pieces");
  snprintf(message, sizeof(message),
           "Ride pilot mirror matches native pilot facing (BB9=%s, OAM=%s)",
           (g_ram[0x0bb9] & 0x40) ? "set" : "clear",
           native_hflip ? "set" : "clear");
  /* The ride sheet is authored opposite the native pilot OAM convention;
   * facing_xor=1 is the data record for that relationship. */
  check(drawn_mirror != native_hflip, message);
  if (!ride_pilot_orientation_seen[native_hflip]) {
    printf("reference: ride-facing BB9=%s native-oam-hflip=%s ride-mirror=%s "
           "expected=%s\n",
           (g_ram[0x0bb9] & 0x40) ? "set" : "clear",
           native_hflip ? "set" : "clear", drawn_mirror ? "left" : "right",
           native_hflip ? "right" : "left");
    ride_pilot_orientation_seen[native_hflip] = true;
  }
  if ((!strcmp(label, "neutral") && pose == 0 && native_hflip))
    seen = &ride_pilot_measured.neutral_left;
  else if ((!strcmp(label, "walk-left") && pose == 1 && native_hflip))
    seen = &ride_pilot_measured.walk_left;
  else if ((!strcmp(label, "punch-left") && pose == 2 && native_hflip))
    seen = &ride_pilot_measured.punch_left;
  else if ((!strcmp(label, "walk-right") && pose == 1 && !native_hflip))
    seen = &ride_pilot_measured.walk_right;
  else if ((!strcmp(label, "punch-right") && pose == 2 && !native_hflip))
    seen = &ride_pilot_measured.punch_right;
  else if ((!strcmp(label, "jump") && pose == 6 && native_hflip))
    seen = &ride_pilot_measured.jump_left;
  else if ((!strcmp(label, "landing") && pose == 6 && native_hflip))
    seen = &ride_pilot_measured.landing_left;
  if (!ride_pilot_expected_box(label, pose, native_box, expected_box)) return;
  if (!seen || *seen) return;
  *seen = true;
  printf("reference: ride-bbox %s pose=%u facing=%s native=(%d,%d)-(%d,%d) "
         "drawn=(%d,%d)-(%d,%d) delta=(%d,%d,%d,%d)\n", label, pose,
         native_hflip ? "L" : "R", native_box[0], native_box[1],
         native_box[2], native_box[3], drawn_box[0], drawn_box[1],
         drawn_box[2], drawn_box[3], drawn_box[0] - native_box[0],
         drawn_box[1] - native_box[1], drawn_box[2] - native_box[2],
         drawn_box[3] - native_box[3]);
  for (unsigned edge = 0; edge < 4; ++edge) {
    snprintf(message, sizeof(message),
             "Ride pilot %s pose %u %s edge %u is within 2 px",
             label, pose, native_hflip ? "left" : "right", edge);
    check(abs(drawn_box[edge] - native_box[edge]) <= 2, message);
  }
}

static void reload_ride_fixture(const char *path) {
  check(RtlLoadSnapshot(path), "ride-armor.sav reloads");
}

static void saber_ride_pilot_sequence(const char *label, unsigned input,
                                       unsigned count,
                                       const MmxSaberAssets *oracle) {
  const MmxSaberAnimation *animation =
      MmxSaberAssetsAnimationById(oracle, 0x006b);
  printf("reference: ride-pilot %s poses=", label);
  check(animation != NULL, "Ride pilot oracle contains animation 0x006B");
  for (unsigned i = 0; i < count; ++i) {
    MmxRenderPlayerOverlay actual;
    unsigned pose;
    const MmxSaberFrame *expected;

    frame(input);
    actual = MmxRendererPlayerOverlaySnapshot();
    pose = g_ram[0x0bbf] & 0x7f;
    expected = MmxSaberAssetsFrameForStep(
        oracle, 0x006b, (uint16_t)(pose < 23 ? pose : 0));
    check(g_ram[0x0baa] == 0x2c && actual.active && expected &&
              actual.body.pixels &&
              actual.body.width == expected->body.width &&
              actual.body.height == expected->body.height &&
              actual.body.origin_x == expected->body.origin_x &&
              actual.body.origin_y == expected->body.origin_y &&
              !memcmp(actual.body.pixels, expected->body.pixels,
                      (size_t)expected->body.width * expected->body.height) &&
              actual.palette &&
              actual.palette_count == MmxSaberAssetsPaletteCount(oracle) &&
              !memcmp(actual.palette, MmxSaberAssetsPalette(oracle),
                      (size_t)actual.palette_count * sizeof(uint16_t)) &&
              actual.blade.pixels == NULL && actual.blade.width == 0 &&
              actual.blade.height == 0 && actual.blade_layer == 0 &&
              actual.facing_left == (((g_ram[0x0bb9] & 0x40) != 0) ^
                                     (animation->facing_xor != 0)),
          "Ride Armor renderer overlay follows the live native pose");
    ride_pilot_record_box(label, pose);
    printf("%s%u", i ? " " : "", pose);
  }
  puts("");
}

static void saber_ride_pilot_checks(const char *fixture_dir) {
  char path[4096];
  MmxSaberAssets *oracle = load_ride_render_oracle();
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         "ride-armor.sav");
  check(written >= 0 && written < (int)sizeof(path),
        "ride-pilot fixture path fits");
  check(RtlLoadSnapshot(path), "ride-armor.sav loads");
  check(g_ram[0x0baa] == 0x2c && g_ram[0x0bbe] == 0x6a &&
            (g_ram[0x0e22] & 0x40) != 0 && g_ram[0x0bbf] < 23,
        "ride-armor.sav starts in the $6A Ride Armor pilot action");
  memset(&ride_pilot_measured, 0, sizeof(ride_pilot_measured));
  memset(ride_pilot_orientation_seen, 0, sizeof(ride_pilot_orientation_seen));
  MmxSaberFrameReset();
  saber_ride_pilot_sequence("neutral", 0, 8, oracle);
  reload_ride_fixture(path);
  MmxSaberFrameReset();
  saber_ride_pilot_sequence("walk-right", SNES_PAD_LEFT, 8, oracle);
  saber_ride_pilot_sequence("punch-right", SNES_PAD_X, 8, oracle);
  reload_ride_fixture(path);
  MmxSaberFrameReset();
  saber_ride_pilot_sequence("walk-left", SNES_PAD_RIGHT, 8, oracle);
  saber_ride_pilot_sequence("punch-left", SNES_PAD_X, 8, oracle);
  reload_ride_fixture(path);
  MmxSaberFrameReset();
  saber_ride_pilot_sequence("jump", SNES_PAD_B, 12, oracle);
  saber_ride_pilot_sequence("landing", 0, 20, oracle);
  check(ride_pilot_measured.neutral_left && ride_pilot_measured.walk_left &&
            ride_pilot_measured.punch_left && ride_pilot_measured.walk_right &&
            ride_pilot_measured.punch_right && ride_pilot_measured.jump_left &&
            ride_pilot_measured.landing_left,
        "Ride pilot measured poses all have native and drawn bounding boxes");
  check(ride_pilot_orientation_seen[0] && ride_pilot_orientation_seen[1],
        "Ride pilot orientation matches native OAM h-flip for both facings");

  load_fixture(getenv("MMX_ZERO_TEST_FIXTURE"));
  MmxSaberFrameReset();
  frame(0);
  MmxRendererBeginFrame(g_ram);
  check(g_ram[0x0baa] != 0x2c &&
            !MmxRendererPlayerOverlaySnapshot().active,
        "standing save0 has no Ride Armor overlay");
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  check(g_ram[0x0baa] != 0x2c &&
            MmxRendererPlayerOverlaySnapshot().active,
        "save0 ground slash has an ordinary Saber overlay");
  check(MmxRendererDraw(ride_pilot_render_output,
                        (MmxRenderView){256, 0, 4.0 / 3.0}, false),
        "save0 ground slash renderer draws");
  uint64_t ground_slash_hash = renderer_frame_hash(ride_pilot_render_output);
  printf("reference: save0 ground slash render hash=%016llx\n",
         (unsigned long long)ground_slash_hash);
  check(ground_slash_hash == UINT64_C(0x143974c8e7809858),
        "ordinary Saber attack render is byte-identical to the locked reference");
  check(!MmxRendererRidePilotBoundsForTest((int[4]){0}, (int[4]){0}),
        "ordinary Saber overlay does not use pilot alignment");
  MmxSaberAssetsFree(oracle);
  puts("ok: saber-ride-pilot");
}

static unsigned projectiles(unsigned kind) {
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    count += g_ram[d] && g_ram[d + 10] == kind;
  return count;
}

static unsigned all_projectiles(void) {
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64) count += g_ram[d] != 0;
  return count;
}

static bool saber_tagged_projectile(unsigned d) {
  return g_ram[d] &&
      ((((unsigned)g_ram[d + 0x3e] | (unsigned)g_ram[d + 0x3f] << 8) &
          MMX_SABER_PROJECTILE_TAG_FAMILY_MASK) ==
              MMX_SABER_PROJECTILE_TAG_FAMILY);
}

static unsigned native_projectiles(void) {
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    count += g_ram[d] && !saber_tagged_projectile(d);
  return count;
}

static unsigned tagged_projectiles(void) {
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    count += saber_tagged_projectile(d);
  return count;
}

static void shot_presence(unsigned kind, unsigned char present[8]) {
  for (unsigned i = 0; i < 8; ++i) {
    unsigned d = 0x1228 + i * 64;
    present[i] = (unsigned char)(g_ram[d] && g_ram[d + 10] == kind);
  }
}

static unsigned new_projectiles(unsigned kind, unsigned char previous[8]) {
  unsigned char now[8];
  unsigned births = 0;
  shot_presence(kind, now);
  for (unsigned i = 0; i < 8; ++i) births += now[i] && !previous[i];
  memcpy(previous, now, sizeof(now));
  return births;
}

static unsigned new_native_projectiles(unsigned char previous[8]) {
  unsigned char now[8];
  unsigned births = 0;
  for (unsigned i = 0; i < 8; ++i) {
    unsigned d = 0x1228 + i * 64;
    now[i] = (unsigned char)(g_ram[d] && !saber_tagged_projectile(d));
    births += now[i] && !previous[i];
  }
  memcpy(previous, now, sizeof(now));
  return births;
}

static unsigned new_native_charged_projectiles(unsigned char previous[8]) {
  unsigned char now[8];
  unsigned births = 0;
  for (unsigned i = 0; i < 8; ++i) {
    unsigned d = 0x1228 + i * 64;
    now[i] = (unsigned char)(g_ram[d] && !saber_tagged_projectile(d) &&
        (g_ram[d + 10] == SABER_TIER_4_RELEASE_CLASS ||
         g_ram[d + 10] == SABER_TIER_8_RELEASE_CLASS));
    births += now[i] && !previous[i];
  }
  memcpy(previous, now, sizeof(now));
  return births;
}

static void load_fixture(const char *fixture) {
  check(RtlLoadSnapshot(fixture), "save0.sav loads");
  check(MmxZeroActive() && !MmxZeroModern(), "fixture runs as X3 Zero");
  check(g_ram[0xba9] == 2 && g_ram[0xbaa] == 0 && g_ram[0xbdb] == 0 &&
            !g_ram[0x1f99] && (g_ram[0xbd3] & 4),
        "fixture is Highway standing with the buster selected");
  MmxZeroCancel(g_ram);
}

static void load_response_fixture(const char *fixture) {
  check(RtlLoadSnapshot(fixture), "response fixture loads");
  check(MmxZeroActive() && !MmxZeroModern(),
        "response fixture runs as upstream X3 Zero");
  MmxZeroCancel(g_ram);
}

static unsigned read_ram_word(const uint8_t *ram, unsigned offset);
static unsigned empty_enemy_slot(void);
static unsigned saber_active_slot(void);
static unsigned apply_test_damage(unsigned enemy, unsigned damage);
static unsigned saber_damage_boss_slot(void);
static bool saber_lifecycle_idle(void);
static bool saber_window_empty(const uint8_t *window);
static bool saber_wave_record_empty(const uint8_t *record);

static const uint8_t kOldSaberWallBounds[12] = {
    31, 246, 33, 19, 22, 0, 23, 15, 14, 1, 16, 12};

typedef struct SaberWallRoute {
  const char *side;
  unsigned walk_input;
  unsigned jump_input;
  unsigned travel_input;
  unsigned walk_frames;
  unsigned expected_wall_frame;
  unsigned expected_x;
  unsigned expected_y;
  uint8_t expected_native_facing;
  int open_sign;
} SaberWallRoute;

static const MmxSaberBoundsSegment *saber_wall_segment(uint8_t tick) {
  const MmxSaberAttack *wall = MmxSaberAttackRecord(SABER_KIND_WALL, 0);
  if (!wall) return NULL;
  for (unsigned i = 0; i < wall->bounds_segment_count; ++i) {
    const MmxSaberBoundsSegment *segment = wall->bounds_segments + i;
    if (tick >= segment->first_tick && tick <= segment->last_tick)
      return segment;
  }
  return NULL;
}

static int saber_wall_effective_x(const MmxSaberBoundsSegment *segment,
                                  uint8_t slot_facing) {
  if (!segment) return 0;
  return slot_facing & 0x40 ? -(int)segment->bounds_x :
      (int)segment->bounds_x;
}

static unsigned saber_wall_setup(const char *path, const SaberWallRoute *route,
                                 bool print_reference) {
  check(RtlLoadSnapshot(path), "wall fixture route loads");
  check(MmxZeroActive() && !MmxZeroModern(),
        "wall fixture route is legacy X3 Zero");
  MmxZeroCancel(g_ram);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < route->walk_frames; ++i)
    frame(route->walk_input);
  /* The release frame makes the following B edge physical after the setup
   * walk; the held travel direction remains native wall-slide input. */
  frame(0);
  for (unsigned i = 0; i < 240; ++i) {
    frame(i < 20 ? route->jump_input : route->travel_input);
    if (g_ram[0x0baa] == 0x12) {
      if (print_reference)
        printf("reference: wall fixture=armadillo-fight.sav side=%s "
               "script=hold %s for %u frames; 0; hold B+direction for "
               "20 frames; hold direction first_wall_frame=%u Zero=(%u,%u) "
               "native_facing=0x%02X\n",
               route->side,
               route->walk_input == SNES_PAD_LEFT ? "LEFT" : "RIGHT",
               route->walk_frames, i, read_ram_word(g_ram, 0x0bad),
               read_ram_word(g_ram, 0x0bb0), g_ram[0x0c11] & 0x40);
      for (unsigned settle = 0; settle < 6; ++settle)
        frame(route->travel_input);
      if (print_reference)
        printf("reference: wall settled side=%s settle_frames=6 Zero=(%u,%u) "
               "native_facing=0x%02X action=0x%02X\n", route->side,
               read_ram_word(g_ram, 0x0bad), read_ram_word(g_ram, 0x0bb0),
               g_ram[0x0c11] & 0x40, g_ram[0x0baa]);
      return i;
    }
  }
  return ~0u;
}

static unsigned saber_wall_charge_setup(const char *path,
                                        const SaberWallRoute *route) {
  const unsigned wall_frame = saber_wall_setup(path, route, false);
  check(wall_frame == route->expected_wall_frame,
        "charged wall fixture reaches the recorded native cling");
  return wall_frame;
}

static MmxSaberPadPhase saber_wall_phase(unsigned tick) {
  return tick < OLD_SABER_WALL_STARTUP ? SABER_PHASE_STARTUP :
      tick < OLD_SABER_WALL_STARTUP + OLD_SABER_WALL_ACTIVE ?
          SABER_PHASE_ACTIVE : SABER_PHASE_RECOVERY;
}

static void saber_wall_checks(const char *fixture_dir) {
  static const SaberWallRoute routes[] = {
    {"OPEN-RIGHT", SNES_PAD_LEFT, SNES_PAD_B | SNES_PAD_LEFT,
     SNES_PAD_LEFT, 60, 20, 5142, 2665, 0x40, 1},
    {"OPEN-LEFT", SNES_PAD_RIGHT, SNES_PAD_B | SNES_PAD_RIGHT,
     SNES_PAD_RIGHT, 180, 20, 5353, 2666, 0x00, -1},
  };
  const char *const fixture_name = "armadillo-fight.sav";
  const MmxSaberAttack *wall = MmxSaberAttackRecord(SABER_KIND_WALL, 0);
  char path[4096];
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         fixture_name);
  check(written >= 0 && written < (int)sizeof(path),
        "wall fixture path fits");
  check(wall && wall->visual_animation == 5 && wall->facing_xor == 1 &&
            wall->startup_ticks == OLD_SABER_WALL_STARTUP &&
            wall->active_ticks == OLD_SABER_WALL_ACTIVE &&
            wall->recovery_ticks == OLD_SABER_WALL_RECOVERY &&
            wall->total_ticks == OLD_SABER_WALL_TOTAL && wall->damage == 3 &&
            wall->bounds_pointer == MMX_SABER_WALL_BOUNDS_POINTER,
        "wall record keeps animation 5, old timing, $FF50, and damage 3");
  printf("reference: old wall timing startup=%u active=%u recovery=%u "
         "total=%u record=oldsaber/src/mmx_saber.c:307-324 "
         "context=oldsaber/src/mmx_saber.c:974-978\n",
         OLD_SABER_WALL_STARTUP, OLD_SABER_WALL_ACTIVE,
         OLD_SABER_WALL_RECOVERY, OLD_SABER_WALL_TOTAL);

  for (unsigned route_number = 0;
       route_number < sizeof(routes) / sizeof(routes[0]); ++route_number) {
    const SaberWallRoute *route = routes + route_number;
    unsigned baseline_y[OLD_SABER_WALL_TOTAL];
    unsigned baseline_x[OLD_SABER_WALL_TOTAL];
    unsigned baseline_vy[OLD_SABER_WALL_TOTAL];
    bool baseline_wall = true;
    bool timing_ok = true;
    bool facing_ok = true;
    bool anchor_ok = true;
    bool damage_ok = false;
    bool slide_ok = true;
    unsigned active_frames = 0;
    unsigned slot = 0;
    unsigned wall_frame = saber_wall_setup(path, route, true);
    check(wall_frame == route->expected_wall_frame,
          "wall setup reaches its recorded native contact frame");
    check(read_ram_word(g_ram, 0x0bad) == route->expected_x &&
              read_ram_word(g_ram, 0x0bb0) == route->expected_y &&
              (g_ram[0x0c11] & 0x40) == route->expected_native_facing &&
              g_ram[0x0baa] == 0x12,
          "wall setup position and settled native facing match reference");
    check(!memcmp(g_snes->cart->rom + 0x37f50, kOldSaberWallBounds,
                  sizeof(kOldSaberWallBounds)),
          "wall setup has the old $FF50 records in the tagged $37F40 window");

    /* Native-only control trace: the same contact and held direction, with no
     * Y edge, is the movement oracle for the slash trace below. */
    for (unsigned i = 0; i < OLD_SABER_WALL_TOTAL; ++i) {
      frame(route->travel_input);
      baseline_x[i] = read_ram_word(g_ram, 0x0bad);
      baseline_y[i] = read_ram_word(g_ram, 0x0bb0);
      baseline_vy[i] = read_ram_word(g_ram, 0x0bc4);
      if (g_ram[0x0baa] != 0x12) baseline_wall = false;
    }

    wall_frame = saber_wall_setup(path, route, false);
    check(wall_frame == route->expected_wall_frame,
          "wall slash reload reaches the same native contact frame");
    for (unsigned i = 0; i < OLD_SABER_WALL_TOTAL; ++i) {
      const unsigned input = route->travel_input |
          (i == 0 ? SNES_PAD_Y : 0);
      frame(input);
      MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
      const MmxSaberPadPhase expected_phase = saber_wall_phase(i);
      if (snapshot.kind != SABER_KIND_WALL || snapshot.index != 0 ||
          snapshot.anim_id != 5 || snapshot.tick != i ||
          snapshot.phase != expected_phase)
        timing_ok = false;
      if (baseline_x[i] != read_ram_word(g_ram, 0x0bad) ||
          baseline_y[i] != read_ram_word(g_ram, 0x0bb0) ||
          baseline_vy[i] != read_ram_word(g_ram, 0x0bc4))
        slide_ok = false;
      if (g_ram[0x0baa] != 0x12)
        slide_ok = false;

      if (snapshot.phase == SABER_PHASE_ACTIVE) {
        const MmxSaberBoundsSegment *segment =
            saber_wall_segment(snapshot.tick);
        slot = saber_active_slot();
        const unsigned slot_facing = slot ? g_ram[slot + 0x11] & 0x40 : 0xff;
        const unsigned expected_slot_facing =
            ((snapshot.facing != 0) ^ (wall->facing_xor != 0)) ? 0x40 : 0;
        const unsigned effective_x = saber_wall_effective_x(segment,
                                                              (uint8_t)slot_facing);
        const int slot_delta_x = slot ?
            (int)read_ram_word(g_ram, slot + 5) -
                (int)read_ram_word(g_ram, 0x0bad) : 0;
        ++active_frames;
        if (snapshot.facing != (g_ram[0x0c11] & 0x40) ||
            slot == 0 || tagged_projectiles() != 1 ||
            slot_facing != expected_slot_facing ||
            ((slot_delta_x + (int)effective_x) * route->open_sign) <= 0) {
          facing_ok = false;
        }
        if (!segment || slot == 0 || slot_delta_x < -1 || slot_delta_x > 1 ||
            read_ram_word(g_ram, slot + 8) != read_ram_word(g_ram, 0x0bb0) ||
            read_ram_word(g_ram, slot + 0x20) !=
                wall->bounds_pointer + (unsigned)(segment - wall->bounds_segments) * 4) {
          anchor_ok = false;
        }
        if (slot && !damage_ok) {
          const unsigned enemy = empty_enemy_slot();
          const unsigned first_damage =
              MmxSaberAttackDamage(g_ram, enemy, slot, 1);
          const unsigned second_damage =
              MmxSaberAttackDamage(g_ram, enemy, slot, 1);
          damage_ok = first_damage == 3 && second_damage == 0 &&
              (MmxSaberAttackHitSlots() &
               (uint16_t)(1u << ((enemy - 0xe68) / 64)));
        }
      } else if (tagged_projectiles() != 0) {
        anchor_ok = false;
      }
    }
    check(timing_ok && active_frames == OLD_SABER_WALL_ACTIVE,
          "wall Y starts animation 5 with old startup/active/recovery timing");
    check(facing_ok,
          "wall ACTIVE slot facing follows settled $0C11 and points to the open side");
    check(anchor_ok && damage_ok,
          "wall ACTIVE uses old bounds and deals 3 once per swing");
    check(baseline_wall && slide_ok,
          "wall slash leaves native wall-slide Y motion unchanged");
    check(MmxSaberAttackCueCount() == 1 &&
              MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_1,
          "wall slash emits exactly one saber_1 cue");
    frame(route->travel_input);
    check(saber_lifecycle_idle(),
          "wall slash releases its tagged slot at natural end");

    /* The old wall context is also the post-native wall-loss boundary.  A
     * native wall jump therefore retires the wall owner instead of becoming a
     * new Saber air owner. */
    wall_frame = saber_wall_setup(path, route, false);
    check(wall_frame == route->expected_wall_frame,
          "wall leave probe reaches the same native contact frame");
    frame(route->travel_input | SNES_PAD_Y);
    for (unsigned i = 0; i < 4; ++i) frame(route->travel_input);
    frame(route->travel_input | SNES_PAD_B);
    check(g_ram[0x0baa] != 0x12 && saber_lifecycle_idle(),
          "wall jump leaves the wall through the old central Saber exit");
  }
  puts("ok: saber-wall");
}

static void hold_charge_button(unsigned frames, unsigned button) {
  while (frames--) frame(button);
}

static void release_charge_button(unsigned frames, unsigned button) {
  hold_charge_button(frames, button);
  frame(0);
}

static void hold_charge(unsigned frames) {
  hold_charge_button(frames, SNES_PAD_Y);
}

static void release_charge(unsigned frames) {
  release_charge_button(frames, SNES_PAD_Y);
}

static unsigned read_ram_word(const uint8_t *ram, unsigned offset) {
  return ram[offset] | (unsigned)ram[offset + 1] << 8;
}

static void write_ram_word(uint8_t *ram, unsigned offset, unsigned value) {
  ram[offset] = (uint8_t)value;
  ram[offset + 1] = (uint8_t)(value >> 8);
}

enum {
  FIXTURE_REPLAY_FRAMES = 120,
  FIXTURE_WRAM_HASH_BYTES = 0x2000,
};

typedef struct {
  const char *name;
  unsigned stage;
  unsigned scene;
  unsigned x;
  unsigned y;
  unsigned hp;
  unsigned expected_stage;
  const char *expected_stage_name;
} FixtureReference;

/* These are identity oracles captured from the converted private fixtures in
 * the upstream Zero path. Keep each fixture's values named at the definition
 * site so a fixture replacement cannot silently change the reference set. */
static const FixtureReference kReferenceArmadilloFight = {
  "armadillo-fight.sav", 0x03, 0x04, 0x141B, 0x0A9F, 18, 0x03,
  "Armored Armadillo",
};
static const FixtureReference kReferenceArmadilloRoom = {
  "armadillo-room.sav", 0x03, 0x04, 0x1380, 0x0A6F, 18, 0x03,
  "Armored Armadillo",
};
static const FixtureReference kReferenceLogPlatform = {
  "log-platform.sav", 0x08, 0x04, 0x01E0, 0x0450, 8, 0x00, "Highway",
};
static const FixtureReference kReferenceMammothFight = {
  "mammoth-fight.sav", 0x04, 0x04, 0x1ED5, 0x02AF, 11, 0x04,
  "Flame Mammoth",
};
static const FixtureReference kReferenceMammothRoom = {
  "mammoth-room.sav", 0x04, 0x04, 0x1E2E, 0x02AF, 11, 0x04,
  "Flame Mammoth",
};
static const FixtureReference kReferenceMammothStun = {
  "mammoth-stun.sav", 0x04, 0x04, 0x1F3A, 0x02A0, 4, 0x04,
  "Flame Mammoth",
};
static const FixtureReference kReferencePenguinFight = {
  "penguin-fight.sav", 0x08, 0x04, 0x1E1B, 0x01AF, 16, 0x08,
  "Chill Penguin",
};
static const FixtureReference kReferencePenguinRoom = {
  "penguin-room.sav", 0x08, 0x04, 0x1D61, 0x018F, 16, 0x08,
  "Chill Penguin",
};
static const FixtureReference kReferenceRideArmor = {
  "ride-armor.sav", 0x08, 0x04, 0x1212, 0x038E, 14, 0x00, "Highway",
};

static const FixtureReference *const kFixtureReferences[] = {
  &kReferenceArmadilloFight,
  &kReferenceArmadilloRoom,
  &kReferenceLogPlatform,
  &kReferenceMammothFight,
  &kReferenceMammothRoom,
  &kReferenceMammothStun,
  &kReferencePenguinFight,
  &kReferencePenguinRoom,
  &kReferenceRideArmor,
};

static uint64_t fixture_wram_hash(void) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (unsigned i = 0; i < FIXTURE_WRAM_HASH_BYTES; ++i) {
    hash ^= g_ram[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void fixture_replay(const char *path, uint64_t hashes[FIXTURE_REPLAY_FRAMES],
                           const char *label) {
  char message[256];
  int written = snprintf(message, sizeof(message), "%s fresh load succeeds", label);
  check(written >= 0 && written < (int)sizeof(message),
        "fixture fresh-load message fits");
  check(RtlLoadSnapshot(path), message);
  for (unsigned frame_number = 0; frame_number < FIXTURE_REPLAY_FRAMES;
       ++frame_number) {
    frame(0);
    hashes[frame_number] = fixture_wram_hash();
  }
}

static void fixture_checks(const char *fixture_dir) {
  char path[4096];
  char message[256];
  for (unsigned i = 0; i < sizeof(kFixtureReferences) / sizeof(kFixtureReferences[0]); ++i) {
    const FixtureReference *reference = kFixtureReferences[i];
    int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir, reference->name);
    check(written >= 0 && written < (int)sizeof(path),
          "fixture path fits the test buffer");
    written = snprintf(message, sizeof(message), "%s exists", reference->name);
    check(written >= 0 && written < (int)sizeof(message),
          "fixture existence message fits");
    check(readable_file(path), message);
    written = snprintf(message, sizeof(message), "%s loads", reference->name);
    check(written >= 0 && written < (int)sizeof(message),
          "fixture load message fits");
    check(RtlLoadSnapshot(path), message);
    check(!MmxSaberEnabled() && MmxZeroActive() && !MmxZeroModern(),
          "fixture identity uses upstream Zero with Saber disabled");

    const unsigned stage = g_ram[0x1f7a];
    const unsigned scene = g_ram[0x00d3];
    const unsigned x = read_ram_word(g_ram, 0x0bad);
    const unsigned y = read_ram_word(g_ram, 0x0bb0);
    const unsigned hp = g_ram[0x0bcf] & 0x7f;
    printf("reference: %s stage=0x%02X scene=0x%02X x=0x%04X y=0x%04X hp=%u\n",
           reference->name, reference->stage, reference->scene, reference->x,
           reference->y, reference->hp);
    check(stage == reference->stage && scene == reference->scene &&
              x == reference->x && y == reference->y && hp == reference->hp,
          snprintf(message, sizeof(message), "%s identity matches reference",
                   reference->name) < (int)sizeof(message) ? message :
              "fixture identity message fits");
    if (stage == reference->expected_stage) {
      printf("mapping: %s -> stage 0x%02X (%s)\n", reference->name, stage,
             reference->expected_stage_name);
    } else {
      printf("mapping: %s -> stage 0x%02X (name suggests %s stage 0x%02X; "
             "mismatch reported, fixture not changed)\n", reference->name, stage,
             reference->expected_stage_name, reference->expected_stage);
    }

    uint64_t first[FIXTURE_REPLAY_FRAMES];
    uint64_t second[FIXTURE_REPLAY_FRAMES];
    fixture_replay(path, first, reference->name);
    fixture_replay(path, second, reference->name);
    unsigned mismatch = FIXTURE_REPLAY_FRAMES;
    for (unsigned frame_number = 0; frame_number < FIXTURE_REPLAY_FRAMES;
         ++frame_number) {
      if (first[frame_number] != second[frame_number]) {
        mismatch = frame_number;
        break;
      }
    }
    check(mismatch == FIXTURE_REPLAY_FRAMES,
          snprintf(message, sizeof(message), "%s has identical per-frame hashes "
                   "for %u neutral frames (WRAM $0000-$1FFF)", reference->name,
                   FIXTURE_REPLAY_FRAMES) < (int)sizeof(message) ? message :
              "fixture replay message fits");
    if (mismatch != FIXTURE_REPLAY_FRAMES)
      printf("replay: %s first mismatch at frame %u\n", reference->name, mismatch);
  }
  puts("ok: fixture identity and replay checks");
}

static struct {
  unsigned pre_calls, tick_calls, end_calls, frame_counter;
  unsigned pre_frames[32], end_frames[32];
  uint16_t first_pre_x, last_end_x;
  bool have_end, order_ok, movement_seen, slide_precondition_seen;
} zero_extension_observer;

static bool zero_extension_solid(const uint8_t *ram, int x, int y) {
  (void)ram;
  (void)x;
  (void)y;
  return true;
}

static void zero_extension_pre_player(uint8_t *ram) {
  unsigned call = zero_extension_observer.pre_calls++;
  if (call < 32) zero_extension_observer.pre_frames[call] = zero_extension_observer.frame_counter;
  if (zero_extension_observer.pre_calls != zero_extension_observer.tick_calls + 1)
    zero_extension_observer.order_ok = false;
  unsigned x = read_ram_word(ram, 0xbad);
  if (ram[0xbaa] == 0 && read_ram_word(ram, 0xbc8) == 0xa552)
    zero_extension_observer.slide_precondition_seen = true;
  if (!zero_extension_observer.have_end) zero_extension_observer.first_pre_x = (uint16_t)x;
  else if (x != zero_extension_observer.last_end_x) zero_extension_observer.order_ok = false;
}

static void zero_extension_player_end(uint8_t *ram) {
  unsigned call = zero_extension_observer.end_calls++;
  if (call < 32) zero_extension_observer.end_frames[call] = zero_extension_observer.frame_counter;
  if (zero_extension_observer.tick_calls != zero_extension_observer.pre_calls)
    zero_extension_observer.order_ok = false;
  unsigned x = read_ram_word(ram, 0xbad);
  if (zero_extension_observer.have_end && x != zero_extension_observer.last_end_x)
    zero_extension_observer.movement_seen = true;
  if (!zero_extension_observer.have_end && x != zero_extension_observer.first_pre_x)
    zero_extension_observer.movement_seen = true;
  zero_extension_observer.last_end_x = (uint16_t)x;
  zero_extension_observer.have_end = true;
}

static bool zero_extension_tick_probe(const uint8_t *ram,
                                      MmxZeroLegacyIntent *intent) {
  (void)ram;
  (void)intent;
  ++zero_extension_observer.tick_calls;
  if (zero_extension_observer.tick_calls != zero_extension_observer.pre_calls)
    zero_extension_observer.order_ok = false;
  return false;
}

static unsigned zero_extension_intent_calls;
static bool zero_extension_saw_mapped_charge;

static bool zero_extension_intent_override(const uint8_t *ram, MmxZeroLegacyIntent *intent) {
  if ((ram[0xbdf] & 64) || (ram[0xbe3] & 64)) zero_extension_saw_mapped_charge = true;
  unsigned call = zero_extension_intent_calls++;
  if (call < 30) {
    intent->held = true;
    intent->pressed = false;
    intent->released = false;
  } else {
    intent->held = false;
    intent->pressed = false;
    intent->released = true;
  }
  return true;
}

static bool zero_extension_intent_reject(const uint8_t *ram, MmxZeroLegacyIntent *intent) {
  if ((ram[0xbdf] & 64) || (ram[0xbe3] & 64)) zero_extension_saw_mapped_charge = true;
  intent->held = true;
  intent->pressed = false;
  intent->released = false;
  return false;
}

static void zero_extension_checks(const char *fixture) {
  static const MmxZeroExtension hooks = {
    .pre_player = zero_extension_pre_player,
    .player_end = zero_extension_player_end,
    .legacy_intent = zero_extension_tick_probe,
    .collision_rom = NULL,
  };
  static const MmxZeroExtension intent_hooks = {
    .legacy_intent = zero_extension_intent_override,
    .collision_rom = NULL,
  };
  static const MmxZeroExtension reject_hooks = {
    .legacy_intent = zero_extension_intent_reject,
    .collision_rom = NULL,
  };

  load_fixture(fixture);
  memset(&zero_extension_observer, 0, sizeof(zero_extension_observer));
  zero_extension_observer.order_ok = true;
  MmxZeroSetTerrainQuery(zero_extension_solid);
  g_ram[0xbaa] = 0;
  g_ram[0xbc8] = 0x52;
  g_ram[0xbc9] = 0xa5;
  MmxZeroSetExtension(&hooks);
  for (unsigned i = 0; i < 30; ++i) {
    zero_extension_observer.frame_counter = i;
    frame(SNES_PAD_RIGHT);
  }
  check(zero_extension_observer.pre_calls == 30 && zero_extension_observer.end_calls == 30,
        "zero extension calls pre-player and player-end once per frame");
  check(zero_extension_observer.tick_calls == 30,
        "zero extension observes one legacy player tick per frame");
  bool frame_records_match = true;
  for (unsigned i = 0; i < 21; ++i)
    if (zero_extension_observer.pre_frames[i] != i || zero_extension_observer.end_frames[i] != i)
      frame_records_match = false;
  check(frame_records_match, "zero extension callbacks record each frame in order");
  check(zero_extension_observer.order_ok && zero_extension_observer.movement_seen &&
            zero_extension_observer.slide_precondition_seen,
        "zero extension pre-player runs before native rightward movement");

  MmxZeroRegisterHooks();
  MmxZeroSetExtension(NULL);
  load_fixture(fixture);
  hold_charge(30);
  unsigned physical_charge = MmxZeroGetState().charge;
  frame(0);
  unsigned physical_class = projectiles(SABER_TIER_4_RELEASE_CLASS);
  check(physical_charge == 30 && physical_class == 1,
        "physical 30-frame charge establishes the legacy release reference");

  load_fixture(fixture);
  zero_extension_intent_calls = 0;
  zero_extension_saw_mapped_charge = false;
  MmxZeroSetExtension(&intent_hooks);
  for (unsigned i = 0; i < 30; ++i) frame(0);
  unsigned virtual_charge = MmxZeroGetState().charge;
  check(!zero_extension_saw_mapped_charge && virtual_charge == physical_charge,
        "legacy intent override charges without a physical charge button");
  frame(0);
  check(projectiles(SABER_TIER_4_RELEASE_CLASS) == physical_class &&
            !projectiles(2) && !projectiles(3),
        "legacy released intent fires the physical release shot class");

  load_fixture(fixture);
  zero_extension_intent_calls = 0;
  zero_extension_saw_mapped_charge = false;
  MmxZeroSetExtension(&reject_hooks);
  hold_charge(30);
  unsigned rejected_charge = MmxZeroGetState().charge;
  frame(0);
  check(zero_extension_saw_mapped_charge && rejected_charge == physical_charge &&
            projectiles(SABER_TIER_4_RELEASE_CLASS) == physical_class,
        "legacy intent callback returning false preserves mapped behavior");

  MmxZeroSetExtension(NULL);
}

static unsigned zero_legacy_slash_slot(void) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (g_ram[d] && read_ram_word(g_ram, d + 0x3e) == 0x5a53) return d;
  return 0;
}

static unsigned zero_slash_request_calls;
static bool zero_slash_request_pending;

static bool zero_legacy_slash_request(const uint8_t *ram) {
  (void)ram;
  ++zero_slash_request_calls;
  if (!zero_slash_request_pending) return false;
  zero_slash_request_pending = false;
  return true;
}

static void zero_legacy_slash_request_checks(const char *fixture) {
  static const MmxZeroExtension request_hooks = {
    .legacy_slash_request = zero_legacy_slash_request,
  };
  load_fixture(fixture);
  zero_slash_request_calls = 0;
  zero_slash_request_pending = true;
  MmxZeroSetExtension(&request_hooks);
  frame(0);
  MmxZeroState started = MmxZeroGetState();
  unsigned slot = zero_legacy_slash_slot();
  check(zero_slash_request_calls == 1,
        "legacy slash request callback runs once on the request frame");
  check(started.slash == 1 && slot && read_ram_word(g_ram, slot + 0x3e) == 0x5a53,
        "legacy slash request publishes one upstream $5A53 slot at slash 1");
  frame(0);
  check(MmxZeroGetState().slash == 2,
        "legacy slash request ages to slash 2 on the next frame");
  unsigned enemy = empty_enemy_slot();
  check(MmxZeroDamage(g_ram, enemy, slot, 3) == 16 &&
            MmxZeroDamage(g_ram, enemy, slot, 3) == 0,
        "legacy requested slash keeps upstream one-hit damage");
  MmxZeroSetExtension(NULL);

  load_fixture(fixture);
  zero_slash_request_calls = 0;
  zero_slash_request_pending = false;
  MmxZeroSetExtension(&request_hooks);
  frame(0);
  check(zero_slash_request_calls == 1 && !MmxZeroGetState().slash &&
            !zero_legacy_slash_slot(),
        "a false legacy slash request does not start a slash");
  MmxZeroSetExtension(NULL);
  puts("ok: zero-legacy-slash-request");
}

static void select_native_weapon(unsigned weapon) {
  check(weapon >= 1 && weapon <= 8, "native weapon index is valid");
  g_ram[0x1f85 + weapon * 2] = 0;
  g_ram[0x1f86 + weapon * 2] = 0xdc;
  for (unsigned i = 0; i < 6; ++i) frame(SNES_PAD_R);
  frame(0);
  check(g_ram[0xbdb] == weapon * 2, "native weapon selection reaches the requested slot");
}

static void switch_to_x(void) {
  frame(SNES_PAD_SELECT);
  check(MmxZeroSwapping(), "Select starts the native X/Zero exchange");
  for (unsigned i = 0; i < 180 && MmxZeroSwapping(); ++i) frame(0);
  check(!MmxZeroSwapping() && !MmxZeroActive(), "native exchange arrives as X");
}

static void x3_plain_checks(const char *fixture, unsigned button) {
  load_fixture(fixture);
  unsigned char previous[8] = {0};
  unsigned births = 0;
  for (unsigned tap = 0; tap < 3; ++tap) {
    frame(button);
    births += new_projectiles(0, previous);
    frame(0);
    births += new_projectiles(0, previous);
    idle(40);
    births += new_projectiles(0, previous);
  }
  check(births == 3, "X3 Zero repeated buster taps spawn one plain shot each");
  check(projectiles(0) <= 1 && all_projectiles() <= 1,
        "X3 Zero plain taps do not become charged or duplicate shots");
  puts("ok: x3-zero-plain");
}

static void x3_charge_checks(const char *fixture, unsigned button) {
  static const struct {
    unsigned frames, charge, tier;
  } checkpoints[] = {
    {20, 20, 0},
    {SABER_CHARGE_TIER_1_FRAME, 21, 4},
    {80, 80, 4},
    {SABER_CHARGE_TIER_2_FRAME, 81, 6},
    {140, 140, 6},
    {SABER_CHARGE_TIER_3_FRAME, 141, 8},
    {200, 200, 8},
    {SABER_CHARGE_FULL_FRAME, 201, 10},
  };
  for (unsigned i = 0; i < sizeof(checkpoints) / sizeof(checkpoints[0]); ++i) {
    load_fixture(fixture);
    hold_charge_button(checkpoints[i].frames, button);
    MmxZeroState state = MmxZeroGetState();
    char label[96];
    snprintf(label, sizeof(label), "X3 Zero charge checkpoint %u reaches %u (tier %u)",
             checkpoints[i].frames, checkpoints[i].charge, checkpoints[i].tier);
    check(state.charge == checkpoints[i].charge && MmxZeroChargeTier(&state) == checkpoints[i].tier,
          label);
  }

  load_fixture(fixture);
  release_charge_button(30, button);
  check(projectiles(SABER_TIER_4_RELEASE_CLASS) == 1 &&
            !projectiles(2) && !projectiles(3),
        "tier-4 release fires exactly one class-1 buster projectile");

  load_fixture(fixture);
  release_charge_button(90, button);
  check(projectiles(SABER_TIER_6_RELEASE_CLASS) == 1 &&
            !projectiles(1) && !projectiles(2),
        "tier-6 release fires exactly one class-3 buster projectile");

  load_fixture(fixture);
  release_charge_button(150, button);
  check(MmxZeroGetState().combo == 1 && !MmxZeroGetState().saber_ready,
        "tier-8 release stores one X3 charged shot without saber readiness");
  idle(30);
  check(projectiles(SABER_TIER_8_RELEASE_CLASS) == 1 &&
            !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "tier-8 release emits one class-3 buster projectile");

  load_fixture(fixture);
  release_charge_button(SABER_CHARGE_FULL_FRAME, button);
  check(MmxZeroGetState().combo == 1 && MmxZeroGetState().saber_ready,
        "full release stores the X3 two-shot combo and saber readiness");
  idle(17);
  frame(button);
  idle(9);
  check(projectiles(SABER_FULL_RELEASE_CLASS) == SABER_FULL_RELEASE_SHOTS &&
            MmxZeroGetState().combo == 2 &&
            !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "full charge release produces the observed two class-3 shots");
  printf("reference: X3 charge thresholds frames %u/%u/%u/%u; release classes 1/3/3; full shots=%u\n",
         SABER_CHARGE_TIER_1_FRAME, SABER_CHARGE_TIER_2_FRAME,
         SABER_CHARGE_TIER_3_FRAME, SABER_CHARGE_FULL_FRAME,
         projectiles(SABER_FULL_RELEASE_CLASS));
  puts("ok: x3-zero-charge-tiers");
}

static void set_burst_facing(bool left) {
  const uint8_t facing = left ? 0 : 0x40;
  g_ram[0xc11] = (uint8_t)((g_ram[0xc11] & (uint8_t)~0x40) | facing);
  g_ram[0xbb9] = (uint8_t)((g_ram[0xbb9] & (uint8_t)~0x40) | facing);
}

static void capture_burst_heights(unsigned char seen[8], BurstHeight *result) {
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned d = 0x1228 + i * 64;
    if (!seen[i] && g_ram[d] && g_ram[d + 10] == SABER_FULL_RELEASE_CLASS) {
      seen[i] = 1;
      if (result->shots < 2)
        result->y[result->shots] = (int)read_ram_word(g_ram, d + 8) -
            (int)read_ram_word(g_ram, 0xbb0);
      ++result->shots;
    }
  }
}

static BurstHeight measure_burst_heights(const char *fixture, unsigned button,
                                         unsigned hold_frames, bool left,
                                         bool airborne) {
  BurstHeight result = {0};
  unsigned char seen[8] = {0};
  load_fixture(fixture);
  if (MmxSaberEnabled()) MmxSaberFrameReset();
  set_burst_facing(left);

  if (!airborne) {
    hold_charge_button(hold_frames, button);
  } else {
    /* Start a real jump near the end of the charge so the release remains
     * airborne without changing the save fixture or its terrain. */
    const unsigned jump_frame = hold_frames > 20 ? hold_frames - 20 : 1;
    for (unsigned i = 0; i < hold_frames; ++i) {
      const bool jump_hold = i >= jump_frame && i < jump_frame + 10;
      frame(button | (jump_hold ? SNES_PAD_B : 0));
    }
    result.airborne_release = !saber_test_grounded();
  }
  frame(0);
  capture_burst_heights(seen, &result);
  /* Match the established full-charge route: wait 17 frames, then make the
   * second press that starts burst 2. Keep sampling through both births. */
  for (unsigned i = 0; i < 120 && result.shots < 2; ++i) {
    frame(i == 17 ? button : 0);
    capture_burst_heights(seen, &result);
  }
  return result;
}

static void upstream_burst_height_checks(const char *fixture,
                                         BurstHeight upstream[2],
                                         BurstHeight *airborne) {
  for (unsigned side = 0; side < 2; ++side) {
    upstream[side] = measure_burst_heights(
        fixture, SNES_PAD_Y, SABER_CHARGE_FULL_FRAME, side != 0, false);
    check(upstream[side].shots == 2,
          side ? "upstream left-facing full burst emits two shots" :
                 "upstream right-facing full burst emits two shots");
    check(upstream[side].y[0] < upstream[side].y[1],
          side ? "upstream left-facing shot 1 keeps its higher Y" :
                 "upstream right-facing shot 1 keeps its higher Y");
    printf("reference: save0 upstream burst-height facing=%s shot1=%d shot2=%d\n",
           side ? "left" : "right", upstream[side].y[0], upstream[side].y[1]);
  }
  *airborne = measure_burst_heights(
      fixture, SNES_PAD_Y, SABER_CHARGE_FULL_FRAME, false, true);
  if (airborne->shots == 2)
    printf("reference: save0 upstream burst-height airborne release_airborne=%u "
           "shot1=%d shot2=%d\n", airborne->airborne_release,
           airborne->y[0], airborne->y[1]);
  else
    puts("reference: save0 upstream airborne burst-height unavailable");
}

static void saber_burst_height_checks(const char *fixture,
                                      const BurstHeight upstream[2],
                                      const BurstHeight *upstream_airborne) {
  BurstHeight saber[2];
  for (unsigned side = 0; side < 2; ++side) {
    saber[side] = measure_burst_heights(
        fixture, SNES_PAD_X, SABER_CHARGE_TIER_3_FRAME + 59,
        side != 0, false);
    check(saber[side].shots == 2,
          side ? "Saber left-facing capped burst emits two shots" :
                 "Saber right-facing capped burst emits two shots");
    check(saber[side].y[0] == saber[side].y[1],
          side ? "Saber left-facing burst uses one Y for both shots" :
                 "Saber right-facing burst uses one Y for both shots");
    check(upstream[side].y[0] < upstream[side].y[1],
          side ? "upstream left-facing reference remains the higher first shot" :
                 "upstream right-facing reference remains the higher first shot");
    printf("reference: save0 Saber burst-height facing=%s cap-tier=8 shot1=%d "
           "shot2=%d upstream=%d/%d\n", side ? "left" : "right",
           saber[side].y[0], saber[side].y[1], upstream[side].y[0],
           upstream[side].y[1]);
  }

  BurstHeight airborne = measure_burst_heights(
      fixture, SNES_PAD_X, SABER_CHARGE_TIER_3_FRAME + 59, false, true);
  if (upstream_airborne->shots == 2 && upstream_airborne->airborne_release &&
      airborne.shots == 2 && airborne.airborne_release) {
    check(airborne.y[0] == airborne.y[1],
          "Saber airborne burst uses one Y for both shots");
    printf("reference: save0 Saber burst-height airborne shot1=%d shot2=%d "
           "upstream=%d/%d native-differ=%u\n", airborne.y[0], airborne.y[1],
           upstream_airborne->y[0], upstream_airborne->y[1],
           upstream_airborne->y[0] != upstream_airborne->y[1]);
  } else {
    puts("reference: save0 Saber airborne burst-height unavailable");
  }
  puts("ok: saber-burst-height");
}

static void x3_hurt_checks(const char *fixture, unsigned button) {
  load_fixture(fixture);
  hold_charge_button(60, button);
  unsigned before = MmxZeroGetState().charge;
  /* This is the same native hurt-state injection used by the upstream Zero
   * ROM checks. It avoids depending on an enemy being in the first few
   * seconds of save0.sav. */
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  unsigned hurt_duration = 0;
  unsigned lowest_charge = before;
  bool hurt_ended = false;
  for (unsigned i = 0; i < 120; ++i) {
    frame(button);
    unsigned charge = MmxZeroGetState().charge;
    if (g_ram[0xbaa] == 0x0e) {
      ++hurt_duration;
      if (charge < lowest_charge) lowest_charge = charge;
    } else {
      hurt_ended = true;
      break;
    }
  }
  unsigned after = MmxZeroGetState().charge;
  check(hurt_ended, "X3 Zero hurt state ends within 120 held-charge frames");
  check(hurt_duration >= 2, "X3 Zero hurt state is observed for at least two frames");
  check(lowest_charge >= before,
        "X3 Zero never drops below its pre-hurt charge while hurt");
  printf("reference: X3 hurt duration=%u charge %u -> %u\n",
         hurt_duration, before, after);
  puts("ok: x3-zero-charge-through-hurt");
}

static void x3_jump_checks(const char *fixture, unsigned button) {
  load_fixture(fixture);
  hold_charge_button(10, button);
  unsigned before = MmxZeroGetState().charge;
  bool was_grounded = (g_ram[0xbd3] & 4) || (g_ram[0xbd4] & 4);
  unsigned airborne_frames = 0;
  unsigned airborne_streak = 0;
  unsigned landing_frame = 0, charge_at_landing = 0;
  bool landed = false;
  bool charge_never_lowered = true;
  for (unsigned i = 1; i <= 20; ++i) {
    frame(SNES_PAD_B | button);
    bool grounded = (g_ram[0xbd3] & 4) || (g_ram[0xbd4] & 4);
    unsigned charge = MmxZeroGetState().charge;
    if (!grounded) {
      ++airborne_frames;
      ++airborne_streak;
    } else {
      airborne_streak = 0;
    }
    if (charge < before) charge_never_lowered = false;
    was_grounded = grounded;
  }
  for (unsigned i = 21; i <= 150; ++i) {
    frame(button);
    bool grounded = (g_ram[0xbd3] & 4) || (g_ram[0xbd4] & 4);
    unsigned charge = MmxZeroGetState().charge;
    if (!grounded) {
      ++airborne_frames;
      ++airborne_streak;
    } else if (!was_grounded) {
      landed = true;
      landing_frame = i;
      charge_at_landing = charge;
      break;
    } else {
      airborne_streak = 0;
    }
    if (charge < before) charge_never_lowered = false;
    was_grounded = grounded;
  }
  check(landed, "X3 Zero reaches a landing edge after the held jump");
  check(airborne_streak >= 10,
        "X3 Zero is airborne for at least ten consecutive frames before landing");
  check(charge_never_lowered && charge_at_landing >= before,
        "X3 Zero never lowers charge through jump and landing");
  unsigned previous_charge = charge_at_landing;
  bool charge_grew_after_landing = true;
  for (unsigned i = 0; i < 5; ++i) {
    frame(button);
    unsigned charge = MmxZeroGetState().charge;
    if (charge <= previous_charge) charge_grew_after_landing = false;
    previous_charge = charge;
  }
  check(charge_grew_after_landing,
        "X3 Zero keeps growing charge for five frames after landing");
  printf("reference: X3 held-jump airborne_frames=%u landing_frame=%u charge=%u\n",
         airborne_frames, landing_frame, charge_at_landing);
  puts("ok: x3-zero-charge-through-jump");
}

static void x3_post_charge_checks(const char *fixture, unsigned button) {
  load_fixture(fixture);
  release_charge_button(SABER_CHARGE_FULL_FRAME, button);
  idle(17);
  frame(button);
  idle(9);
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask || g_ram[0xc25]); ++i)
    frame(0);
  check(MmxZeroGetState().combo == 2, "full X3 buster sequence reaches its stored second-shot state");
  frame(button);
  idle(55);
  check(!MmxZeroGetState().combo && !MmxZeroGetState().slash,
        "full X3 buster sequence finishes before the plain-buster probe");
  unsigned char previous[8] = {0};
  frame(button);
  unsigned births = new_projectiles(0, previous);
  check(births == 1 && projectiles(0) == 1,
        "plain Zero buster still fires once after a full charged release");
  puts("ok: x3-zero-post-full-charge-plain");
}

static void native_x1_checks(const char *fixture) {
  load_fixture(fixture);
  switch_to_x();
  select_native_weapon(2); /* Fire Wave. */
  unsigned char previous[8] = {0};
  unsigned births = 0, live_frames = 0, peak_live = 0, live_samples = 0;
  for (unsigned i = 0; i < SABER_FIRE_WAVE_FRAMES; ++i) {
    frame(SNES_PAD_Y);
    unsigned live = projectiles(8); /* X1 Fire Wave's native flame class. */
    births += new_projectiles(8, previous);
    live_frames += live != 0;
    live_samples += live;
    if (live > peak_live) peak_live = live;
  }
  check(births == SABER_FIRE_WAVE_BIRTHS &&
            live_frames == SABER_FIRE_WAVE_LIVE_FRAMES &&
            live_samples == SABER_FIRE_WAVE_LIVE_SAMPLES &&
            peak_live == SABER_FIRE_WAVE_PEAK,
        "native X1 Fire Wave matches its 90-frame reference counts");
  printf("reference: Fire Wave held %u frames births=%u live_frames=%u live_samples=%u peak=%u\n",
         SABER_FIRE_WAVE_FRAMES, births, live_frames, live_samples, peak_live);

  load_fixture(fixture);
  switch_to_x();
  select_native_weapon(5); /* Storm Tornado: one normal shot per tap. */
  memset(previous, 0, sizeof(previous));
  births = 0;
  frame(SNES_PAD_Y);
  births += new_projectiles(11, previous);
  for (unsigned i = 1; i < SABER_ONE_SHOT_FRAMES; ++i) {
    frame(0);
    births += new_projectiles(11, previous);
  }
  check(births == SABER_ONE_SHOT_PROJECTILES,
        "native X1 one-shot weapon fires exactly once per tap");
  printf("reference: Storm Tornado one tap frames=%u projectiles=%u\n",
         SABER_ONE_SHOT_FRAMES, births);
  puts("ok: x1-native-weapons");
}

static FireWaveCounts measure_fire_wave(const char *fixture, unsigned button) {
  FireWaveCounts counts = {0};
  unsigned char previous[8] = {0};

  load_fixture(fixture);
  MmxSaberFrameReset();
  select_native_weapon(2); /* Fire Wave. */
  for (unsigned i = 0; i < SABER_FIRE_WAVE_FRAMES; ++i) {
    frame(button);
    unsigned live = projectiles(8);
    counts.births += new_projectiles(8, previous);
    counts.live_frames += live != 0;
    counts.live_samples += live;
    if (live > counts.peak) counts.peak = live;
  }
  return counts;
}

static unsigned measure_storm_tornado(const char *fixture, unsigned button) {
  unsigned char previous[8] = {0};
  unsigned births = 0;

  load_fixture(fixture);
  MmxSaberFrameReset();
  select_native_weapon(5); /* Storm Tornado: one normal shot per tap. */
  frame(button);
  births += new_projectiles(11, previous);
  for (unsigned i = 1; i < SABER_ONE_SHOT_FRAMES; ++i) {
    frame(0);
    births += new_projectiles(11, previous);
  }
  return births;
}

static SpecialCounts measure_specials(const char *fixture, unsigned button) {
  SpecialCounts counts;
  counts.fire_wave = measure_fire_wave(fixture, button);
  counts.storm_tornado_projectiles = measure_storm_tornado(fixture, button);
  return counts;
}

static void print_special_reference(const char *label,
                                    const SpecialCounts *counts) {
  printf("reference: %s Fire Wave held %u frames births=%u live_frames=%u "
         "live_samples=%u peak=%u\n",
         label, SABER_FIRE_WAVE_FRAMES, counts->fire_wave.births,
         counts->fire_wave.live_frames, counts->fire_wave.live_samples,
         counts->fire_wave.peak);
  printf("reference: %s Storm Tornado one tap frames=%u projectiles=%u\n",
         label, SABER_ONE_SHOT_FRAMES, counts->storm_tornado_projectiles);
}

static void check_fire_wave_equal(const char *what,
                                  const FireWaveCounts *reference,
                                  const FireWaveCounts *actual) {
  bool equal = reference->births == actual->births &&
               reference->live_frames == actual->live_frames &&
               reference->live_samples == actual->live_samples &&
               reference->peak == actual->peak;
  if (!equal) {
    fprintf(stderr,
            "FAIL: %s upstream=%u/%u/%u/%u actual=%u/%u/%u/%u\n",
            what, reference->births, reference->live_frames,
            reference->live_samples, reference->peak, actual->births,
            actual->live_frames, actual->live_samples, actual->peak);
  }
  check(equal, what);
}

static void check_specials_equal(const char *what,
                                 const SpecialCounts *reference,
                                 const SpecialCounts *actual) {
  bool equal = reference->fire_wave.births == actual->fire_wave.births &&
               reference->fire_wave.live_frames == actual->fire_wave.live_frames &&
               reference->fire_wave.live_samples == actual->fire_wave.live_samples &&
               reference->fire_wave.peak == actual->fire_wave.peak &&
               reference->storm_tornado_projectiles ==
                   actual->storm_tornado_projectiles;
  if (!equal) {
    fprintf(stderr,
            "FAIL: %s upstream Fire Wave=%u/%u/%u/%u Storm=%u; "
            "actual Fire Wave=%u/%u/%u/%u Storm=%u\n",
            what, reference->fire_wave.births,
            reference->fire_wave.live_frames,
            reference->fire_wave.live_samples, reference->fire_wave.peak,
            reference->storm_tornado_projectiles, actual->fire_wave.births,
            actual->fire_wave.live_frames, actual->fire_wave.live_samples,
            actual->fire_wave.peak, actual->storm_tornado_projectiles);
  }
  check(equal, what);
}

static SpecialCounts x3_zero_specials_checks(const char *fixture) {
  SpecialCounts counts = measure_specials(fixture, SNES_PAD_Y);
  print_special_reference("X3 Zero", &counts);
  check(counts.fire_wave.births == X3_ZERO_FIRE_WAVE_BIRTHS &&
            counts.fire_wave.live_frames == X3_ZERO_FIRE_WAVE_LIVE_FRAMES &&
            counts.fire_wave.live_samples == X3_ZERO_FIRE_WAVE_LIVE_SAMPLES &&
            counts.fire_wave.peak == X3_ZERO_FIRE_WAVE_PEAK,
        "upstream X3 Zero Fire Wave matches its 90-frame reference counts");
  check(counts.storm_tornado_projectiles == X3_ZERO_STORM_TORNADO_PROJECTILES,
        "upstream X3 Zero Storm Tornado fires once per tap");
  puts("ok: x3-zero-specials");
  return counts;
}

static void saber_y_checks(const char *fixture) {
  load_fixture(fixture);
  for (unsigned i = 0; i < 200; ++i) frame(SNES_PAD_Y);
  check(MmxZeroGetState().charge == 0 && native_projectiles() == 0,
        "physical Y hold never charges or fires Zero's buster");
  frame(0);
  for (unsigned tap = 0; tap < 3; ++tap) {
    frame(SNES_PAD_Y);
    frame(0);
    idle(20);
    check(MmxZeroGetState().charge == 0 && native_projectiles() == 0,
          "physical Y taps never fire a Zero shot");
  }
  puts("ok: saber-input-y-blocked");
}

static void saber_ground_1_checks(const char *fixture) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberAttackSnapshot previous;
  bool timing_ok = true;
  bool mask_ok = true;
  bool position_ok = true;
  bool charge_not_lost = true;
  bool shot_before_idle = false;
  bool previous_projectile = false;
  unsigned starts = 0;
  unsigned births = 0;
  unsigned charge_before_release;
  uint16_t standing_x;

  printf("reference: old Saber ground-1 timing startup=%u active=%u "
         "recovery=%u total=%u (oldsaber src/mmx_saber.c:266-269)\n",
         OLD_SABER_GROUND1_STARTUP, OLD_SABER_GROUND1_ACTIVE,
         OLD_SABER_GROUND1_RECOVERY, OLD_SABER_GROUND1_TOTAL);

  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL; ++i) {
    const MmxSaberPadPhase expected =
        i < OLD_SABER_GROUND1_STARTUP ? SABER_PHASE_STARTUP :
        i < OLD_SABER_GROUND1_STARTUP + OLD_SABER_GROUND1_ACTIVE ?
            SABER_PHASE_ACTIVE : SABER_PHASE_RECOVERY;
    frame(i == 0 ? SNES_PAD_Y : 0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.kind != SABER_KIND_GROUND1 || snapshot.index != 0 ||
        snapshot.anim_id != 1 || snapshot.tick != i ||
        snapshot.phase != expected)
      timing_ok = false;
  }
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(timing_ok && snapshot.phase == SABER_PHASE_IDLE &&
            snapshot.kind == SABER_KIND_NONE && snapshot.anim_id == 0,
        "Saber ground-1 publishes animation 1 through old startup/active/recovery timing");

  load_fixture(fixture);
  MmxSaberFrameReset();
  previous = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 12; ++i) {
    frame(SNES_PAD_Y);
    snapshot = MmxSaberAttackSnapshotGet();
    if (previous.phase == SABER_PHASE_IDLE &&
        snapshot.phase != SABER_PHASE_IDLE)
      ++starts;
    previous = snapshot;
  }
  check(starts == 1 && snapshot.phase == SABER_PHASE_IDLE,
        "holding Y starts exactly one ground-1 attack");

  load_fixture(fixture);
  MmxSaberFrameReset();
  g_ram[0x00ac] = 0x40;
  g_ram[0x00a7] = 0;
  g_ram[0x00a9] = 0;
  starts = 0;
  previous = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 2; ++i) {
    MmxZeroExtPrePlayer(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
    if (previous.phase == SABER_PHASE_IDLE &&
        snapshot.phase != SABER_PHASE_IDLE)
      ++starts;
    previous = snapshot;
  }
  check(starts == 1 && snapshot.phase == SABER_PHASE_IDLE,
        "the pre-player Y hold edge remains one-shot across the idle boundary");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y | SNES_PAD_RIGHT);
  standing_x = (uint16_t)read_ram_word(g_ram, 0x0bad);
  if (g_ram[0x0bdf] & MMX_SABER_NATIVE_HORIZONTAL_BITS)
    mask_ok = false;
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL - 1; ++i) {
    frame(SNES_PAD_RIGHT);
    if (g_ram[0x0bdf] & MMX_SABER_NATIVE_HORIZONTAL_BITS)
      mask_ok = false;
    if (read_ram_word(g_ram, 0x0bad) != standing_x)
      position_ok = false;
  }
  check(mask_ok && position_ok,
        "ground-1 feeds the live phase back to clear horizontal pad bits and halt Zero");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y | SNES_PAD_X);
  for (unsigned i = 0; i < 6; ++i) {
    frame(SNES_PAD_X);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE && native_projectiles())
      shot_before_idle = true;
  }
  charge_before_release = MmxZeroGetState().charge;
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  if (snapshot.phase == SABER_PHASE_IDLE || native_projectiles())
    shot_before_idle = true;
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 5; ++i) {
    bool live;
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    live = native_projectiles() != 0;
    if (snapshot.phase != SABER_PHASE_IDLE &&
        MmxZeroGetState().charge < charge_before_release)
      charge_not_lost = false;
    if (live && !previous_projectile)
      ++births;
    if (live && snapshot.phase != SABER_PHASE_IDLE)
      shot_before_idle = true;
    previous_projectile = live;
  }
  check(charge_before_release > 0 && charge_not_lost && births == 1 &&
            !shot_before_idle,
        "X held during ground-1 charges without a shot; release buffers exactly one post-idle shot");
  puts("ok: saber-ground-1");
}

static MmxSaberPadPhase old_ground_phase(unsigned startup, unsigned active,
                                         unsigned tick) {
  return tick < startup ? SABER_PHASE_STARTUP :
      tick < startup + active ? SABER_PHASE_ACTIVE : SABER_PHASE_RECOVERY;
}

static bool ground_snapshot_matches(const MmxSaberAttackSnapshot *snapshot,
                                    unsigned index, unsigned animation,
                                    unsigned startup, unsigned active,
                                    unsigned tick) {
  const MmxSaberPadPhase phase = old_ground_phase(startup, active, tick);
  return snapshot->kind == (MmxSaberPadKind)(SABER_KIND_GROUND1 + index) &&
      snapshot->index == index && snapshot->anim_id == animation &&
      snapshot->tick == tick && snapshot->phase == phase;
}

static void saber_ground_combo_checks(const char *fixture) {
  MmxSaberAttackSnapshot snapshot;
  bool phase_ok = true;
  bool buffer_ok = true;
  bool held_ok = true;
  bool boundary_ok = true;
  bool facing_ok = true;
  bool position_ok = true;
  bool velocity_ok = true;
  bool saw_ground3 = false;
  uint16_t running_x;
  uint16_t stopped_x;

  printf("reference: old Saber combo windows g1 chain=%u..%u buffer=%u..%u; "
         "g2 chain=%u..%u buffer=%u..%u "
         "(oldsaber src/mmx_saber.c:260-319)\n",
         OLD_SABER_GROUND1_CHAIN_OPEN, OLD_SABER_GROUND1_CHAIN_CLOSE,
         OLD_SABER_GROUND1_BUFFER_OPEN, OLD_SABER_GROUND1_BUFFER_CLOSE,
         OLD_SABER_GROUND2_CHAIN_OPEN, OLD_SABER_GROUND2_CHAIN_CLOSE,
         OLD_SABER_GROUND2_BUFFER_OPEN, OLD_SABER_GROUND2_BUFFER_CLOSE);

  /* Separate Y edges at the inclusive chain-window endpoint publish all
   * three records. Validate the phase oracle independently for every visible
   * tick, including the recovery portions before each accepted edge. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  phase_ok = ground_snapshot_matches(&snapshot, 0, 1,
                                     OLD_SABER_GROUND1_STARTUP,
                                     OLD_SABER_GROUND1_ACTIVE, 0);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_CLOSE; ++tick) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (!ground_snapshot_matches(&snapshot, 0, 1,
                                 OLD_SABER_GROUND1_STARTUP,
                                 OLD_SABER_GROUND1_ACTIVE, tick))
      phase_ok = false;
  }
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 1, 2,
                               OLD_SABER_GROUND2_STARTUP,
                               OLD_SABER_GROUND2_ACTIVE, 0))
    phase_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND2_CHAIN_CLOSE; ++tick) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (!ground_snapshot_matches(&snapshot, 1, 2,
                                 OLD_SABER_GROUND2_STARTUP,
                                 OLD_SABER_GROUND2_ACTIVE, tick))
      phase_ok = false;
  }
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 2, 3,
                               OLD_SABER_GROUND3_STARTUP,
                               OLD_SABER_GROUND3_ACTIVE, 0))
    phase_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND3_TOTAL; ++tick) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (!ground_snapshot_matches(&snapshot, 2, 3,
                                 OLD_SABER_GROUND3_STARTUP,
                                 OLD_SABER_GROUND3_ACTIVE, tick))
      phase_ok = false;
  }
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(phase_ok && snapshot.phase == SABER_PHASE_IDLE,
        "separate Y taps chain ground animations 1, 2, 3 with old phases");

  /* One early edge is held until the first chain-open tick, while a second
   * early edge cannot create a second queued entry. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_BUFFER_OPEN; ++tick)
    frame(0);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 0, 1,
                               OLD_SABER_GROUND1_STARTUP,
                               OLD_SABER_GROUND1_ACTIVE,
                               OLD_SABER_GROUND1_BUFFER_OPEN))
    buffer_ok = false;
  for (unsigned tick = OLD_SABER_GROUND1_BUFFER_OPEN + 1;
       tick < OLD_SABER_GROUND1_CHAIN_OPEN; ++tick)
    frame(0);
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 1, 2,
                               OLD_SABER_GROUND2_STARTUP,
                               OLD_SABER_GROUND2_ACTIVE, 0))
    buffer_ok = false;

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_BUFFER_OPEN; ++tick)
    frame(0);
  frame(SNES_PAD_Y);
  frame(0);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 0, 1,
                               OLD_SABER_GROUND1_STARTUP,
                               OLD_SABER_GROUND1_ACTIVE,
                               OLD_SABER_GROUND1_BUFFER_OPEN + 2))
    buffer_ok = false;
  for (unsigned tick = OLD_SABER_GROUND1_BUFFER_OPEN + 3;
       tick < OLD_SABER_GROUND1_CHAIN_OPEN; ++tick)
    frame(0);
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 1, 2,
                               OLD_SABER_GROUND2_STARTUP,
                               OLD_SABER_GROUND2_ACTIVE, 0))
    buffer_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND2_TOTAL; ++tick) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.kind == SABER_KIND_GROUND3) saw_ground3 = true;
  }
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(buffer_ok && !saw_ground3 && snapshot.phase == SABER_PHASE_IDLE,
        "one early Y edge buffers at most one combo entry");

  /* A held Y is one physical edge, even when the chain windows pass. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned tick = 0; tick < OLD_SABER_GROUND1_TOTAL + 2; ++tick) {
    frame(SNES_PAD_Y);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.kind != SABER_KIND_NONE && snapshot.kind != SABER_KIND_GROUND1)
      held_ok = false;
  }
  check(held_ok && snapshot.phase == SABER_PHASE_IDLE,
        "holding Y never chains beyond ground slash 1");

  /* A press on the frame after chain-close is rejected while the swing is
   * completing; a later edge after the idle frame starts slash 1. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_TOTAL; ++tick)
    frame(0);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (snapshot.phase != SABER_PHASE_IDLE)
    boundary_ok = false;
  frame(0);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 0, 1,
                               OLD_SABER_GROUND1_STARTUP,
                               OLD_SABER_GROUND1_ACTIVE, 0))
    boundary_ok = false;
  check(boundary_ok, "out-of-window Y is ignored until the post-IDLE press");

  /* Direction is locked on the first frame of the accepted next swing. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y | SNES_PAD_RIGHT);
  if (g_ram[0x0c11] != 0x40 || !(g_ram[0x0bb9] & 0x40))
    facing_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_OPEN; ++tick) {
    frame(SNES_PAD_LEFT);
    if (g_ram[0x0c11] != 0x40 || !(g_ram[0x0bb9] & 0x40))
      facing_ok = false;
  }
  frame(SNES_PAD_Y | SNES_PAD_LEFT);
  snapshot = MmxSaberAttackSnapshotGet();
  if (!ground_snapshot_matches(&snapshot, 1, 2,
                               OLD_SABER_GROUND2_STARTUP,
                               OLD_SABER_GROUND2_ACTIVE, 0) ||
      g_ram[0x0c11] != 0 || (g_ram[0x0bb9] & 0x40))
    facing_ok = false;
  check(facing_ok,
        "held direction turns only when the next ground swing is accepted");

  /* Preserve the old pre-player VX stop through all three accepted swings. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  uint16_t initial_x = (uint16_t)read_ram_word(g_ram, 0x0bad);
  for (unsigned tick = 0; tick < 12; ++tick)
    frame(SNES_PAD_RIGHT);
  running_x = (uint16_t)read_ram_word(g_ram, 0x0bad);
  check(running_x != initial_x, "right input establishes horizontal movement");
  frame(SNES_PAD_Y | SNES_PAD_RIGHT);
  stopped_x = (uint16_t)read_ram_word(g_ram, 0x0bad);
  if (stopped_x != running_x || g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0)
    velocity_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_CLOSE; ++tick) {
    frame(SNES_PAD_RIGHT);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE) {
      if (read_ram_word(g_ram, 0x0bad) != stopped_x) position_ok = false;
      if (g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0) velocity_ok = false;
    }
  }
  frame(SNES_PAD_Y | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  if (snapshot.kind != SABER_KIND_GROUND2 ||
      read_ram_word(g_ram, 0x0bad) != stopped_x ||
      g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0)
    velocity_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND2_CHAIN_CLOSE; ++tick) {
    frame(SNES_PAD_RIGHT);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE) {
      if (read_ram_word(g_ram, 0x0bad) != stopped_x) position_ok = false;
      if (g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0) velocity_ok = false;
    }
  }
  frame(SNES_PAD_Y | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  if (snapshot.kind != SABER_KIND_GROUND3 ||
      read_ram_word(g_ram, 0x0bad) != stopped_x ||
      g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0)
    velocity_ok = false;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND3_TOTAL; ++tick) {
    frame(SNES_PAD_RIGHT);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE) {
      if (read_ram_word(g_ram, 0x0bad) != stopped_x) position_ok = false;
      if (g_ram[0x0bc2] != 0 || g_ram[0x0bc3] != 0) velocity_ok = false;
    }
  }
  check(position_ok && velocity_ok,
        "ground combo holds position and writes zero VX on every swing frame");
  puts("ok: saber-ground-combo");
}

static bool saber_lifecycle_idle(void) {
  MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
  return snapshot.phase == SABER_PHASE_IDLE &&
      snapshot.kind == SABER_KIND_NONE && tagged_projectiles() == 0 &&
      MmxSaberAttackHitSlots() == 0;
}

static void saber_ground_lifecycle_checks(const char *fixture) {
  MmxSaberAttackSnapshot snapshot;
  unsigned cues_before;
  unsigned charge_before;

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  check(MmxSaberAttackCueCount() == 1 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_1,
        "one accepted ground slash emits exactly one saber_1 cue");
  for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 5; ++i)
    frame(SNES_PAD_Y);
  check(MmxSaberAttackCueCount() == 1,
        "holding Y emits no second cue for the same slash");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  check(MmxSaberAttackCueCount() == 1 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_1,
        "ground combo cue 1 is emitted after native player end");
  for (unsigned i = 1; i < OLD_SABER_GROUND1_CHAIN_CLOSE; ++i)
    frame(0);
  frame(SNES_PAD_Y);
  check(MmxSaberAttackCueCount() == 2 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_2,
        "ground combo cue 2 is emitted once in order");
  for (unsigned i = 1; i < OLD_SABER_GROUND2_CHAIN_CLOSE; ++i)
    frame(0);
  frame(SNES_PAD_Y);
  check(MmxSaberAttackCueCount() == 3 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_3,
        "ground combo cue 3 is emitted once in order");
  idle(OLD_SABER_GROUND3_TOTAL + 2);
  check(saber_lifecycle_idle(),
        "natural recovery end returns Saber idle and clears ownership");

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(10, SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 8 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    frame(SNES_PAD_X);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.phase == SABER_PHASE_ACTIVE,
        "hurt lifecycle probe reaches an active Saber frame");
  charge_before = MmxZeroGetState().charge;
  cues_before = MmxSaberAttackCueCount();
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  frame(SNES_PAD_X);
  check(saber_lifecycle_idle() &&
            MmxSaberAttackCueCount() == cues_before &&
            MmxZeroGetState().charge == charge_before,
        "hurt exits idle, releases the slot, clears the mask, and preserves charge");
  idle(12);
  check(MmxSaberAttackCueCount() == cues_before,
        "hurt exit emits no later Saber cue");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 5; ++i) frame(0);
  charge_before = MmxZeroGetState().charge;
  MmxSaberAttackExit(g_ram, MMX_SABER_ATTACK_EXIT_HURT);
  check(saber_lifecycle_idle() && MmxZeroGetState().charge == charge_before,
        "central hurt exit releases ownership without touching charge");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 5; ++i) frame(0);
  cues_before = MmxSaberAttackCueCount();
  switch_to_x();
  if (!saber_lifecycle_idle()) frame(0);
  check(saber_lifecycle_idle() && MmxSaberAttackCueCount() == cues_before,
        "exchange to X returns Saber idle and releases its slot");
  frame(SNES_PAD_X);
  check(!MmxSaberFrameLastWroteInput(),
        "X controls remain native after exchanging out of a slash");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 5; ++i) frame(0);
  check(tagged_projectiles() == 1,
        "plugin reset probe has a live Saber-tagged slot");
  MmxSaberFrameReset();
  check(saber_lifecycle_idle(),
        "plugin reset returns Saber idle and releases tagged RAM ownership");
  puts("ok: saber-ground-lifecycle");
}

static const uint8_t kSaberGroundBounds[40] = {
    7, 228, 12, 10, 28, 237, 19, 19, 36, 1, 17, 15, 36, 10, 17, 6,
    24, 251, 14, 13, 13, 251, 42, 13, 235, 251, 18, 13,
    248, 236, 19, 14, 30, 247, 37, 29, 38, 252, 30, 25};

static const uint8_t kSaberAirBounds[40] = {
    16, 232, 17, 12, 14, 240, 30, 19, 13, 244, 42, 23, 243, 242, 18, 11,
    31, 246, 33, 19, 22, 0, 23, 15, 14, 1, 16, 12, 24, 1, 37, 17,
    45, 0, 30, 17, 14, 241, 24, 6};

static const uint8_t kUnchangedSlash2Bounds[12] = {
    24, 251, 14, 13, 13, 251, 42, 13, 235, 251, 18, 13};

static const uint8_t kUnchangedWallBounds[12] = {
    31, 246, 33, 19, 22, 0, 23, 15, 14, 1, 16, 12};

static int saber_record_top(const uint8_t record[4]) {
  return (int8_t)record[1] - record[3];
}

static int saber_record_bottom(const uint8_t record[4]) {
  return (int8_t)record[1] + record[3];
}

static void saber_refit_rom_checks(void) {
  static const uint8_t old_slash1_late[4] = {37, 0, 16, 8};
  static const uint8_t old_slash3_late[4] = {39, 245, 29, 19};
  static const uint8_t old_air_late[4] = {244, 250, 18, 12};
  static const uint8_t old_dash_middle[4] = {46, 254, 29, 10};
  const uint8_t *ground = g_snes->cart->rom + 0x37fd8;
  const uint8_t *air = g_snes->cart->rom + 0x37f40;
  const uint8_t *slash1_late = ground + 12;
  const uint8_t *slash3_late = ground + 36;
  const uint8_t *air_late = air + 12;
  const uint8_t *dash_middle = air + 32;

  printf("reference: refit edges slash1_bottom=%d/%d slash3_bottom=%d/%d "
         "air_top_bottom=%d,%d/%d,%d dash_top_bottom=%d,%d/%d,%d\n",
         saber_record_bottom(slash1_late),
         saber_record_bottom(old_slash1_late),
         saber_record_bottom(slash3_late),
         saber_record_bottom(old_slash3_late),
         saber_record_top(air_late), saber_record_bottom(air_late),
         saber_record_top(old_air_late), saber_record_bottom(old_air_late),
         saber_record_top(dash_middle), saber_record_bottom(dash_middle),
         saber_record_top(old_dash_middle),
         saber_record_bottom(old_dash_middle));

  check(!memcmp(slash1_late, kSaberGroundBounds + 12, 4),
        "slash 1 late ROM record installs the measured bytes");
  check(saber_record_bottom(slash1_late) >
                saber_record_bottom(old_slash1_late) &&
            saber_record_bottom(slash1_late) > 0,
        "slash 1 late ROM record reaches below the feet line");
  check(!memcmp(slash3_late, kSaberGroundBounds + 36, 4),
        "slash 3 late ROM record installs the measured bytes");
  check(saber_record_bottom(slash3_late) >
                saber_record_bottom(old_slash3_late) &&
            saber_record_bottom(slash3_late) > 0,
        "slash 3 late ROM record reaches below the feet line");
  check(!memcmp(air_late, kSaberAirBounds + 12, 4),
        "air late ROM record installs the measured bytes");
  check(saber_record_top(air_late) < saber_record_top(old_air_late) &&
            saber_record_bottom(air_late) < saber_record_bottom(old_air_late),
        "air late ROM record moves both its top and bottom upward");
  check(!memcmp(dash_middle, kSaberAirBounds + 32, 4),
        "dash ROM record installs the measured bytes");
  check(saber_record_top(dash_middle) <
                saber_record_top(old_dash_middle) &&
            saber_record_bottom(dash_middle) >
                saber_record_bottom(old_dash_middle),
        "dash ROM record grows at both the top and bottom");
  check(!memcmp(ground + 16, kUnchangedSlash2Bounds,
                sizeof(kUnchangedSlash2Bounds)),
        "slash 2 ROM records remain byte-identical");
  check(!memcmp(air + 16, kUnchangedWallBounds,
                sizeof(kUnchangedWallBounds)),
        "wall ROM records remain byte-identical");
}

static unsigned abs_difference(unsigned a, unsigned b) {
  return a > b ? a - b : b - a;
}

static unsigned response_enemy_near_player(void) {
  const unsigned zero_x = read_ram_word(g_ram, 0x0bad);
  const unsigned zero_y = read_ram_word(g_ram, 0x0bb0);
  unsigned best = 0;
  unsigned best_distance = 0xffff;
  for (unsigned d = 0xe68; d < 0x1228; d += 64) {
    unsigned enemy_x, enemy_y, distance;
    if (!g_ram[d] || !g_ram[d + 14] || !g_ram[d + 0x27] ||
        g_ram[d + 10] != 2 || g_ram[d + 0x30])
      continue;
    enemy_x = read_ram_word(g_ram, d + 5);
    enemy_y = read_ram_word(g_ram, d + 8);
    distance = abs_difference(enemy_x, zero_x) + abs_difference(enemy_y, zero_y);
    if (distance < best_distance) {
      best = d;
      best_distance = distance;
    }
  }
  return best;
}

static unsigned walk_to_response_enemy(const char *fixture,
                                       unsigned *walk_frames) {
  unsigned target = 0;
  load_response_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < 300 && !target; ++i) {
    target = response_enemy_near_player();
    if (target) {
      const int dx = (int)read_ram_word(g_ram, target + 5) -
          (int)read_ram_word(g_ram, 0x0bad);
      const int dy = (int)read_ram_word(g_ram, target + 8) -
          (int)read_ram_word(g_ram, 0x0bb0);
      if (abs(dx) <= 48 && abs(dy) <= 48) {
        if (walk_frames) *walk_frames = i;
        return target;
      }
      target = 0;
      frame(dx >= 0 ? SNES_PAD_RIGHT : SNES_PAD_LEFT);
    } else {
      frame(0);
    }
  }
  if (walk_frames) *walk_frames = 300;
  return target;
}

static unsigned reachable_ground_enemy(void) {
  const unsigned zero_x = read_ram_word(g_ram, 0x0bad);
  const unsigned zero_y = read_ram_word(g_ram, 0x0bb0);
  unsigned best = 0;
  unsigned best_distance = 0xffff;
  for (unsigned d = 0xe68; d < 0x1228; d += 64) {
    unsigned enemy_x, enemy_y, distance;
    if (!g_ram[d] || !g_ram[d + 14] || (g_ram[d + 0x27] & 127) <= 3 ||
        g_ram[d + 0x28] >= 6)
      continue;
    enemy_x = read_ram_word(g_ram, d + 5);
    enemy_y = read_ram_word(g_ram, d + 8);
    if (enemy_x < zero_x || enemy_x - zero_x > 48 ||
        abs_difference(enemy_y, zero_y) > 48)
      continue;
    distance = (enemy_x - zero_x) + abs_difference(enemy_y, zero_y);
    if (distance < best_distance) {
      best = d;
      best_distance = distance;
    }
  }
  return best;
}

static unsigned walk_to_ground_enemy(const char *fixture,
                                     unsigned *walk_frames) {
  unsigned target = 0;
  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < 300 && !target; ++i) {
    frame(i < 180 ? SNES_PAD_RIGHT : SNES_PAD_RIGHT | SNES_PAD_R);
    target = reachable_ground_enemy();
    if (target && walk_frames) *walk_frames = i + 1;
  }
  return target;
}

typedef struct {
  unsigned target;
  unsigned walk_frames;
  unsigned initial_hp;
  unsigned after_first_hp;
  unsigned native_final_hp;
  unsigned final_hp;
  unsigned first_damage;
  unsigned iframe_damage;
  unsigned override_damage;
  unsigned calls;
  unsigned enemy;
  unsigned projectile;
  unsigned value;
  unsigned first_frame;
  unsigned first_enemy;
  unsigned first_projectile;
  unsigned first_value;
  unsigned iframe_frame;
  unsigned iframe_enemy;
  unsigned iframe_projectile;
  unsigned iframe_value;
  unsigned iframe_timer;
  unsigned override_frame;
  unsigned override_enemy;
  unsigned override_projectile;
  unsigned override_value;
  unsigned override_timer;
  unsigned zero_response_calls;
  unsigned damage_calls;
  unsigned native_damage_calls;
  unsigned zero_damage_calls;
  unsigned zero_damage_frame;
  unsigned zero_damage_enemy;
  unsigned zero_damage_projectile;
  unsigned zero_damage_value;
} ZeroResponseRun;

static void zero_response_contact_run(const char *fixture, bool use_extension,
                                      ZeroResponseRun *run) {
  memset(run, 0, sizeof(*run));
  run->target = walk_to_ground_enemy(fixture, &run->walk_frames);
  check(run->target == 0xea8 && run->walk_frames == 213,
        "response seam walk reaches Highway enemy $0EA8 in 213 frames");
  run->initial_hp = g_ram[run->target + 0x27] & 127;
  memset(&zero_response_probe, 0, sizeof(zero_response_probe));
  zero_response_probe.current_frame = 1;
  MmxZeroSetExtension(use_extension ? &zero_response_observer_extension : NULL);

  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 90; ++i) {
    zero_response_probe.current_frame = i + 2;
    frame(0);
  }

  MmxZeroSetExtension(NULL);
  run->final_hp = g_ram[run->target + 0x27] & 127;
  run->first_damage = run->initial_hp > run->final_hp ?
      run->initial_hp - run->final_hp : 0;
  run->calls = zero_response_probe.calls;
  run->enemy = zero_response_probe.enemy;
  run->projectile = zero_response_probe.projectile;
  run->value = zero_response_probe.value;
}

static void zero_response_iframe_run(const char *fixture, bool override_zero,
                                     ZeroResponseRun *run) {
  unsigned frame_number = 1;
  unsigned first_hp = 0;

  memset(run, 0, sizeof(*run));
  run->target = walk_to_response_enemy(fixture, &run->walk_frames);
  check(run->target != 0,
        "response seam i-frame walk reaches a native boss enemy");
  run->initial_hp = g_ram[run->target + 0x27] & 127;
  memset(&zero_response_probe, 0, sizeof(zero_response_probe));
  zero_response_probe.override_zero = override_zero;
  zero_response_probe.chosen_damage = override_zero ? 2 : 0;
  MmxZeroSetExtension(override_zero ? &zero_response_extension :
                      &zero_response_observer_extension);

  for (unsigned attempt = 0; attempt < 120 && !first_hp; ++attempt) {
    unsigned before = g_ram[run->target + 0x27] & 127;
    zero_response_probe.current_frame = frame_number++;
    frame(attempt ? 0 : SNES_PAD_Y);
    first_hp = g_ram[run->target + 0x27] & 127;
    if (first_hp >= before) first_hp = 0;
  }
  run->after_first_hp = first_hp;

  /* Release Y before the second shot. It reaches the same enemy while the
   * native post-hit timer is active, selecting the zero response row. */
  zero_response_probe.current_frame = frame_number++;
  frame(SNES_PAD_Y);
  for (unsigned attempt = 0; attempt < 120 && !zero_response_probe.zero_frame;
       ++attempt) {
    zero_response_probe.current_frame = frame_number++;
    frame(0);
  }

  run->native_final_hp = g_ram[run->target + 0x27] & 127;
  run->native_damage_calls = zero_response_probe.damage_calls;

  MmxZeroSetExtension(NULL);
  run->final_hp = run->native_final_hp;
  if (override_zero) run->override_damage = run->after_first_hp > run->final_hp ?
      run->after_first_hp - run->final_hp : 0;

  run->first_damage = run->initial_hp > run->after_first_hp ?
      run->initial_hp - run->after_first_hp : 0;
  run->iframe_damage = run->after_first_hp > run->final_hp ?
      run->after_first_hp - run->final_hp : 0;
  run->calls = zero_response_probe.calls;
  run->enemy = zero_response_probe.enemy;
  run->projectile = zero_response_probe.projectile;
  run->value = zero_response_probe.value;
  run->first_frame = zero_response_probe.first_positive_frame;
  run->first_enemy = zero_response_probe.first_positive_enemy;
  run->first_projectile = zero_response_probe.first_positive_projectile;
  run->first_value = zero_response_probe.first_positive_value;
  run->iframe_frame = zero_response_probe.zero_frame;
  run->iframe_enemy = zero_response_probe.zero_enemy;
  run->iframe_projectile = zero_response_probe.zero_projectile;
  run->iframe_value = zero_response_probe.zero_value;
  run->iframe_timer = zero_response_probe.zero_timer;
  run->override_frame = zero_response_probe.override_frame;
  run->override_enemy = zero_response_probe.override_enemy;
  run->override_projectile = zero_response_probe.override_projectile;
  run->override_value = zero_response_probe.override_value;
  run->override_timer = zero_response_probe.override_timer;
  run->zero_response_calls = zero_response_probe.zero_response_calls;
  run->damage_calls = zero_response_probe.damage_calls;
  run->zero_damage_calls = zero_response_probe.zero_damage_calls;
  run->zero_damage_frame = zero_response_probe.zero_damage_frame;
  run->zero_damage_enemy = zero_response_probe.zero_damage_enemy;
  run->zero_damage_projectile = zero_response_probe.zero_damage_projectile;
  run->zero_damage_value = zero_response_probe.zero_damage_value;
  printf("reference: iframe probe target=0x%X walk=%u first=(%u,%u,0x%X) "
         "native_zero=(%u,%u,0x%X,timer=0x%02X) "
         "override_zero=(%u,%u,0x%X,timer=0x%02X) "
         "HP=%u->%u->%u->%u damage=%u/%u/%u calls=%u/%u\n",
         run->target, run->walk_frames, run->first_frame, run->first_value,
         run->first_projectile, run->iframe_frame, run->iframe_value,
         run->iframe_projectile, run->iframe_timer, run->override_frame,
         run->override_value, run->override_projectile, run->override_timer,
         run->initial_hp, run->after_first_hp, run->native_final_hp,
         run->final_hp, run->first_damage, run->iframe_damage,
         run->override_damage, run->native_damage_calls, run->damage_calls);
}

static void zero_response_seam_checks(const char *fixture,
                                      const char *fixture_dir) {
  ZeroResponseRun baseline, passthrough, native_iframe, overridden_iframe;
  char fallback[4096];

  check(!MmxSaberEnabled(),
        "zero-response-seam runs with Saber disabled");
  zero_response_contact_run(fixture, false, &baseline);
  check(baseline.first_damage != 0,
        "upstream buster contact loses HP without an extension");

  zero_response_contact_run(fixture, true, &passthrough);
  check(passthrough.calls != 0 && passthrough.enemy == passthrough.target &&
            passthrough.projectile >= 0x1228 &&
            passthrough.projectile < 0x1428 &&
            (passthrough.projectile & 63) == 0x28,
        "response callback records the Highway enemy and buster slot");
  check(passthrough.first_damage == baseline.first_damage,
        "returning the original response preserves native HP loss");

  check(snprintf(fallback, sizeof(fallback), "%s/%s", fixture_dir,
                 "penguin-fight.sav") < (int)sizeof(fallback),
        "response seam fallback fixture path fits");
  check(readable_file(fallback),
        "response seam fallback penguin fixture exists");
  printf("reference: save0 Highway buster contacts stayed positive; "
         "using penguin-fight.sav native i-frame fallback\n");
  zero_response_iframe_run(fallback, false, &native_iframe);
  check(native_iframe.first_value != 0 && native_iframe.first_damage != 0 &&
            native_iframe.iframe_frame != 0 && native_iframe.iframe_value == 0,
        "the second buster contact observes native zero response during i-frames");
  check(native_iframe.iframe_damage == 0 &&
            native_iframe.native_final_hp == native_iframe.after_first_hp &&
            native_iframe.native_damage_calls == 0 &&
            native_iframe.zero_damage_calls == 0,
        "native zero response skips damage and leaves the i-frame HP unchanged");

  RtlReset(1);
  zero_response_iframe_run(fallback, true, &overridden_iframe);
  check(overridden_iframe.first_value != 0 &&
            overridden_iframe.first_damage == native_iframe.first_damage &&
            overridden_iframe.iframe_frame != 0 &&
            overridden_iframe.iframe_value == 0,
        "the override run reaches the same native zero response contact");
  check(overridden_iframe.override_frame == overridden_iframe.iframe_frame &&
            overridden_iframe.override_value == 0 &&
            overridden_iframe.override_enemy == overridden_iframe.target &&
            overridden_iframe.override_damage == 2 &&
            overridden_iframe.iframe_damage == 2 &&
            overridden_iframe.final_hp == overridden_iframe.after_first_hp - 2,
        "response zero-to-one override lets the damage callback drop HP by 2");
  check(overridden_iframe.zero_damage_calls == 1 &&
            overridden_iframe.damage_calls == 2 &&
            overridden_iframe.zero_damage_frame == overridden_iframe.override_frame &&
            overridden_iframe.zero_damage_enemy == overridden_iframe.override_enemy &&
            overridden_iframe.zero_damage_projectile == overridden_iframe.override_projectile &&
            overridden_iframe.zero_damage_value == 0,
        "the i-frame damage callback runs after the response override");
  check(native_iframe.iframe_frame == overridden_iframe.iframe_frame &&
            native_iframe.iframe_enemy == overridden_iframe.override_enemy &&
            native_iframe.iframe_projectile == overridden_iframe.override_projectile &&
            native_iframe.target == overridden_iframe.target &&
            native_iframe.walk_frames == overridden_iframe.walk_frames,
        "native and overridden i-frame contacts use the same enemy and shot");

  printf("reference: response path=%s first_frame=%u first_value=%u "
         "native_i_frame_frame=%u native_i_frame_value=%u "
         "override_i_frame_frame=%u override_i_frame_value=%u "
         "timer=0x%02X initial_hp=%u after_first=%u native_final=%u "
         "override_final=%u damage_calls=%u/%u\n",
         getenv("SNESRECOMP_LLE_BOUNCE") &&
             !strcmp(getenv("SNESRECOMP_LLE_BOUNCE"), "0") ?
             "interpreted" : "compiled",
         native_iframe.first_frame, native_iframe.first_value,
         native_iframe.iframe_frame, native_iframe.iframe_value,
         overridden_iframe.iframe_frame, overridden_iframe.override_value,
         native_iframe.iframe_timer, native_iframe.initial_hp,
         native_iframe.after_first_hp, native_iframe.final_hp,
         overridden_iframe.final_hp, native_iframe.damage_calls,
         overridden_iframe.damage_calls);
  puts("ok: zero-response-seam");
}

static unsigned saber_record_pointer(const MmxSaberAttackSnapshot *snapshot) {
  const MmxSaberAttack *attack;
  if (!snapshot) return 0;
  attack = MmxSaberAttackRecord(snapshot->kind, snapshot->index);
  if (!attack) return 0;
  for (unsigned i = 0; i < attack->bounds_segment_count; ++i) {
    const MmxSaberBoundsSegment *segment = attack->bounds_segments + i;
    if (snapshot->tick >= segment->first_tick &&
        snapshot->tick <= segment->last_tick)
      return attack->bounds_pointer + i * 4;
  }
  return 0;
}

static unsigned saber_active_slot(void) {
  unsigned slot = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (saber_tagged_projectile(d)) {
      if (slot) return 0;
      slot = d;
    }
  return slot;
}

static void saber_ground_hit_checks(const char *fixture) {
  uint8_t saved_ground[40];
  unsigned target, walk_frames = 0, slot, bit, initial_hp;
  unsigned damage, contacts, first_contact_frame;
  uint16_t mask_on_contact = 0;
  bool startup_empty, active_ok, release_ok, mask_ok;
  uint16_t active_slot_x = 0;
  MmxSaberAttackSnapshot snapshot;

  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target != 0, "Highway walk reaches an ordinary ground-slash target");
  printf("reference: Highway ground-hit walk_frames=%u Zero=(%u,%u) "
         "enemy_slot=0x%X enemy=(%u,%u)\n",
         walk_frames, read_ram_word(g_ram, 0x0bad), read_ram_word(g_ram, 0x0bb0),
         target, read_ram_word(g_ram, target + 5), read_ram_word(g_ram, target + 8));

  frame(0);
  check(!memcmp(g_snes->cart->rom + 0x37fd8, kSaberGroundBounds, 40) &&
            !memcmp(g_snes->cart->rom + 0x37f40, kSaberAirBounds, 40),
        "Saber collision windows install the measured ground and air rectangles");

  initial_hp = g_ram[target + 0x27] & 127;
  bit = 1u << ((target - 0xe68) / 64);
  startup_empty = true;
  active_ok = true;
  release_ok = true;
  mask_ok = false;
  damage = 0;
  contacts = 0;
  first_contact_frame = 0;

  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (tagged_projectiles() != 0) startup_empty = false;
  for (unsigned i = 1; i < 45 && snapshot.phase != SABER_PHASE_IDLE; ++i) {
    unsigned hp_before = g_ram[target + 0x27] & 127;
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    slot = saber_active_slot();
    if (snapshot.phase == SABER_PHASE_STARTUP && tagged_projectiles() != 0)
      startup_empty = false;
    if (snapshot.phase == SABER_PHASE_ACTIVE) {
      unsigned hp_after = g_ram[target + 0x27] & 127;
      if (slot == 0 || tagged_projectiles() != 1 ||
          read_ram_word(g_ram, slot + 5) != read_ram_word(g_ram, 0x0bad) ||
          read_ram_word(g_ram, slot + 8) != read_ram_word(g_ram, 0x0bb0) ||
          read_ram_word(g_ram, slot + 0x20) != saber_record_pointer(&snapshot))
        active_ok = false;
      else if (!active_slot_x)
        active_slot_x = read_ram_word(g_ram, slot + 5);
      if (hp_after < hp_before) {
        unsigned delta = hp_before - hp_after;
        ++contacts;
        damage += delta;
        if (!first_contact_frame) first_contact_frame = i;
        if (delta != 3 || damage != 3) active_ok = false;
        mask_on_contact = MmxSaberAttackHitSlots();
        if (mask_on_contact & bit) mask_ok = true;
      }
    } else if (snapshot.phase == SABER_PHASE_RECOVERY && active_slot_x) {
      if (tagged_projectiles() != 0) release_ok = false;
    }
  }
  check(startup_empty && active_ok && release_ok && contacts == 1 &&
            damage == 3 && (g_ram[target + 0x27] & 127) == initial_hp - 3,
        "ground slash 1 creates one anchored tagged slot and deals 3 once");
  check(mask_ok, "ground slash 1 sets the Saber enemy mask bit");
  printf("reference: ground-hit slash1 first_contact_frame=%u "
         "damage=%u mask=0x%X\n", first_contact_frame, damage,
         mask_on_contact);

  load_fixture(fixture);
  MmxSaberFrameReset();
  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target != 0, "Highway combo route retains an ordinary target");
  initial_hp = g_ram[target + 0x27] & 127;
  bit = 1u << ((target - 0xe68) / 64);
  damage = 0;
  contacts = 0;
  bool first_seen = false;
  bool second_mask = false;
  frame(SNES_PAD_Y);
  for (unsigned i = 1; i <= 29; ++i) {
    unsigned hp_before = g_ram[target + 0x27] & 127;
    frame(i == 29 ? SNES_PAD_Y : 0);
    snapshot = MmxSaberAttackSnapshotGet();
    unsigned hp_after = g_ram[target + 0x27] & 127;
    if (hp_after < hp_before) {
      unsigned delta = hp_before - hp_after;
      ++contacts;
      damage += delta;
      if (delta == 3) {
        if (!first_seen) first_seen = (MmxSaberAttackHitSlots() & bit) != 0;
        else second_mask = (MmxSaberAttackHitSlots() & bit) != 0;
      }
    }
  }
  check(snapshot.kind == SABER_KIND_GROUND2 &&
            snapshot.phase == SABER_PHASE_ACTIVE && tagged_projectiles() == 1,
        "ground slash 2 is accepted at the combo window with one new slot");
  for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_IDLE; ++i) {
    unsigned hp_before = g_ram[target + 0x27] & 127;
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    unsigned hp_after = g_ram[target + 0x27] & 127;
    if (hp_after < hp_before) {
      unsigned delta = hp_before - hp_after;
      ++contacts;
      damage += delta;
      if (delta == 3 && contacts == 2)
        second_mask = (MmxSaberAttackHitSlots() & bit) != 0;
    }
  }
  check(first_seen && contacts == 2 && damage == 6 && second_mask &&
            (g_ram[target + 0x27] & 127) == initial_hp - 6,
        "ground slash 2 can hit the same enemy again for 3 with a fresh mask");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(0);
  memcpy(saved_ground, g_snes->cart->rom + 0x37fd8, sizeof(saved_ground));
  unsigned warning_before = MmxSaberAttackCollisionWarningCount();
  g_snes->cart->rom[0x37fd8] ^= 1;
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  check(MmxSaberAttackCollisionWarningCount() == warning_before + 1,
        "foreign collision data logs once and fails closed");
  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target != 0, "foreign-window route reaches an ordinary target");
  frame(SNES_PAD_Y);
  bool foreign_animation = false;
  bool foreign_no_hitbox = true;
  for (unsigned i = 0; i < 18; ++i) {
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE) foreign_animation = true;
    slot = saber_active_slot();
    if (snapshot.phase == SABER_PHASE_ACTIVE &&
        (slot == 0 || read_ram_word(g_ram, slot + 0x20) != 0))
      foreign_no_hitbox = false;
    frame(0);
  }
  check(foreign_animation && foreign_no_hitbox,
        "foreign collision data leaves slashes animating with no hitbox");
  memcpy(g_snes->cart->rom + 0x37fd8, saved_ground, sizeof(saved_ground));
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  check(!memcmp(g_snes->cart->rom + 0x37fd8, kSaberGroundBounds, 40),
        "restored collision window is owned and idempotent");
  puts("ok: saber-ground-hit");
}

static MmxSaberPadPhase old_air_phase(unsigned tick) {
  return tick < OLD_SABER_AIR_STARTUP ? SABER_PHASE_STARTUP :
      tick < OLD_SABER_AIR_STARTUP + OLD_SABER_AIR_ACTIVE ?
          SABER_PHASE_ACTIVE : SABER_PHASE_RECOVERY;
}

static unsigned record_jump_arc(const char *fixture, bool slash,
                                bool short_hop, unsigned y[96]) {
  bool airborne = false;
  unsigned count = 0;

  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned frame_number = 1; frame_number < 96; ++frame_number) {
    unsigned input = short_hop ?
        (frame_number < 6 ? SNES_PAD_B : 0) :
        (frame_number <= 20 ? SNES_PAD_B : 0);
    bool grounded;
    if (slash && frame_number == 3) input |= SNES_PAD_Y;
    frame(input);
    y[count++] = read_ram_word(g_ram, 0x0bb0);
    grounded = (g_ram[0xbd3] & 4) || (g_ram[0xbd4] & 4);
    if (!grounded)
      airborne = true;
    else if (airborne)
      return count;
  }
  return 0;
}

static unsigned empty_enemy_slot(void) {
  for (unsigned d = 0xe68; d < 0x1228; d += 64)
    if (!g_ram[d]) return d;
  return 0xe68;
}

static void saber_air_checks(const char *fixture) {
  const MmxSaberAttack *air = MmxSaberAttackRecord(SABER_KIND_AIR, 0);
  unsigned reference[96], slash[96], short_reference[96], short_slash[96];
  unsigned reference_count, slash_count, short_reference_count, short_slash_count;
  MmxSaberAttackSnapshot snapshot;
  bool phase_ok = true;
  bool startup_empty = true;
  bool active_ok = true;
  bool press_during_ok = false;
  bool first_end = false;
  bool second_start = false;
  unsigned starts = 0;
  bool was_active = false;
  unsigned slot = 0;
  unsigned direct_enemy = 0;
  bool direct_damage_ok = false;
  bool saw_land_after_second = false;

  printf("reference: old Saber air timing startup=%u active=%u recovery=%u "
         "total=%u (oldsaber src/mmx_saber.c:322-339)\n",
         OLD_SABER_AIR_STARTUP, OLD_SABER_AIR_ACTIVE,
         OLD_SABER_AIR_RECOVERY, OLD_SABER_AIR_TOTAL);
  check(air && air->visual_animation == 4 &&
            air->startup_ticks == OLD_SABER_AIR_STARTUP &&
            air->active_ticks == OLD_SABER_AIR_ACTIVE &&
            air->recovery_ticks == OLD_SABER_AIR_RECOVERY &&
            air->total_ticks == OLD_SABER_AIR_TOTAL && air->damage == 3 &&
            air->bounds_pointer == MMX_SABER_AIR_BOUNDS_POINTER,
        "air record publishes animation 4, old timing, $FF40, and damage 3");

  reference_count = record_jump_arc(fixture, false, false, reference);
  slash_count = record_jump_arc(fixture, true, false, slash);
  short_reference_count = record_jump_arc(fixture, false, true, short_reference);
  short_slash_count = record_jump_arc(fixture, true, true, short_slash);
  check(reference_count != 0 && slash_count == reference_count &&
            !memcmp(reference, slash, reference_count * sizeof(reference[0])),
        "early air slash with B held preserves the complete native jump arc");
  check(short_reference_count != 0 &&
            short_slash_count == short_reference_count &&
            !memcmp(short_reference, short_slash,
                    short_reference_count * sizeof(short_reference[0])),
        "air slash preserves the native short-hop B-release cutoff");
  printf("reference: native air arc frames=%u short-hop frames=%u\n",
         reference_count, short_reference_count);

  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned frame_number = 1; frame_number <= 20; ++frame_number) {
    const unsigned input = frame_number <= 20 ?
        (SNES_PAD_B | (frame_number == 3 ? SNES_PAD_Y : 0)) : 0;
    frame(input);
    snapshot = MmxSaberAttackSnapshotGet();
    if (frame_number < 3) {
      if (snapshot.phase != SABER_PHASE_IDLE)
        phase_ok = false;
    } else {
      const unsigned tick = frame_number - 3;
      if (snapshot.kind != SABER_KIND_AIR || snapshot.index != 0 ||
          snapshot.anim_id != 4 || snapshot.tick != tick ||
          snapshot.phase != old_air_phase(tick))
        phase_ok = false;
    }
  }
  check(phase_ok && MmxSaberAttackCueCount() == 1 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_1,
        "air slash publishes animation 4 through the old startup/active/recovery phases and cues saber_1");

  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned frame_number = 1; frame_number <= 20; ++frame_number) {
    frame(frame_number <= 20 ?
        (SNES_PAD_B | (frame_number == 3 ? SNES_PAD_Y : 0)) : 0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase == SABER_PHASE_STARTUP && tagged_projectiles() != 0)
      startup_empty = false;
    if (snapshot.phase == SABER_PHASE_ACTIVE) {
      unsigned expected_pointer = saber_record_pointer(&snapshot);
      slot = saber_active_slot();
      if (slot == 0 || tagged_projectiles() != 1 || expected_pointer == 0 ||
          read_ram_word(g_ram, slot + 0x20) != expected_pointer)
        active_ok = false;
      else if (!direct_enemy) {
        direct_enemy = empty_enemy_slot();
        direct_damage_ok = direct_enemy != 0 &&
            MmxSaberAttackDamage(g_ram, direct_enemy, slot, 1) == 3 &&
            MmxSaberAttackDamage(g_ram, direct_enemy, slot, 1) == 0 &&
            (MmxSaberAttackHitSlots() &
             (uint16_t)(1u << ((direct_enemy - 0xe68) / 64)));
      }
    }
  }
  check(!memcmp(g_snes->cart->rom + 0x37f40, kSaberAirBounds, 40),
        "air slash keeps the measured $37F40 collision records installed");
  check(startup_empty && active_ok,
        "air ACTIVE owns one tagged slot anchored to its old air collision record");
  check(direct_damage_ok,
        "air collision damage is 3 once per enemy per swing (direct fallback)");
  puts("reference: airborne enemy contact is not required by this fixture; the air-hit fallback asserts the live tagged slot, $FF40 record, and damage callback");

  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned frame_number = 1; frame_number <= 22; ++frame_number) {
    unsigned input = SNES_PAD_B;
    if (frame_number == 3 || frame_number == 5 || frame_number == 22)
      input |= SNES_PAD_Y;
    frame(input);
    snapshot = MmxSaberAttackSnapshotGet();
    if (!was_active && snapshot.phase != SABER_PHASE_IDLE) ++starts;
    if (frame_number == 5)
      press_during_ok = snapshot.kind == SABER_KIND_AIR &&
          snapshot.tick == 2 && MmxSaberAttackCueCount() == 1;
    if (frame_number == 21)
      first_end = snapshot.phase == SABER_PHASE_IDLE &&
          snapshot.kind == SABER_KIND_NONE;
    if (frame_number == 22)
      second_start = snapshot.kind == SABER_KIND_AIR &&
          snapshot.anim_id == 4 && snapshot.tick == 0;
    was_active = snapshot.phase != SABER_PHASE_IDLE;
  }
  check(press_during_ok,
        "a Y press during an air slash does not restart or cue another swing");
  check(first_end && second_start && starts == 2 &&
            MmxSaberAttackCueCount() == 2,
        "a second airborne Y press after completion starts a second air slash and cue in one jump");
  for (unsigned i = 0; i < 40; ++i) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.kind == SABER_KIND_SABER_LAND || snapshot.anim_id == 7)
      saw_land_after_second = true;
  }
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            tagged_projectiles() == 0 && !saw_land_after_second,
        "air slash landing/natural cleanup reaches idle without SaberLand");
  puts("ok: saber-air");
}

static bool saber_test_grounded(void) {
  return (g_ram[0xbd3] & 4) || (g_ram[0xbd4] & 4);
}

typedef struct SaberDashSample {
  unsigned x;
  int vx;
  uint8_t facing;
} SaberDashSample;

static int saber_signed_ram_word(const uint8_t *ram, unsigned offset) {
  return (int)(int16_t)read_ram_word(ram, offset);
}

static unsigned start_saber_dash_right(unsigned extra_input) {
  for (unsigned i = 0; i < 40; ++i) {
    frame(extra_input | SNES_PAD_A | SNES_PAD_RIGHT);
    if (g_ram[0xbaa] == 0x14 && saber_test_grounded()) return i + 1;
  }
  return 0;
}

static SaberDashSample saber_dash_sample(void) {
  return (SaberDashSample){
      read_ram_word(g_ram, 0x0bad),
      saber_signed_ram_word(g_ram, 0x0bc2),
      (uint8_t)(g_ram[0x0c11] & 0x40)};
}

static unsigned trace_native_dash(const char *fixture,
                                  SaberDashSample samples[OLD_SABER_DASH_TOTAL]) {
  unsigned started;
  load_fixture(fixture);
  MmxSaberFrameReset();
  started = start_saber_dash_right(0);
  if (!started) return 0;
  for (unsigned i = 0; i < OLD_SABER_DASH_TOTAL; ++i) {
    frame(SNES_PAD_A | SNES_PAD_RIGHT);
    samples[i] = saber_dash_sample();
  }
  return started;
}

static unsigned trace_dash_slash_motion(
    const char *fixture, unsigned direction,
    SaberDashSample samples[OLD_SABER_DASH_TOTAL]) {
  unsigned started;
  load_fixture(fixture);
  MmxSaberFrameReset();
  started = start_saber_dash_right(0);
  if (!started) return 0;
  for (unsigned i = 0; i < OLD_SABER_DASH_TOTAL; ++i) {
    frame(SNES_PAD_A | direction | (i == 0 ? SNES_PAD_Y : 0));
    samples[i] = saber_dash_sample();
  }
  return started;
}

static MmxSaberPadPhase old_dash_phase(unsigned tick) {
  return tick < OLD_SABER_DASH_STARTUP ? SABER_PHASE_STARTUP :
      tick < OLD_SABER_DASH_STARTUP + OLD_SABER_DASH_ACTIVE ?
          SABER_PHASE_ACTIVE : SABER_PHASE_RECOVERY;
}

static void saber_dash_checks(const char *fixture) {
  const MmxSaberAttack *dash = MmxSaberAttackRecord(SABER_KIND_DASH, 0);
  SaberDashSample native[OLD_SABER_DASH_TOTAL];
  SaberDashSample slash[OLD_SABER_DASH_TOTAL];
  SaberDashSample no_direction[OLD_SABER_DASH_TOTAL];
  SaberDashSample opposite[OLD_SABER_DASH_TOTAL];
  MmxSaberAttackSnapshot snapshot;
  unsigned native_start;
  unsigned slash_start;
  unsigned no_direction_start;
  unsigned opposite_start;
  bool timing_ok = true;
  bool motion_ok = true;
  bool slot_ok = true;
  bool startup_empty = true;
  bool cue_ok = true;
  bool damage_ok = false;
  unsigned active_frames = 0;
  unsigned damage_enemy = 0;

  printf("reference: old Saber dash timing startup=%u active=%u recovery=%u "
         "total=%u (oldsaber src/mmx_saber.c:362-379)\n",
         OLD_SABER_DASH_STARTUP, OLD_SABER_DASH_ACTIVE,
         OLD_SABER_DASH_RECOVERY, OLD_SABER_DASH_TOTAL);
  check(dash && dash->visual_animation == 6 &&
            dash->startup_ticks == OLD_SABER_DASH_STARTUP &&
            dash->active_ticks == OLD_SABER_DASH_ACTIVE &&
            dash->recovery_ticks == OLD_SABER_DASH_RECOVERY &&
            dash->total_ticks == OLD_SABER_DASH_TOTAL && dash->damage == 3 &&
            dash->bounds_pointer == MMX_SABER_DASH_BOUNDS_POINTER &&
            MmxSaberSfxAttackClip(MMX_SABER_SFX_ATTACK_DASH) ==
                MMX_SABER_SFX_CLIP_SABER_2,
        "dash record keeps animation 6, old timing, $FF5C, damage 3, and saber_2");

  native_start = trace_native_dash(fixture, native);
  check(native_start != 0, "Highway starts a native grounded dash to the right");
  printf("reference: Highway native dash start_frames=%u x/vx=", native_start);
  for (unsigned i = 0; i < OLD_SABER_DASH_TOTAL; ++i)
    printf("%u/%d%s", native[i].x, native[i].vx,
           i + 1 == OLD_SABER_DASH_TOTAL ? "" : ",");
  printf(" facing=0x%02X\n", native[0].facing);
  check(native[0].vx == 0x0375,
        "native Highway dash reference keeps the old X1 VX 0x0375");
  check(!memcmp(g_snes->cart->rom + 0x37f40 + 28,
                kSaberAirBounds + 28, 12),
        "dash slash keeps the measured $FF5C records in the $37F40 window");

  load_fixture(fixture);
  MmxSaberFrameReset();
  slash_start = start_saber_dash_right(0);
  check(slash_start == native_start,
        "dash slash starts from the same native dash frame as its control");
  for (unsigned i = 0; i < OLD_SABER_DASH_TOTAL; ++i) {
    frame(SNES_PAD_A | SNES_PAD_RIGHT | (i == 0 ? SNES_PAD_Y : 0));
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.kind != SABER_KIND_DASH || snapshot.index != 0 ||
        snapshot.anim_id != 6 || snapshot.tick != i ||
        snapshot.phase != old_dash_phase(i))
      timing_ok = false;
    slash[i] = saber_dash_sample();
    if (slash[i].x != native[i].x || slash[i].vx != native[i].vx)
      motion_ok = false;
    if (snapshot.phase == SABER_PHASE_STARTUP && tagged_projectiles() != 0)
      startup_empty = false;
    if (snapshot.phase == SABER_PHASE_ACTIVE) {
      const unsigned slot = saber_active_slot();
      ++active_frames;
      if (!slot || tagged_projectiles() != 1 ||
          read_ram_word(g_ram, slot + 8) != read_ram_word(g_ram, 0x0bb0) ||
          read_ram_word(g_ram, slot + 0x20) !=
              saber_record_pointer(&snapshot))
        slot_ok = false;
      if (!damage_enemy && slot) {
        damage_enemy = empty_enemy_slot();
        damage_ok = MmxSaberAttackDamage(g_ram, damage_enemy, slot, 1) == 3 &&
            MmxSaberAttackDamage(g_ram, damage_enemy, slot, 1) == 0 &&
            (MmxSaberAttackHitSlots() &
             (uint16_t)(1u << ((damage_enemy - 0xe68) / 64)));
      }
    } else if (tagged_projectiles() != 0) {
      slot_ok = false;
    }
    if (i == 0 && (MmxSaberAttackCueCount() != 1 ||
                   MmxSaberSfxLastClip() != MMX_SABER_SFX_CLIP_SABER_2))
      cue_ok = false;
  }
  printf("reference: dash slash active_frames=%u startup_empty=%u slot_ok=%u "
         "damage_ok=%u\n", active_frames, startup_empty, slot_ok, damage_ok);
  check(timing_ok,
        "dash Y starts animation 6 with the old startup/active/recovery timing");
  check(motion_ok,
        "dash slash X/VX matches the plain native dash reference every frame");
  check(active_frames == OLD_SABER_DASH_ACTIVE && startup_empty && slot_ok &&
            damage_ok,
        "dash ACTIVE owns one tagged slot and deals 3 once per swing");
  check(cue_ok && MmxSaberAttackCueCount() == 1 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_2,
        "dash slash emits exactly one saber_2 cue");
  frame(SNES_PAD_A | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            tagged_projectiles() == 0,
        "dash slash natural end releases its tagged slot");

  no_direction_start = trace_dash_slash_motion(fixture, 0, no_direction);
  opposite_start = trace_dash_slash_motion(fixture, SNES_PAD_LEFT, opposite);
  bool opposite_ok = no_direction_start == native_start &&
      opposite_start == native_start;
  for (unsigned i = 0; i < OLD_SABER_DASH_TOTAL; ++i) {
    if (no_direction[i].facing != opposite[i].facing ||
        no_direction[i].vx != opposite[i].vx ||
        no_direction[i].facing != native[i].facing ||
        no_direction[i].vx != native[i].vx)
      opposite_ok = false;
  }
  check(opposite_ok,
        "holding LEFT during dash slash cannot turn or change native VX");

  load_fixture(fixture);
  MmxSaberFrameReset();
  check(start_saber_dash_right(0) != 0, "active-jump probe starts a native dash");
  frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_Y);
  frame(SNES_PAD_A | SNES_PAD_RIGHT);
  frame(SNES_PAD_A | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_DASH && snapshot.phase == SABER_PHASE_ACTIVE,
        "active-jump probe reaches dash ACTIVE");
  frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_B);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            !saber_test_grounded(),
        "jump during dash ACTIVE exits that frame and leaves Zero airborne");

  load_fixture(fixture);
  MmxSaberFrameReset();
  check(start_saber_dash_right(0) != 0, "startup-jump probe starts a native dash");
  frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_Y);
  frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_B);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_DASH && snapshot.tick == 1 &&
            snapshot.phase == SABER_PHASE_STARTUP && saber_test_grounded(),
        "jump during dash STARTUP is masked and does not leave the slash");

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  const unsigned charge_before_dash = MmxZeroGetState().charge;
  bool charge_never_decreased = charge_before_dash != 0;
  bool shot_fired = false;
  unsigned previous_charge = charge_before_dash;
  check(start_saber_dash_right(SNES_PAD_X) != 0,
        "charged dash probe starts while X remains held");
  for (unsigned i = 0; i < 3; ++i) {
    frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_X |
          (i == 0 ? SNES_PAD_Y : 0));
    if (MmxZeroGetState().charge < previous_charge)
      charge_never_decreased = false;
    previous_charge = MmxZeroGetState().charge;
    if (native_projectiles() || MmxZeroGetState().burst) shot_fired = true;
  }
  snapshot = MmxSaberAttackSnapshotGet();
  const unsigned charge_before_jump = MmxZeroGetState().charge;
  frame(SNES_PAD_A | SNES_PAD_RIGHT | SNES_PAD_X | SNES_PAD_B);
  if (MmxZeroGetState().charge < previous_charge)
    charge_never_decreased = false;
  if (native_projectiles() || MmxZeroGetState().burst) shot_fired = true;
  check(snapshot.kind == SABER_KIND_DASH &&
            snapshot.phase == SABER_PHASE_ACTIVE && charge_before_jump >=
                charge_before_dash,
        "charged dash probe reaches ACTIVE without consuming charge");
  snapshot = MmxSaberAttackSnapshotGet();
  check(charge_never_decreased && snapshot.phase == SABER_PHASE_IDLE &&
            !saber_test_grounded() && !shot_fired && native_projectiles() == 0,
        "held X charge survives dash slash and jump-out without firing");
  puts("ok: saber-dash");
}

static bool saber_ground_cancel_charge_probe(
    const char *fixture, MmxSaberPadPhase cancel_phase, unsigned cancel_input,
    bool startup_probe) {
  MmxSaberAttackSnapshot snapshot;
  bool no_shot;
  bool accepted;
  unsigned before;
  unsigned after;

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  before = MmxZeroGetState().charge;
  frame(SNES_PAD_X | SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  if (startup_probe) {
    frame(SNES_PAD_X | SNES_PAD_B);
    snapshot = MmxSaberAttackSnapshotGet();
    accepted = snapshot.kind == SABER_KIND_GROUND1 &&
        snapshot.phase == SABER_PHASE_STARTUP && saber_test_grounded();
    no_shot = native_projectiles() == 0 && tagged_projectiles() == 0;
    for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 2 &&
         MmxSaberAttackSnapshotGet().phase != SABER_PHASE_IDLE; ++i)
      frame(SNES_PAD_X);
  } else {
    for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 2 &&
         snapshot.phase != cancel_phase; ++i) {
      frame(SNES_PAD_X);
      snapshot = MmxSaberAttackSnapshotGet();
    }
    check(snapshot.phase == cancel_phase,
          "ground cancel charge probe reaches its requested phase");
    frame(SNES_PAD_X | cancel_input);
    snapshot = MmxSaberAttackSnapshotGet();
    accepted = snapshot.phase == SABER_PHASE_IDLE &&
        snapshot.kind == SABER_KIND_NONE;
    no_shot = native_projectiles() == 0 && tagged_projectiles() == 0 &&
        projectiles(SABER_TIER_4_RELEASE_CLASS) == 0 &&
        projectiles(SABER_FULL_RELEASE_CLASS) == 0;
  }
  after = MmxZeroGetState().charge;
  frame(0);
  return accepted && no_shot && after >= before &&
      projectiles(SABER_TIER_4_RELEASE_CLASS) == 1 &&
      projectiles(SABER_FULL_RELEASE_CLASS) == 0;
}

static void saber_cancel_checks(const char *fixture, const char *fixture_dir) {
  static const SaberWallRoute route = {
    "OPEN-RIGHT", SNES_PAD_LEFT, SNES_PAD_B | SNES_PAD_LEFT,
    SNES_PAD_LEFT, 60, 20, 5142, 2665, 0x40, 1};
  const char *const fixture_name = "armadillo-fight.sav";
  char wall_path[4096];
  MmxSaberAttackSnapshot snapshot;
  unsigned wall_frame;
  int written;

  written = snprintf(wall_path, sizeof(wall_path), "%s/%s", fixture_dir,
                     fixture_name);
  check(written >= 0 && written < (int)sizeof(wall_path),
        "cancel wall fixture path fits");
  printf("reference: W3.3 post-native cancel observation uses old "
         "oldsaber/src/mmx_saber.c:1197-1210 action values "
         "$04/$06/$08/$12/$14; $10 remains ordinary per "
         "oldsaber/src/mmx_saber.c:1045-1065\n");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  frame(SNES_PAD_B);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_GROUND1 &&
            snapshot.phase == SABER_PHASE_STARTUP && saber_test_grounded(),
        "ground slash 1 STARTUP masks jump and does not cancel or leave ground");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  do {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  } while (snapshot.phase != SABER_PHASE_ACTIVE);
  frame(SNES_PAD_B);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            !saber_test_grounded() &&
            (g_ram[0x0baa] == 0x04 || g_ram[0x0baa] == 0x06 ||
             g_ram[0x0baa] == 0x08),
        "ground slash 1 ACTIVE exits in the native accepted jump frame");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  do {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  } while (snapshot.phase != SABER_PHASE_RECOVERY);
  frame(SNES_PAD_A | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            saber_test_grounded() && g_ram[0x0baa] == 0x14,
        "ground slash 1 RECOVERY exits in the native accepted dash frame");

  check(saber_ground_cancel_charge_probe(
            fixture, SABER_PHASE_STARTUP, SNES_PAD_B, true),
        "held tier-4 charge survives the masked STARTUP jump and one release fires class 1");
  check(saber_ground_cancel_charge_probe(
            fixture, SABER_PHASE_ACTIVE, SNES_PAD_B, false),
        "held tier-4 charge survives the accepted ACTIVE jump and one release fires class 1");
  check(saber_ground_cancel_charge_probe(
            fixture, SABER_PHASE_RECOVERY, SNES_PAD_A | SNES_PAD_RIGHT, false),
        "held tier-4 charge survives the accepted RECOVERY dash and one release fires class 1");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  do {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  } while (snapshot.phase != SABER_PHASE_RECOVERY);
  frame(SNES_PAD_A | SNES_PAD_RIGHT);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 80 && g_ram[0x0baa] == 0x14; ++i)
    frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_IDLE && saber_test_grounded() &&
            g_ram[0x0baa] != 0x14,
        "cancelled ground combo returns to idle after native dash completion");
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_GROUND1 && snapshot.tick == 0,
        "the next ground Y after a cancelled combo starts slash 1");

  wall_frame = saber_wall_charge_setup(wall_path, &route);
  check(wall_frame != ~0u && g_ram[0x0baa] == 0x12 &&
            read_ram_word(g_ram, 0x0bad) == route.expected_x &&
            read_ram_word(g_ram, 0x0bb0) == route.expected_y,
        "charged cancel wall setup reaches the recorded native cling");
  /* Start charging only after the exact native cling has been recorded; the
   * approach inputs are intentionally kept separate from the persistence
   * probe. */
  for (unsigned i = 0; i < 21; ++i)
    frame(SNES_PAD_X | route.travel_input);
  const unsigned wall_charge_before = MmxZeroGetState().charge;
  check(g_ram[0x0baa] == 0x12,
        "charged cancel wall probe remains in the native cling");
  frame(SNES_PAD_X | route.travel_input | SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  check(wall_charge_before >= SABER_CHARGE_TIER_1_FRAME &&
            snapshot.kind == SABER_KIND_WALL &&
            snapshot.phase == SABER_PHASE_ACTIVE,
        "wall slash ACTIVE starts from the armadillo-fight cling");
  for (unsigned i = 0; i < 3; ++i) frame(SNES_PAD_X | route.travel_input);
  const unsigned wall_cues_before_jump = MmxSaberAttackCueCount();
  frame(SNES_PAD_X | route.jump_input);
  snapshot = MmxSaberAttackSnapshotGet();
  const unsigned wall_charge_after_jump = MmxZeroGetState().charge;
  check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE &&
            wall_charge_after_jump >= wall_charge_before &&
            tagged_projectiles() == 0,
        "wall slash ACTIVE exits on the native accepted wall jump and keeps charge");
  bool saw_normal_action10 = false;
  bool action10_side_effects_ok = true;
  for (unsigned i = 0; i < 5; ++i) {
    frame(SNES_PAD_X);
    if (g_ram[0x0baa] == 0x10) {
      saw_normal_action10 = true;
      snapshot = MmxSaberAttackSnapshotGet();
      if (snapshot.phase != SABER_PHASE_IDLE || snapshot.kind != SABER_KIND_NONE ||
          tagged_projectiles() != 0 ||
          MmxSaberAttackCueCount() != wall_cues_before_jump ||
          MmxZeroGetState().charge < wall_charge_after_jump)
        action10_side_effects_ok = false;
    }
  }
  check(saw_normal_action10 && action10_side_effects_ok,
        "the post-jump native $10 frame is playable, idle, side-effect free, and charge-safe");
  frame(0);

  wall_frame = saber_wall_setup(wall_path, &route, false);
  check(wall_frame == route.expected_wall_frame && g_ram[0x0baa] == 0x12,
        "air cancel wall setup reaches the recorded native cling");
  frame(route.jump_input);
  check(g_ram[0x0baa] == 0x10 && !saber_test_grounded(),
        "air cancel probe enters native wall-jump action $10");
  for (unsigned i = 0; i < 4; ++i) frame(0);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_AIR && snapshot.phase == SABER_PHASE_STARTUP,
        "air slash starts from the native wall-jump bridge");
  for (unsigned i = 0; i < 4; ++i) frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_AIR && snapshot.phase == SABER_PHASE_ACTIVE,
        "air cancel probe reaches ACTIVE before the wall-cling observation");
  bool air_wall_cancelled = false;
  for (unsigned i = 0; i < 16 && snapshot.phase != SABER_PHASE_IDLE; ++i) {
    frame(route.travel_input);
    snapshot = MmxSaberAttackSnapshotGet();
    if (g_ram[0x0baa] == 0x12) {
      check(snapshot.phase == SABER_PHASE_IDLE && snapshot.kind == SABER_KIND_NONE,
            "air ACTIVE wall cling retires the Saber owner through the cancel path");
      air_wall_cancelled = true;
      break;
    }
  }
  check(air_wall_cancelled,
        "air ACTIVE reaches a native wall cling for its old context rule");

  /* Separate Highway probe for the negative observation: B is pressed while
   * an air slash is ACTIVE, but native has no accepted midair jump action. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_B);
  for (unsigned i = 0; i < 4; ++i) frame(SNES_PAD_B);
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_AIR && snapshot.phase == SABER_PHASE_STARTUP,
        "Highway negative air-cancel probe starts an air slash");
  for (unsigned i = 0; i < 4; ++i) frame(0);
  frame(SNES_PAD_B);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_AIR && snapshot.phase == SABER_PHASE_ACTIVE &&
            !saber_test_grounded() &&
            g_ram[0x0baa] != 0x12 && g_ram[0x0baa] != 0x14,
        "an unaccepted airborne jump press does not cancel the air slash");
  puts("ok: saber-cancel");
}

static void begin_air_landing_probe(const char *fixture) {
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_B);
  frame(SNES_PAD_B);
  frame(SNES_PAD_B | SNES_PAD_Y);
}

static void advance_air_landing_probe(unsigned target_tick) {
  MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
  while (snapshot.kind == SABER_KIND_AIR && snapshot.tick < target_tick) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  }
}

static void advance_air_landing_probe_without_native(unsigned target_tick) {
  MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
  while (snapshot.kind == SABER_KIND_AIR && snapshot.tick < target_tick) {
    MmxSaberAttackStep(false, false, true, 0, 0);
    MmxSaberAttackRuntimeTick(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
  }
}

static void force_air_landing_probe(void) {
  g_ram[0xbd3] |= 4;
  MmxSaberAttackPlayerEnd(g_ram);
}

static void saber_land_checks(const char *fixture) {
  const MmxSaberAttack *land =
      MmxSaberAttackRecord(SABER_KIND_SABER_LAND, 0);
  MmxSaberAttackSnapshot snapshot;

  printf("reference: SaberLand record total=%u remains inert table data; "
         "landing exits the AIR owner\n", OLD_SABER_LAND_TOTAL);
  check(land && land->visual_animation == 7 &&
            land->total_ticks == OLD_SABER_LAND_TOTAL &&
            land->active_ticks == 0 && land->recovery_ticks == 0 &&
            land->bounds_segments == NULL && land->bounds_segment_count == 0 &&
            land->damage == 0 && land->bounds_pointer == 0,
        "ported SaberLand record remains inert table data and is never started");

  /* D3a/e: force the grounded edge at AIR tick 6.  Use the direct damage and
   * hitbox callbacks to prove that a target reachable only after landing is
   * not hit by the ended AIR swing. */
  begin_air_landing_probe(fixture);
  advance_air_landing_probe(6);
  snapshot = MmxSaberAttackSnapshotGet();
  const unsigned active_slot = saber_active_slot();
  const unsigned active_tag = active_slot ?
      read_ram_word(g_ram, active_slot + 0x3e) : 0;
  const unsigned air_enemy = empty_enemy_slot();
  const unsigned air_bit = 1u << ((air_enemy - 0xe68) / 64);
  const unsigned air_cue_count = MmxSaberAttackCueCount();
  const unsigned air_first_damage = active_slot ?
      MmxSaberAttackDamage(g_ram, air_enemy, active_slot, 1) : 0;
  check(snapshot.kind == SABER_KIND_AIR && snapshot.phase == SABER_PHASE_ACTIVE &&
            snapshot.tick == 6 && snapshot.anim_id == 4 && active_slot != 0 &&
            active_tag != 0 && tagged_projectiles() == 1 && air_enemy != 0 &&
            air_first_damage == 3 &&
            (MmxSaberAttackHitSlots() & air_bit) &&
            read_ram_word(g_ram, active_slot + 0x20) ==
                saber_record_pointer(&snapshot),
        "tick-6 probe reaches AIR ACTIVE with a live tagged slot and hit mask");
  force_air_landing_probe();
  snapshot = MmxSaberAttackSnapshotGet();
  bool active_landing_idle = snapshot.phase == SABER_PHASE_IDLE &&
      snapshot.kind == SABER_KIND_NONE && snapshot.anim_id == 0 &&
      tagged_projectiles() == 0 && saber_active_slot() == 0 &&
      MmxSaberAttackHitSlots() == 0 && MmxSaberAttackCueCount() == air_cue_count &&
      !g_ram[active_slot] && read_ram_word(g_ram, active_slot + 0x20) == 0;
  bool no_post_landing_visual = true;
  for (unsigned i = 0; i < 4; ++i) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase != SABER_PHASE_IDLE ||
        snapshot.kind != SABER_KIND_NONE || snapshot.anim_id != 0)
      no_post_landing_visual = false;
  }
  check(active_landing_idle && no_post_landing_visual,
        "active AIR landing exits on the landing frame with no anim 4/7, slot, or extra cue");
  const unsigned damage_after_landing =
      MmxSaberAttackDamage(g_ram, air_enemy, active_slot, 1);
  const unsigned hitbox_after_landing =
      MmxSaberAttackHitbox(g_ram, air_enemy, active_slot, 1);
  check(damage_after_landing == 1 && hitbox_after_landing == 1 &&
            MmxSaberAttackHitSlots() == 0 &&
            read_ram_word(g_ram, active_slot + 0x3e) == 0,
        "direct damage and hitbox callbacks cannot hit an enemy reachable only after landing");

  /* D3b: recovery landing also exits immediately and has no later effect. */
  begin_air_landing_probe(fixture);
  advance_air_landing_probe_without_native(14);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_RECOVERY && snapshot.tick == 14 &&
            tagged_projectiles() == 0,
        "tick-14 probe reaches AIR recovery with no tagged slot");
  const unsigned recovery_cues = MmxSaberAttackCueCount();
  force_air_landing_probe();
  snapshot = MmxSaberAttackSnapshotGet();
  bool recovery_nothing_after = true;
  if (snapshot.phase != SABER_PHASE_IDLE || snapshot.kind != SABER_KIND_NONE ||
      snapshot.anim_id != 0 || tagged_projectiles() != 0 ||
      MmxSaberAttackHitSlots() != 0 ||
      MmxSaberAttackCueCount() != recovery_cues)
    recovery_nothing_after = false;
  for (unsigned i = 0; i < 5; ++i) {
    MmxSaberAttackStep(false, true, true, 0, 0);
    snapshot = MmxSaberAttackSnapshotGet();
    MmxSaberAttackRuntimeTick(g_ram);
    if (snapshot.phase != SABER_PHASE_IDLE || snapshot.kind != SABER_KIND_NONE ||
        snapshot.anim_id != 0 || tagged_projectiles() != 0)
      recovery_nothing_after = false;
  }
  check(recovery_nothing_after,
        "recovery AIR landing is idle on the landing frame and stays side-effect free");

  /* D3c: the AIR slash completes before this fixture's later landing. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  bool air_completed_before_landing = false;
  bool landed_after_air_completion = false;
  bool completed_landing_idle = true;
  for (unsigned frame_number = 1; frame_number <= 120; ++frame_number) {
    const bool was_grounded = saber_test_grounded();
    const MmxSaberAttackSnapshot before = MmxSaberAttackSnapshotGet();
    unsigned input = frame_number <= 20 ? SNES_PAD_B : 0;
    if (frame_number == 3) input |= SNES_PAD_Y;
    frame(input);
    snapshot = MmxSaberAttackSnapshotGet();
    if (before.kind == SABER_KIND_AIR &&
        before.tick == OLD_SABER_AIR_TOTAL - 1 &&
        snapshot.phase == SABER_PHASE_IDLE)
      air_completed_before_landing = true;
    if (air_completed_before_landing) {
      if (snapshot.phase != SABER_PHASE_IDLE ||
          snapshot.kind != SABER_KIND_NONE || snapshot.anim_id != 0)
        completed_landing_idle = false;
      if (!was_grounded && saber_test_grounded())
        landed_after_air_completion = true;
    }
    if (landed_after_air_completion) break;
  }
  check(air_completed_before_landing && landed_after_air_completion &&
            completed_landing_idle,
        "completed air slash has no visual or attack state when landing later");

  /* D3d: plain jump has no AIR owner, so its landing remains completely idle. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  bool plain_landing_edge = false;
  bool plain_landing_idle = true;
  bool plain_airborne = false;
  for (unsigned frame_number = 1; frame_number <= 120; ++frame_number) {
    const bool was_grounded = saber_test_grounded();
    frame(frame_number <= 20 ? SNES_PAD_B : 0);
    snapshot = MmxSaberAttackSnapshotGet();
    if (!saber_test_grounded()) plain_airborne = true;
    if (plain_airborne && !was_grounded && saber_test_grounded()) {
      plain_landing_edge = true;
      plain_landing_idle = snapshot.phase == SABER_PHASE_IDLE &&
          snapshot.kind == SABER_KIND_NONE && snapshot.anim_id == 0;
      break;
    }
  }
  check(plain_landing_edge && plain_landing_idle,
        "plain jump landing has no Saber attack or replacement visual");

  /* D3f: upstream owns charge; holding X never decreases it through this
   * landing edge. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  const unsigned charge_before_landing = MmxZeroGetState().charge;
  unsigned charge_at_landing = 0;
  bool charge_airborne = false;
  bool charge_landed = false;
  bool charge_never_decreased = true;
  for (unsigned frame_number = 1; frame_number <= 120; ++frame_number) {
    const bool was_grounded = saber_test_grounded();
    const unsigned charge_before_frame = MmxZeroGetState().charge;
    unsigned input = frame_number <= 20 ? SNES_PAD_B | SNES_PAD_X : SNES_PAD_X;
    if (frame_number == 3) input |= SNES_PAD_Y;
    frame(input);
    snapshot = MmxSaberAttackSnapshotGet();
    if (MmxZeroGetState().charge < charge_before_frame)
      charge_never_decreased = false;
    if (!saber_test_grounded()) charge_airborne = true;
    if (charge_airborne && !was_grounded && saber_test_grounded()) {
      charge_landed = true;
      charge_at_landing = MmxZeroGetState().charge;
      break;
    }
  }
  check(charge_airborne && charge_landed && charge_never_decreased &&
            charge_at_landing >= charge_before_landing,
        "held X charge never decreases across an air slash landing");

  puts("ok: saber-land");
}

static void saber_special_checks(const char *fixture,
                                 const SpecialCounts *upstream) {
  SpecialCounts saber = measure_specials(fixture, SNES_PAD_X);
  print_special_reference("Saber Zero", &saber);
  check_specials_equal("Saber Zero specials equal upstream X3 Zero", upstream,
                       &saber);

  {
    unsigned char previous[8] = {0};
    unsigned slash_births = 0;
    unsigned held_births = 0;
    unsigned fresh_births = 0;
    bool saw_slash = false;
    bool reached_idle = false;
    MmxSaberAttackSnapshot snapshot;

    load_fixture(fixture);
    MmxSaberFrameReset();
    select_native_weapon(2); /* Fire Wave. */
    shot_presence(8, previous);
    for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 2; ++i) {
      const unsigned input = i == 0 ? SNES_PAD_X | SNES_PAD_Y : SNES_PAD_X;
      frame(input);
      snapshot = MmxSaberAttackSnapshotGet();
      const unsigned births = new_projectiles(8, previous);
      if (snapshot.phase != SABER_PHASE_IDLE) {
        saw_slash = true;
        slash_births += births;
      } else {
        reached_idle = true;
        held_births += births;
        break;
      }
    }
    for (unsigned i = 0; reached_idle && i < 60; ++i) {
      frame(SNES_PAD_X);
      held_births += new_projectiles(8, previous);
    }
    printf("reference: OD4 Fire Wave saw_slash=%d reached_idle=%d slash_births=%u held_births=%u\n",
           saw_slash, reached_idle, slash_births, held_births);
    check(saw_slash && reached_idle && slash_births == 0,
          "OD4 blocks new Fire Wave flames throughout ground-1 while X is held");
    if (held_births) {
      printf("reference: Saber Fire Wave post-idle held-X semantics births=%u in 60 frames\n",
             held_births);
    } else {
      frame(0);
      new_projectiles(8, previous);
      frame(SNES_PAD_X);
      fresh_births = new_projectiles(8, previous);
      check(fresh_births > 0,
            "Fire Wave produces a flame after a fresh X press following ground-1");
      printf("reference: Saber Fire Wave requires a fresh X press after idle; births=%u\n",
             fresh_births);
    }
  }

  {
    unsigned char previous[8] = {0};
    unsigned slash_births = 0;
    unsigned after_idle_births = 0;
    bool saw_slash = false;
    bool reached_idle = false;
    MmxSaberAttackSnapshot snapshot;

    load_fixture(fixture);
    MmxSaberFrameReset();
    select_native_weapon(5); /* Storm Tornado. */
    shot_presence(11, previous);
    for (unsigned i = 0; i < OLD_SABER_GROUND1_TOTAL + 2; ++i) {
      const unsigned input = i == 0 ? SNES_PAD_Y :
          i == 1 ? SNES_PAD_X : 0;
      frame(input);
      snapshot = MmxSaberAttackSnapshotGet();
      const unsigned births = new_projectiles(11, previous);
      if (snapshot.phase != SABER_PHASE_IDLE) {
        saw_slash = true;
        slash_births += births;
      } else {
        reached_idle = true;
        break;
      }
    }
    check(saw_slash && reached_idle && slash_births == 0,
          "OD4 blocks a Storm Tornado X press throughout ground-1");
    frame(SNES_PAD_X);
    after_idle_births += new_projectiles(11, previous);
    for (unsigned i = 1; i < SABER_ONE_SHOT_FRAMES; ++i) {
      frame(0);
      after_idle_births += new_projectiles(11, previous);
    }
    check(after_idle_births == 1,
          "Storm Tornado fires exactly one projectile from an X press after ground-1");
  }
  puts("ok: saber-input-specials-zero");
}

static void saber_pass_through_checks(const char *fixture) {
  load_fixture(fixture);
  switch_to_x();
  for (unsigned i = 0; i < 5; ++i) {
    frame(i == 0 ? SNES_PAD_X : 0);
    check(!MmxSaberFrameLastWroteInput(),
          "Saber frame hook writes nothing after exchange to X");
  }
  puts("ok: saber-input-x-pass-through");
}

static void saber_legacy_intent_bridge_check(const char *fixture) {
  load_fixture(fixture);
  /* Run the real Saber pre-player hook, then clear the mapped fire bytes
   * before the Zero legacy tick. This isolates the legacy_intent seam from
   * the native mapped view: a rejected override must not charge. */
  g_ram[0x00a7] = 0x40;
  g_ram[0x00a9] = 0;
  g_ram[0x00ab] = 0x40;
  g_ram[0x00ac] = 0;
  MmxZeroExtPrePlayer(g_ram);
  g_ram[0x0bdf] = 0;
  g_ram[0x0be3] = 0;
  MmxZeroPlayerTick(g_ram);
  check(MmxZeroGetState().charge == 1,
        "Saber legacy intent drives Zero charge after mapped fire is cleared");
  load_fixture(fixture);
}

static void saber_input_checks(const char *fixture,
                               const SpecialCounts *upstream) {
  /* Exercise the native X1 arm-upgrade branch while the Saber buster path
   * still owns the legacy X3 charge chain. */
  saber_legacy_intent_bridge_check(fixture);
  load_fixture(fixture);
  g_ram[0x1f99] |= 2;
  saber_track_x1_charged = true;
  saber_saw_x1_charged = false;
  release_charge_button(SABER_CHARGE_FULL_FRAME, SNES_PAD_X);
  idle(17);
  frame(SNES_PAD_X);
  idle(9);
  saber_track_x1_charged = false;
  check(!saber_saw_x1_charged && projectiles(SABER_FULL_RELEASE_CLASS) == 2,
        "Saber X buster upgrade still emits only the two X3 class-3 shots");

  saber_track_x1_charged = true;
  saber_saw_x1_charged = false;
  x3_plain_checks(fixture, SNES_PAD_X);
  x3_hurt_checks(fixture, SNES_PAD_X);
  x3_jump_checks(fixture, SNES_PAD_X);
  saber_track_x1_charged = false;
  check(!saber_saw_x1_charged,
        "Saber X3 buster paths never spawn X1 charged-shot class 2");
  saber_y_checks(fixture);
  saber_special_checks(fixture, upstream);
  saber_pass_through_checks(fixture);
  native_x1_checks(fixture);
  puts("ok: saber-input");
}

static void saber_hurt_latch_checks(const char *fixture) {
  unsigned char previous[8] = {0};
  unsigned charged_before_hurt;
  unsigned lowest_charge;
  unsigned native_births = 0;
  bool saw_hurt = false;
  bool saw_hurt_shot = false;
  MmxSaberAttackSnapshot snapshot;

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned i = 0; i < 6; ++i) frame(SNES_PAD_X);
  frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase != SABER_PHASE_IDLE,
        "hurt-latch probe releases X while the Saber slash is live");
  while (snapshot.phase != SABER_PHASE_RECOVERY) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  while (snapshot.tick < OLD_SABER_GROUND1_TOTAL - 2) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  }

  charged_before_hurt = MmxZeroGetState().charge;
  lowest_charge = charged_before_hurt;
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  for (unsigned i = 0; i < 120; ++i) {
    frame(0);
    native_births += new_native_projectiles(previous);
    if (g_ram[0xbaa] == 0x0e) {
      saw_hurt = true;
      if (native_births) saw_hurt_shot = true;
      if (MmxZeroGetState().charge < lowest_charge)
        lowest_charge = MmxZeroGetState().charge;
    }
    if (saw_hurt && g_ram[0xbaa] != 0x0e && native_births)
      break;
  }
  check(saw_hurt, "hurt-latch probe observes native hurt frames");
  check(lowest_charge >= charged_before_hurt,
        "hurt-latch probe keeps charge through the hurt freeze");
  check(!saw_hurt_shot && native_births == 1,
        "hurt-latch probe delivers exactly one charged shot after hurt");
  check(!projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "hurt-latch probe emits no X1 native charged shot");
  puts("ok: saber-buster-hurt-latch");
}

static bool legacy_saber_route_present(void) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (g_ram[d] && read_ram_word(g_ram, d + 0x3e) == 0x5a53)
      return true;
  return false;
}

static void saber_plain_after_probe(const char *label) {
  unsigned char previous[8] = {0};
  unsigned births;
  unsigned native_x1_charged = 0;

  MmxSaberFrameReset();
  MmxZeroCancel(g_ram);
  /* The Saber owner is reset above; restore only the native idle/action and
   * weapon-selection fields so this remains a post-sequence plain-buster
   * probe rather than a fresh save reload. */
  g_ram[0xbaa] = 0;
  g_ram[0xbab] = 0;
  g_ram[0xbdb] = 0;
  g_ram[0xbfd] = 0;
  g_ram[0xc06] &= (uint8_t)~4;
  g_ram[0xc26] &= (uint8_t)~64;
  for (unsigned i = 0; i < 60 && native_projectiles(); ++i)
    frame(0);
  check(!native_projectiles(), "plain probe clears residual native projectiles");
  memset(previous, 0, sizeof(previous));
  frame(0);
  births = new_projectiles(0, previous);
  frame(SNES_PAD_X);
  births += new_projectiles(0, previous);
  frame(0);
  births += new_projectiles(0, previous);
  for (unsigned i = 0; i < 8; ++i) {
    unsigned d = 0x1228 + i * 64;
    native_x1_charged += g_ram[d] && !saber_tagged_projectile(d) &&
        g_ram[d + 10] == SABER_X1_CHARGED_RELEASE_CLASS;
  }
  check(births == 1 && projectiles(0) == 1 && native_x1_charged == 0, label);
}

static void saber_charge_cap_checks(const char *fixture) {
  unsigned max_charge = 0;
  bool tier_exceeded = false;
  bool third_route = false;
  unsigned char previous[8] = {0};
  unsigned first_shots = 0;
  unsigned second_shots = 0;

  printf("reference: fda759c caps the charge value at 200, preserving tier 8; "
         "tier-8 release is class 3 and the second buster press remains the "
         "two-shot route, while tier 10 marks the removed Saber route\n");
  load_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < 260; ++i) {
    frame(SNES_PAD_X);
    MmxZeroState state = MmxZeroGetState();
    if (state.charge > max_charge) max_charge = state.charge;
    tier_exceeded |= MmxZeroChargeTier(&state) > 8;
    third_route |= state.saber_ready || legacy_saber_route_present();
  }
  MmxZeroState held = MmxZeroGetState();
  check(!tier_exceeded && max_charge == 200 && held.charge == 200 &&
            MmxZeroChargeTier(&held) == 8 && !held.saber_ready && !third_route,
        "Saber 260-frame hold caps the upstream charge at pink tier 8");

  frame(0);
  first_shots += new_projectiles(SABER_TIER_8_RELEASE_CLASS, previous);
  check(MmxZeroGetState().combo == 1 && !MmxZeroGetState().saber_ready,
        "Saber capped release stores the tier-8 one-class-3 buster step");
  for (unsigned i = 0; i < 120; ++i) {
    frame(0);
    first_shots += new_projectiles(SABER_TIER_8_RELEASE_CLASS, previous);
    third_route |= MmxZeroGetState().saber_ready || legacy_saber_route_present();
  }
  check(first_shots == 1 && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "Saber capped release emits exactly one class-3 shot");
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask || g_ram[0xc25]); ++i)
    frame(0);
  memset(previous, 0, sizeof(previous));
  frame(SNES_PAD_X);
  second_shots += new_projectiles(SABER_TIER_8_RELEASE_CLASS, previous);
  check(!MmxZeroGetState().saber_ready && MmxZeroGetState().burst == 2,
        "Saber capped tier-8 charge still reaches the second buster burst");
  for (unsigned i = 0; i < 120; ++i) {
    frame(0);
    second_shots += new_projectiles(SABER_TIER_8_RELEASE_CLASS, previous);
    third_route |= MmxZeroGetState().saber_ready || legacy_saber_route_present();
  }
  check(second_shots == 1 && !third_route && !legacy_saber_route_present(),
        "Saber capped two-shot route never reaches the green/third legacy route");
}

static void saber_ground_combo_attack_probe(const char *fixture, bool special) {
  unsigned char previous[8] = {0};
  unsigned attack_births = 0;
  MmxSaberAttackSnapshot after;

  load_fixture(fixture);
  MmxSaberFrameReset();
  if (special) select_native_weapon(2); /* Fire Wave. */
  frame(SNES_PAD_Y);
  after = MmxSaberAttackSnapshotGet();
  attack_births += after.phase != SABER_PHASE_IDLE ?
      new_native_projectiles(previous) : 0;
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_CLOSE; ++tick) {
    frame(tick == 5 ? SNES_PAD_X : 0);
    after = MmxSaberAttackSnapshotGet();
    unsigned births = new_native_projectiles(previous);
    if (after.phase != SABER_PHASE_IDLE) attack_births += births;
  }
  frame(SNES_PAD_Y);
  after = MmxSaberAttackSnapshotGet();
  new_native_projectiles(previous);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND2_CHAIN_CLOSE; ++tick) {
    frame(tick == 5 ? SNES_PAD_X : 0);
    after = MmxSaberAttackSnapshotGet();
    unsigned births = new_native_projectiles(previous);
    if (after.phase != SABER_PHASE_IDLE) attack_births += births;
  }
  frame(SNES_PAD_Y);
  after = MmxSaberAttackSnapshotGet();
  new_native_projectiles(previous);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND3_TOTAL; ++tick) {
    frame(tick == 5 ? SNES_PAD_X : 0);
    after = MmxSaberAttackSnapshotGet();
    unsigned births = new_native_projectiles(previous);
    if (after.phase != SABER_PHASE_IDLE) attack_births += births;
  }
  frame(0);
  check(attack_births == 0 && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        special ? "special Fire Wave is blocked through ground slashes 1/2/3"
                : "buster X taps are blocked through ground slashes 1/2/3");
  saber_plain_after_probe(special ?
      "plain X works after the ground 1/2/3 special-block sequence" :
      "plain X works after the ground 1/2/3 buster-block sequence");
}

static void saber_air_attack_probe(const char *fixture, bool special) {
  unsigned char previous[8] = {0};
  unsigned attack_births = 0;

  load_fixture(fixture);
  MmxSaberFrameReset();
  if (special) select_native_weapon(2); /* Fire Wave. */
  for (unsigned frame_number = 1; frame_number <= 22; ++frame_number) {
    unsigned input = SNES_PAD_B;
    if (frame_number == 3) input |= SNES_PAD_Y;
    if (frame_number == 8) input |= SNES_PAD_X;
    frame(input);
    unsigned births = new_native_projectiles(previous);
    if (MmxSaberAttackSnapshotGet().phase != SABER_PHASE_IDLE)
      attack_births += births;
  }
  check(attack_births == 0 && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        special ? "special Fire Wave is blocked through the air Saber attack"
                : "buster X tap is blocked through the air Saber attack");
  saber_plain_after_probe(special ?
      "plain X works after the air special-block sequence" :
      "plain X works after the air buster-block sequence");
}

static void saber_dash_attack_probe(const char *fixture, bool special) {
  unsigned char previous[8] = {0};
  unsigned attack_births = 0;

  load_fixture(fixture);
  MmxSaberFrameReset();
  if (special) select_native_weapon(2); /* Fire Wave. */
  check(start_saber_dash_right(0) != 0,
        "dash buster-rule probe starts its native dash");
  for (unsigned tick = 0; tick < OLD_SABER_DASH_TOTAL; ++tick) {
    unsigned input = SNES_PAD_A | SNES_PAD_RIGHT;
    if (tick == 0) input |= SNES_PAD_Y;
    if (tick == 5) input |= SNES_PAD_X;
    frame(input);
    unsigned births = new_native_projectiles(previous);
    if (MmxSaberAttackSnapshotGet().phase != SABER_PHASE_IDLE)
      attack_births += births;
  }
  check(attack_births == 0 && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        special ? "special Fire Wave is blocked through the dash Saber attack"
                : "buster X tap is blocked through the dash Saber attack");
  saber_plain_after_probe(special ?
      "plain X works after the dash special-block sequence" :
      "plain X works after the dash buster-block sequence");
}

static void saber_wall_attack_probe(const char *fixture_dir, bool special) {
  static const SaberWallRoute route = {
    "OPEN-RIGHT", SNES_PAD_LEFT, SNES_PAD_B | SNES_PAD_LEFT,
    SNES_PAD_LEFT, 60, 20, 5142, 2665, 0x40, 1};
  char path[4096];
  unsigned char previous[8] = {0};
  unsigned attack_births = 0;
  MmxSaberAttackSnapshot snapshot;
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         "armadillo-fight.sav");
  check(written >= 0 && written < (int)sizeof(path),
        "buster-rule wall fixture path fits");
  check(saber_wall_setup(path, &route, false) == route.expected_wall_frame,
        "buster-rule wall setup reaches the native cling");
  if (special) {
    g_ram[0xbdb] = 4; /* Fire Wave, with its native energy slot available. */
    g_ram[0x1f89] = 0;
    g_ram[0x1f8a] = 0xdc;
  }
  frame(route.travel_input | SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_WALL_TOTAL; ++tick) {
    frame(route.travel_input | (tick == 5 ? SNES_PAD_X : 0));
    snapshot = MmxSaberAttackSnapshotGet();
    unsigned births = new_native_projectiles(previous);
    if (snapshot.phase != SABER_PHASE_IDLE) attack_births += births;
  }
  check(attack_births == 0 && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        special ? "special Fire Wave is blocked through the wall Saber attack"
                : "buster X tap is blocked through the wall Saber attack");
  saber_plain_after_probe(special ?
      "plain X works after the wall special-block sequence" :
      "plain X works after the wall buster-block sequence");
}

static void saber_combo_release_checks(const char *fixture) {
  unsigned char previous[8] = {0};
  unsigned charged_births = 0;
  unsigned plain_births = 0;
  unsigned first_idle_charged = 0;
  bool first_idle_seen = false;
  bool delivered_on_first_idle = false;
  MmxSaberAttackSnapshot snapshot;

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_CLOSE; ++tick)
    frame(SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned tick = 1; tick <= 5; ++tick) frame(SNES_PAD_X);
  frame(0); /* Release during slash 2. */
  for (unsigned tick = 7; tick < OLD_SABER_GROUND2_CHAIN_CLOSE; ++tick)
    frame(0);
  frame(SNES_PAD_Y); /* Start slash 3. */
  for (unsigned tick = 1; tick < OLD_SABER_GROUND3_TOTAL; ++tick)
    frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_GROUND3 && snapshot.phase != SABER_PHASE_IDLE,
        "CR1 probe reaches ground slash 3 after releasing X during slash 2");
  snapshot = MmxSaberAttackSnapshotGet();
  frame(0);
  first_idle_seen = snapshot.phase != SABER_PHASE_IDLE &&
      MmxSaberAttackSnapshotGet().phase == SABER_PHASE_IDLE;
  first_idle_charged = new_native_charged_projectiles(previous);
  delivered_on_first_idle = first_idle_seen && first_idle_charged == 1;
  charged_births = first_idle_charged;
  for (unsigned i = 0; i < 120; ++i) {
    frame(0);
    charged_births += new_native_charged_projectiles(previous);
  }
  check(first_idle_seen && delivered_on_first_idle && charged_births == 1 &&
            !projectiles(0) && !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "CR1 emits exactly one charged shot on the first idle frame after slash 3");
  saber_plain_after_probe("plain X works after CR1 delivery");

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND1_CHAIN_CLOSE; ++tick)
    frame(SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned tick = 1; tick <= 5; ++tick) frame(SNES_PAD_X);
  frame(0); /* Set the CR1 latch. */
  frame(SNES_PAD_X); /* R7 cancels it before slash 2 ends. */
  for (unsigned tick = 8; tick < OLD_SABER_GROUND2_CHAIN_CLOSE; ++tick)
    frame(SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned tick = 1; tick < OLD_SABER_GROUND3_TOTAL; ++tick)
    frame(SNES_PAD_X);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_GROUND3 && snapshot.phase != SABER_PHASE_IDLE,
        "R7 probe re-presses X before slash 3 ends");
  memset(previous, 0, sizeof(previous));
  frame(SNES_PAD_X);
  plain_births += new_native_projectiles(previous);
  check(plain_births == 0 && !MmxZeroGetState().burst &&
            !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "R7 re-press produces no shot at the first idle frame");
  memset(previous, 0, sizeof(previous));
  frame(0); /* Only this real release may deliver CR1. */
  charged_births = new_native_charged_projectiles(previous);
  for (unsigned i = 0; i < 120; ++i) {
    frame(0);
    charged_births += new_native_charged_projectiles(previous);
  }
  check(charged_births == 1 && !projectiles(0) &&
            !projectiles(SABER_X1_CHARGED_RELEASE_CLASS),
        "R7 waits for the real X release before one charged shot");
  saber_plain_after_probe("plain X works after the R7 CR1 probe");
}

static void saber_charge_transition_checks(const char *fixture,
                                           const char *fixture_dir) {
  unsigned before;
  unsigned lowest;
  unsigned landing_charge = 0;
  bool landed = false;
  bool airborne = false;
  bool never_decreased = true;
  MmxSaberAttackSnapshot snapshot;

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(60, SNES_PAD_X);
  before = MmxZeroGetState().charge;
  lowest = before;
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  for (unsigned i = 0; i < 120; ++i) {
    frame(SNES_PAD_X);
    if (MmxZeroGetState().charge < lowest) lowest = MmxZeroGetState().charge;
    if (g_ram[0xbaa] != 0x0e && lowest >= before) break;
  }
  check(lowest >= before, "Saber charge never decreases through hurt");
  saber_plain_after_probe("plain X works after the hurt charge probe");

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  before = MmxZeroGetState().charge;
  for (unsigned frame_number = 1; frame_number <= 120; ++frame_number) {
    unsigned previous_charge = MmxZeroGetState().charge;
    unsigned input = frame_number <= 20 ? SNES_PAD_B | SNES_PAD_X : SNES_PAD_X;
    if (frame_number == 3) input |= SNES_PAD_Y;
    frame(input);
    if (MmxZeroGetState().charge < previous_charge) never_decreased = false;
    if (!airborne && !saber_test_grounded()) airborne = true;
    if (airborne && saber_test_grounded()) {
      landed = true;
      landing_charge = MmxZeroGetState().charge;
      break;
    }
  }
  check(airborne && landed && never_decreased && landing_charge >= before,
        "Saber charge never decreases through air slash landing");
  saber_plain_after_probe("plain X works after the landing charge probe");

  static const SaberWallRoute route = {
    "OPEN-RIGHT", SNES_PAD_LEFT, SNES_PAD_B | SNES_PAD_LEFT,
    SNES_PAD_LEFT, 60, 20, 5142, 2665, 0x40, 1};
  char path[4096];
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         "armadillo-fight.sav");
  check(written >= 0 && written < (int)sizeof(path),
        "charge wall fixture path fits");
  check(saber_wall_charge_setup(path, &route) == route.expected_wall_frame,
        "charge wall setup reaches the native cling");
  before = MmxZeroGetState().charge;
  lowest = before;
  for (unsigned i = 0; i < 21; ++i) {
    frame(route.travel_input | SNES_PAD_X);
    if (MmxZeroGetState().charge < lowest) lowest = MmxZeroGetState().charge;
  }
  check(lowest >= before && g_ram[0xbaa] == 0x12,
        "Saber charge never decreases while clinging to a wall");
  frame(route.travel_input | route.jump_input | SNES_PAD_X);
  lowest = MmxZeroGetState().charge;
  for (unsigned i = 0; i < 8; ++i) {
    frame(SNES_PAD_X);
    if (MmxZeroGetState().charge < lowest) lowest = MmxZeroGetState().charge;
  }
  check(lowest >= before && g_ram[0xbaa] != 0x12,
        "Saber charge never decreases through a wall jump");
  saber_plain_after_probe("plain X works after the wall charge probe");

  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(30, SNES_PAD_X);
  before = MmxZeroGetState().charge;
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned i = 0; i < 8; ++i) frame(SNES_PAD_X);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_ACTIVE,
        "jump-out charge probe reaches active Saber slash");
  frame(SNES_PAD_X | SNES_PAD_B);
  lowest = MmxZeroGetState().charge;
  for (unsigned i = 0; i < 12; ++i) {
    frame(SNES_PAD_X);
    if (MmxZeroGetState().charge < lowest) lowest = MmxZeroGetState().charge;
  }
  check(lowest >= before && MmxSaberAttackSnapshotGet().phase == SABER_PHASE_IDLE,
        "Saber charge never decreases when jumping out of a slash");
  saber_plain_after_probe("plain X works after the jump-out charge probe");
}

static void saber_buster_rule_checks(const char *fixture, const char *fixture_dir) {
  saber_track_x1_charged = true;
  saber_saw_x1_charged = false;
  saber_charge_transition_checks(fixture, fixture_dir);
  saber_combo_release_checks(fixture);
  saber_charge_cap_checks(fixture);
  saber_ground_combo_attack_probe(fixture, false);
  saber_ground_combo_attack_probe(fixture, true);
  saber_air_attack_probe(fixture, false);
  saber_air_attack_probe(fixture, true);
  saber_wall_attack_probe(fixture_dir, false);
  saber_wall_attack_probe(fixture_dir, true);
  saber_dash_attack_probe(fixture, false);
  saber_dash_attack_probe(fixture, true);
  saber_hurt_latch_checks(fixture);
  check(!saber_saw_x1_charged,
        "all Saber buster-rule sequences avoid X1 native charged class 2");
  saber_track_x1_charged = false;
  puts("ok: saber-buster-rules");
}

static const RecompLauncherCModProvider *g_mod_provider;

static int set_test_env(const char *name, const char *value) {
#ifdef _WIN32
  return _putenv_s(name, value);
#else
  return setenv(name, value, 1);
#endif
}

/* Saber is the "saber" Zero behavior of Add Zero; its tuning lives in the
 * separate Saber Zero settings feature, which saber runs enable. */
static void activate_zero(const char *x1_rom, const char *x3_rom,
                          const char *assets, bool saber_package,
                          bool expect_saber_assets) {
  const char *root = getenv("MMX_COOP_LAUNCHER_ROOT");
  const char *package = "megaman-x.character.zero";
  const char *feature = "zero";
  if (!g_mod_provider) {
    check(root && root[0], "Saber runner supplies an isolated mod catalog");
    check(readable_file(x1_rom), "X1 ROM exists");
    check(readable_file(x3_rom), "X3 ROM exists");
    check(readable_file(getenv("MMX_ZERO_TEST_FIXTURE")), "save0.sav exists");
    if (!snes_mod_runtime_initialize_c(root, "megaman-x-us", kMmxRomDigest)) {
      fprintf(stderr, "Saber catalog error: %s\n",
              snes_mod_runtime_last_error_c());
      check(0, "Saber catalog initializes");
    }
    g_mod_provider = snes_mod_runtime_launcher_provider_c();
    check(g_mod_provider && g_mod_provider->feature_enable &&
              g_mod_provider->feature_set_option &&
              g_mod_provider->feature_resource_set_path && g_mod_provider->commit,
          "Saber catalog exposes the feature/resource provider");
  }
  check(g_mod_provider->feature_enable(g_mod_provider->ctx, package, feature, 1),
        "Add Zero enables");
  check(g_mod_provider->feature_set_option(g_mod_provider->ctx, package, feature,
                                           "start", "zero"),
        "Add Zero starts as Zero");
  check(g_mod_provider->feature_set_option(g_mod_provider->ctx, package, feature,
                                           "behavior",
                                           saber_package ? "saber" : "x3"),
        saber_package ? "Add Zero selects behavior=saber" :
                        "Add Zero selects behavior=x3");
  check(g_mod_provider->feature_enable(g_mod_provider->ctx,
                                       MMX_SABER_SETTINGS_PACKAGE,
                                       MMX_SABER_SETTINGS_FEATURE,
                                       saber_package ? 1 : 0),
        saber_package ? "Saber Zero settings enable" :
                        "Saber Zero settings disable");
  check(g_mod_provider->feature_resource_set_path(g_mod_provider->ctx, package, feature,
                                                  "x3-rom", x3_rom),
        "Add Zero selects the X3 ROM");
  check(g_mod_provider->commit(g_mod_provider->ctx, x1_rom),
        "Add Zero commits for the X1 ROM");
  snes_mod_runtime_activate_plugins_c();
  check(MmxZeroEnabled() && MmxZeroActive() && !MmxZeroModern(),
        "Add Zero activates the legacy X3 controller");
  check(expect_saber_assets ? MmxSaberEnabled() : !MmxSaberEnabled(),
        expect_saber_assets ? "behavior=saber enables the Saber plugin" :
                              "missing Saber assets leave the Saber plugin disabled");
  if (saber_package && expect_saber_assets)
    check(MmxSaberAssetsLoaded() && MmxSaberRideAssetsLoaded() &&
              MmxSaberWaveLoaded(),
          "Saber loader reports private sprite and wave caches loaded");
  check(readable_file(assets), "isolated X3 Zero asset cache exists");
}

static unsigned saber_finisher_slot_with_tag(unsigned tag) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (read_ram_word(g_ram, d + 0x3e) == tag) return d;
  return 0;
}

static unsigned saber_finisher_wave_slot(void) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    const unsigned tag = read_ram_word(g_ram, d + 0x3e);
    if ((tag & MMX_SABER_WAVE_TAG_FAMILY_MASK) ==
            MMX_SABER_WAVE_TAG_FAMILY && !g_ram[d] && g_ram[d + 1] == 0x80)
      return d;
  }
  return 0;
}

static unsigned saber_finisher_free_slots(void) {
  unsigned free_count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    free_count += read_ram_word(g_ram, d) == 0;
  return free_count;
}

static void saber_finisher_fill_free_slots(void) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    if (read_ram_word(g_ram, d) != 0) continue;
    g_ram[d] = 1;
    g_ram[d + 1] = 2;
    g_ram[d + 10] = 3;
  }
}

static void saber_finisher_window_setup(const char *fixture) {
  bool opened = false;

  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(150, SNES_PAD_X);
  check(MmxZeroGetState().combo == 1,
        "finisher setup stores the capped tier-8 first burst");
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask ||
        g_ram[0xc25]); ++i)
    frame(0);
  check(MmxZeroGetState().combo == 1,
        "finisher setup retires burst 1 while retaining the combo step");
  frame(SNES_PAD_X);
  check(MmxZeroGetState().burst == 2,
        "finisher setup starts the X3 second burst from a fresh X edge");
  if (MmxSaberComboWindowTicks()) opened = true;
  for (unsigned i = 0; i < 120 && !opened; ++i) {
    frame(0);
    opened = MmxSaberComboWindowTicks() != 0;
  }
  check(opened, "finisher setup observes a window after a live second shot");
}

static void saber_finisher_accept_checks(const char *fixture) {
  uint8_t buster_live_before[8] = {0};
  bool buster_survived = false;
  unsigned cue_count;

  saber_finisher_window_setup(fixture);
  check(MmxSaberComboWindowTicks() == MMX_SABER_DEFAULT_FINISHER_WINDOW,
        "default finisher window is 27 frames on the second-shot frame");
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned d = 0x1228 + i * 64;
    buster_live_before[i] = (unsigned char)(g_ram[d] &&
        g_ram[d + 10] == SABER_TIER_8_RELEASE_CLASS &&
        read_ram_word(g_ram, d + 0x3e) != 0x5a53);
  }
  cue_count = MmxSaberComboFinisherCueCount();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned d = 0x1228 + i * 64;
    buster_survived |= buster_live_before[i] && g_ram[d] &&
        g_ram[d + 10] == SABER_TIER_8_RELEASE_CLASS;
  }
  const unsigned slash_slot = saber_finisher_slot_with_tag(0x5a53);
  const unsigned wave_slot = saber_finisher_wave_slot();
  check(MmxZeroGetState().slash == 1 && slash_slot && wave_slot &&
            slash_slot != wave_slot,
        "Y in the finisher window publishes $5A53 and a distinct inactive $5600 slot");
  check(g_ram[wave_slot] == 0 && g_ram[wave_slot + 1] == 0x80 &&
            (read_ram_word(g_ram, wave_slot + 0x3e) & 0xff) != 0,
        "the reserved wave slot is inactive with a nonzero generation");
  check(MmxSaberAttackSnapshotGet().phase == SABER_PHASE_IDLE,
        "the window Y claim does not start a donor ground slash");
  check(buster_survived, "the flying second buster shot remains untouched");
  check(MmxSaberComboFinisherCueCount() == cue_count + 1 &&
            MmxSaberSfxLastClip() == MMX_SABER_SFX_CLIP_SABER_3,
        "accepted upstream slash plays exactly one saber_3 finisher cue");
}

static void saber_finisher_timing_checks(const char *fixture) {
  saber_finisher_window_setup(fixture);
  for (unsigned i = 0; i < MMX_SABER_DEFAULT_FINISHER_WINDOW - 1; ++i)
    frame(0);
  check(MmxSaberComboWindowTicks() == 1,
        "the first eligible pre-player frame counts down to remaining 1");
  frame(SNES_PAD_Y);
  check(MmxZeroGetState().slash == 1 &&
            MmxSaberComboFinisherCueCount() == 1,
        "Y at remaining 1 still starts the upstream finisher");

  saber_finisher_window_setup(fixture);
  idle(MMX_SABER_DEFAULT_FINISHER_WINDOW);
  check(MmxSaberComboWindowTicks() == 0,
        "the inclusive finisher window expires after its final no-input frame");
  frame(SNES_PAD_Y);
  check(!MmxZeroGetState().slash &&
            MmxSaberAttackSnapshotGet().kind == SABER_KIND_GROUND1 &&
            MmxSaberAttackSnapshotGet().phase == SABER_PHASE_STARTUP,
        "Y one frame after expiry becomes the normal donor ground slash");
}

static void saber_finisher_option_checks(const char *fixture) {
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "finisher_window_frames", "60"),
        "test catalog sets finisher_window_frames through feature_set_option");
  printf("reference: finisher_window_frames=60 set through the copied catalog "
         "feature_set_option API before plugin activation\n");
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled(), "option-60 reactivation keeps Saber enabled");
  saber_finisher_window_setup(fixture);
  check(MmxSaberComboWindowTicks() == 60,
        "finisher_window_frames option opens a 60-frame window");
}

static void saber_finisher_atomic_checks(const char *fixture) {
  unsigned keep;

  saber_finisher_window_setup(fixture);
  keep = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    if (read_ram_word(g_ram, d) == 0) {
      keep = d;
      break;
    }
  }
  check(keep && saber_finisher_free_slots() >= 2,
        "atomic reservation probe finds at least two free slots before fill");
  saber_finisher_fill_free_slots();
  check(saber_finisher_free_slots() == 0,
        "atomic reservation probe can fill the projectile pool");
  g_ram[keep] = 0;
  g_ram[keep + 1] = 0;
  check(saber_finisher_free_slots() == 1,
        "atomic reservation probe leaves exactly one free projectile slot");
  frame(SNES_PAD_Y);
  check(!MmxZeroGetState().slash && !saber_finisher_wave_slot() &&
            !MmxSaberComboReservedSlot() &&
            MmxSaberAttackSnapshotGet().phase == SABER_PHASE_IDLE,
        "one free projectile slot makes the finisher claim atomic and inert");
}

static void saber_finisher_gate_checks(const char *fixture) {
  unsigned char previous[8] = {0};
  unsigned buster_births;
  unsigned special_births;

  saber_finisher_window_setup(fixture);
  frame(SNES_PAD_Y);
  check(MmxZeroGetState().slash == 1,
        "gate probe starts from an accepted upstream finisher");
  shot_presence(0, previous);
  frame(SNES_PAD_X);
  buster_births = new_projectiles(0, previous);
  frame(0);
  check(buster_births == 0,
        "a buster X tap fires nothing while the upstream finisher is active");

  g_ram[0xbdb] = 4; /* Fire Wave. */
  g_ram[0x1f89] = 0;
  g_ram[0x1f8a] = 0xdc;
  memset(previous, 0, sizeof(previous));
  shot_presence(8, previous);
  frame(SNES_PAD_X);
  special_births = new_projectiles(8, previous);
  check(special_births == 0,
        "a Fire Wave X tap fires nothing while the upstream finisher is active");
}

static void saber_finisher_no_emission_checks(const char *fixture) {
  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(150, SNES_PAD_X);
  for (unsigned i = 0; i < 240 && MmxZeroGetState().burst; ++i)
    frame(0);
  check(MmxZeroGetState().combo == 1 && !MmxSaberComboWindowTicks(),
        "single X3 burst has no finisher window");

  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(150, SNES_PAD_X);
  for (unsigned i = 0; i < 240 && MmxZeroGetState().burst; ++i)
    frame(0);
  saber_finisher_fill_free_slots();
  check(saber_finisher_free_slots() == 0,
        "no-emission probe fills the pool before the second-burst request");
  frame(SNES_PAD_X);
  check(MmxZeroGetState().burst != 2 && !MmxSaberComboWindowTicks(),
        "a full pool prevents the second burst from opening a window");

  /* Start burst 2 with one free slot, then keep it full through the emission
   * phase. This exercises the actual burst-2/no-new-live-bit path. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(150, SNES_PAD_X);
  for (unsigned i = 0; i < 240 && MmxZeroGetState().burst; ++i)
    frame(0);
  saber_finisher_fill_free_slots();
  unsigned keep = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    if (read_ram_word(g_ram, d) != 0) continue;
    keep = d;
    break;
  }
  check(keep == 0, "no-emission setup initially has a full native pool");
  /* The save reload above leaves all slots free after burst 1. Make exactly
   * one slot free for the second-burst request, then refill after it starts. */
  g_ram[0x1228] = 0;
  g_ram[0x1229] = 0;
  for (unsigned d = 0x1228 + 64; d < 0x1428; d += 64) {
    g_ram[d] = 1;
    g_ram[d + 1] = 2;
    g_ram[d + 10] = 3;
  }
  frame(SNES_PAD_X);
  check(MmxZeroGetState().burst == 2,
        "no-emission probe starts burst 2 while one slot is free");
  for (unsigned i = 0; i < 60 && !MmxSaberComboWindowTicks(); ++i) {
    saber_finisher_fill_free_slots();
    frame(0);
  }
  check(!MmxSaberComboWindowTicks(),
        "burst 2 with no newly live emitted slot does not open a window");
}

static void saber_finisher_checks(const char *fixture) {
  saber_finisher_accept_checks(fixture);
  saber_finisher_timing_checks(fixture);
  saber_finisher_option_checks(fixture);
  saber_finisher_atomic_checks(fixture);
  saber_finisher_gate_checks(fixture);
  saber_finisher_no_emission_checks(fixture);
  puts("ok: saber-finisher");
}

static void saber_tuning_checks(void) {
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "slash3_damage", "32"),
        "test catalog sets slash3_damage through feature_set_option");
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "boss_slash3_damage", "0"),
        "test catalog sets boss_slash3_damage through feature_set_option");
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "priority_window_frames", "71"),
        "test catalog sets priority_window_frames through feature_set_option");
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "finisher_window_frames", "60"),
        "test catalog sets finisher_window_frames through feature_set_option");
  puts("reference: Saber tuning options set through the copied catalog "
       "feature_set_option API before plugin activation");
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled(), "option-backed tuning reactivation keeps Saber enabled");
  check(MmxSaberTuningNormalDamage(MMX_SABER_TUNING_DAMAGE_SLASH3) == 32,
        "slash3_damage reaches the normal damage getter after activation");
  check(MmxSaberTuningGet()->boss_damage[MMX_SABER_TUNING_DAMAGE_SLASH3] == 0,
        "boss slash3 retains the zero fallback marker in the cache");
  check(MmxSaberTuningBossDamage(MMX_SABER_TUNING_DAMAGE_SLASH3) == 32,
        "boss zero resolves to the tuned normal slash3 damage");
  check(MmxSaberTuningPriorityWindowFrames() == 71,
        "priority_window_frames preserves an arbitrary in-range integer");
  check(MmxSaberTuningFinisherWindowFrames() == 60,
        "finisher_window_frames reaches the typed getter after activation");
  puts("ok: saber-tuning");
}

static bool debug_rects_overlap(const MmxRenderDebugRect *a,
                                const MmxRenderDebugRect *b) {
  int32_t a_right, a_bottom, b_right, b_bottom;
  if (!a || !b || !a->w || !a->h || !b->w || !b->h) return false;
  a_right = a->world_x + (int32_t)a->w - 1;
  a_bottom = a->world_y + (int32_t)a->h - 1;
  b_right = b->world_x + (int32_t)b->w - 1;
  b_bottom = b->world_y + (int32_t)b->h - 1;
  return a->world_x <= b_right && b->world_x <= a_right &&
      a->world_y <= b_bottom && b->world_y <= a_bottom;
}

static void saber_hitbox_debug_checks(const char *fixture) {
  static const int8_t slash1_record[4] = {7, -28, 12, 10};
  uint8_t x3_finisher_before[40];
  uint8_t native_weapon_before[32];
  uint8_t wave_before[MMX_SABER_WAVE_COLLISION_RECORD_BYTES];
  MmxRenderDebugRect rects[64];
  MmxRenderDebugRect slash_rect = {0};
  unsigned target, walk_frames = 0, count;
  unsigned hp_before;
  bool slash_record_checked = false;
  bool hit_overlap = false;

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(0);
  memcpy(x3_finisher_before, g_snes->cart->rom + 0x37fb0,
         sizeof(x3_finisher_before));
  memcpy(native_weapon_before, g_snes->cart->rom + 0x37f80,
         sizeof(native_weapon_before));
  memcpy(wave_before, g_snes->cart->rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
         sizeof(wave_before));
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  saber_refit_rom_checks();
  check(!memcmp(g_snes->cart->rom + 0x37fb0, x3_finisher_before,
                sizeof(x3_finisher_before)) &&
            !memcmp(g_snes->cart->rom + 0x37f80, native_weapon_before,
                    sizeof(native_weapon_before)),
        "X3 finisher and native weapon ROM records remain byte-identical");
  check(!memcmp(g_snes->cart->rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
                wave_before, sizeof(wave_before)),
        "wave ROM record remains byte-identical after Saber window install");
  MmxRendererBeginFrame(g_ram);
  check(!MmxSaberTuningShowHitboxes() &&
            MmxRendererDebugRectSnapshot(rects, 64) == 0,
        "show_hitboxes defaults off and leaves no renderer provider snapshot");

  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", "show_hitboxes", "true"),
        "test catalog sets show_hitboxes through feature_set_option");
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled() && MmxSaberTuningShowHitboxes(),
        "show_hitboxes reactivation keeps Saber enabled and turns the provider on");

  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target == 0xea8 && walk_frames == 213,
        "hitbox debug walk reaches Highway enemy $0EA8 in 213 frames");
  hp_before = g_ram[target + 0x27] & 127;
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < 45; ++i) {
    MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
    MmxRenderDebugRect active_cyan = {0};
    bool active_cyan_found = false;
    MmxRendererBeginFrame(g_ram);
    count = MmxRendererDebugRectSnapshot(rects, 64);
    for (unsigned r = 0; r < count; ++r)
      if (!active_cyan_found && rects[r].rgb555 == MMX_SABER_HITBOX_CYAN) {
        active_cyan = rects[r];
        active_cyan_found = true;
      }
    if (snapshot.phase == SABER_PHASE_ACTIVE && snapshot.tick == 4) {
      const unsigned slot = saber_active_slot();
      int center_x = slash1_record[0];
      bool found = false;
      check(slot != 0 && (g_ram[slot + 0x11] & 0x40) == 0,
            "right-facing slash publishes the unmirrored native orientation");
      if (g_ram[slot + 0x11] & 0x40) center_x = -center_x;
      slash_rect.world_x = (int16_t)read_ram_word(g_ram, 0x0bad) +
          center_x - slash1_record[2];
      slash_rect.world_y = (int16_t)read_ram_word(g_ram, 0x0bb0) +
          slash1_record[1] - slash1_record[3];
      slash_rect.w = (uint16_t)(slash1_record[2] * 2 + 1);
      slash_rect.h = (uint16_t)(slash1_record[3] * 2 + 1);
      slash_rect.rgb555 = MMX_SABER_HITBOX_CYAN;
      for (unsigned r = 0; r < count; ++r)
        if (rects[r].rgb555 == MMX_SABER_HITBOX_CYAN &&
            rects[r].world_x == slash_rect.world_x &&
            rects[r].world_y == slash_rect.world_y &&
            rects[r].w == slash_rect.w && rects[r].h == slash_rect.h) {
          found = true;
          break;
        }
      check(found && slash_rect.w == 25 && slash_rect.h == 21,
            "ACTIVE slash 1 exposes the measured collision record bounds");
      slash_record_checked = found;
    }
    if ((g_ram[target + 0x27] & 127) < hp_before && active_cyan_found) {
      for (unsigned r = 0; r < count; ++r)
        if (rects[r].rgb555 == MMX_SABER_HITBOX_RED &&
            debug_rects_overlap(&rects[r], &active_cyan)) {
          hit_overlap = true;
          break;
        }
    }
    hp_before = g_ram[target + 0x27] & 127;
    if (snapshot.phase == SABER_PHASE_IDLE) break;
    frame(0);
  }
  check(slash_record_checked,
        "the Saber provider returns a cyan donor box during ACTIVE");
  check(hit_overlap,
        "the $0EA8 red enemy box overlaps the cyan slash on the hit frame");

  /* The left-facing case is deliberately independent of the right-facing
   * record assertion above. It catches a provider that forgets native bit $40
   * while still producing a plausible right-facing rectangle. */
  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target == 0xea8 && walk_frames == 213,
        "left-facing hitbox probe reaches Highway enemy $0EA8");
  g_ram[0x0c11] &= (uint8_t)~0x40;
  g_ram[0x0bb9] &= (uint8_t)~0x40;
  frame(0);
  frame(SNES_PAD_Y);
  bool left_record_checked = false;
  for (unsigned i = 0; i < 45 && !left_record_checked; ++i) {
    MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
    MmxRendererBeginFrame(g_ram);
    count = MmxRendererDebugRectSnapshot(rects, 64);
    if (snapshot.phase == SABER_PHASE_ACTIVE && snapshot.tick == 4) {
      const unsigned slot = saber_active_slot();
      int center_x = -slash1_record[0];
      bool found = false;
      check(slot != 0 && (g_ram[slot + 0x11] & 0x40) != 0,
            "left-facing slash publishes native bit $40 for the mirrored record");
      if (g_ram[slot + 0x11] & 0x40) center_x = -slash1_record[0];
      slash_rect.world_x = (int16_t)read_ram_word(g_ram, 0x0bad) +
          center_x - slash1_record[2];
      slash_rect.world_y = (int16_t)read_ram_word(g_ram, 0x0bb0) +
          slash1_record[1] - slash1_record[3];
      slash_rect.w = (uint16_t)(slash1_record[2] * 2 + 1);
      slash_rect.h = (uint16_t)(slash1_record[3] * 2 + 1);
      slash_rect.rgb555 = MMX_SABER_HITBOX_CYAN;
      for (unsigned r = 0; r < count; ++r)
        if (rects[r].rgb555 == MMX_SABER_HITBOX_CYAN &&
            rects[r].world_x == slash_rect.world_x &&
            rects[r].world_y == slash_rect.world_y &&
            rects[r].w == slash_rect.w && rects[r].h == slash_rect.h) {
          found = true;
          break;
        }
      check(found,
            "left-facing ACTIVE slash mirrors the measured record's X center offset");
      left_record_checked = found;
    }
    if (!left_record_checked) frame(0);
  }
  check(left_record_checked,
        "left-facing mutation coverage reaches the mirrored cyan slash box");
}

static unsigned saber_wave_live_count(void) {
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64)
    if (MmxSaberWaveRuntimeOwns(g_ram, d)) ++count;
  return count;
}

static bool saber_wave_finisher_empty(void) {
  return !MmxSaberWaveRuntimeActive(g_ram) &&
      !saber_finisher_wave_slot() && !MmxSaberComboReservedSlot() &&
      !MmxSaberComboWindowTicks();
}

static void saber_wave_keep_onscreen(unsigned slot) {
  const int x = (int16_t)read_ram_word(g_ram, slot + 5);
  write_ram_word(g_ram, 0x1e4d, (uint16_t)(x - 128));
}

static unsigned saber_wave_launch(const char *fixture, bool left) {
  unsigned slot;
  bool reserved_ages = true;

  saber_finisher_window_setup(fixture);
  /* The upstream X3 burst has finished by the open window. Set the native
   * facing immediately before the legacy request, matching old $C11 use. */
  g_ram[0x0c11] = left ? 0 : 0x40;
  g_ram[0x0bb9] = (g_ram[0x0bb9] & (uint8_t)~0x40) | (left ? 0 : 0x40);
  frame(SNES_PAD_Y);
  slot = saber_finisher_wave_slot();
  check(MmxZeroGetState().slash == 1 && slot,
        "wave launch starts the upstream finisher and keeps its reservation");
  const unsigned generation = read_ram_word(g_ram, slot + 0x3e);
  check((generation & MMX_SABER_WAVE_TAG_FAMILY_MASK) ==
            MMX_SABER_WAVE_TAG_FAMILY && (generation & 0xff) != 0,
        "wave reservation carries a nonzero $5600 generation");
  for (unsigned age = 2; age <= 7; ++age) {
    frame(0);
    if (age <= 6)
      reserved_ages &= !g_ram[slot] && g_ram[slot + 1] == 0x80 &&
          read_ram_word(g_ram, slot + 0x3e) == generation;
  }
  check(reserved_ages,
        "reserved wave stays inactive through finisher ages 1-6");
  check(MmxZeroGetState().slash == 7 && MmxSaberWaveRuntimeOwns(g_ram, slot),
        "finisher age 7 publishes the reserved wave");
  const int player_x = (int16_t)read_ram_word(g_ram, 0x0bad);
  const int player_y = (int16_t)read_ram_word(g_ram, 0x0bb0);
  const int wave_x = (int16_t)read_ram_word(g_ram, slot + 5);
  const int wave_y = (int16_t)read_ram_word(g_ram, slot + 8);
  check(wave_x == player_x + (left ? -24 : 24) && wave_y == player_y - 6 &&
            (g_ram[slot + 0x11] & 0x40) == (left ? 0 : 0x40),
        left ? "left wave publishes at player X-24/Y-6" :
               "right wave publishes at player X+24/Y-6");
  check(read_ram_word(g_ram, slot + 0x20) ==
            MMX_SABER_WAVE_COLLISION_POINTER,
        "published wave installs the old $FFA0 collision pointer");
  check(!MmxSaberComboReservedSlot() &&
            g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] == 0,
        "age-7 publication releases the reservation before active travel");
  return slot;
}

static unsigned priority_classify_live(MmxSaberPriorityClass expected,
                                       unsigned expected_priority,
                                       const char *label) {
  unsigned found = 0;
  unsigned count = 0;
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    MmxSaberPriorityClassification result;
    if (!g_ram[d] || !MmxSaberPriorityClassify(g_ram, d, &result)) continue;
    if (result.priority_class == expected &&
        result.priority == expected_priority) {
      found = d;
      ++count;
    }
  }
  check(count == 1, label);
  return found;
}

static unsigned priority_find_class(MmxSaberPriorityClass expected,
                                    unsigned expected_priority) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    MmxSaberPriorityClassification result;
    if (!g_ram[d] || !MmxSaberPriorityClassify(g_ram, d, &result)) continue;
    if (result.priority_class == expected &&
        result.priority == expected_priority)
      return d;
  }
  return 0;
}

static unsigned priority_wait_for_class(MmxSaberPriorityClass expected,
                                        unsigned expected_priority) {
  for (unsigned i = 0; i < 160; ++i) {
    unsigned slot = priority_find_class(expected, expected_priority);
    if (slot) return slot;
    frame(0);
  }
  return 0;
}

static unsigned priority_direct_ground(unsigned target_index) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberFrameReset();
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  if (!target_index) {
    snapshot = MmxSaberAttackSnapshotGet();
    for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
      MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
      MmxSaberAttackRuntimeTick(g_ram);
      snapshot = MmxSaberAttackSnapshotGet();
    }
  } else {
    for (unsigned index = 0; index < target_index; ++index) {
      const MmxSaberAttack *attack = MmxSaberAttackRecord(
          (MmxSaberPadKind)(SABER_KIND_GROUND1 + index), (uint8_t)index);
      check(attack != NULL, "priority direct ground attack record exists");
      for (unsigned tick = 1; tick < attack->chain_open_tick; ++tick) {
        MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
        MmxSaberAttackRuntimeTick(g_ram);
      }
      MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
      MmxSaberAttackRuntimeTick(g_ram);
    }
  }
  return saber_active_slot();
}

/* Start a direct combo member without clearing the priority sidecar. The
 * ordinary classifier tests intentionally reset the complete Saber frame; the
 * priority tests need the preceding accepted hit to remain in history. */
static unsigned priority_ground_attack_preserving_history(unsigned target_index) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberAttackResetRam(g_ram);
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  if (!target_index) {
    snapshot = MmxSaberAttackSnapshotGet();
    for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
      MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
      MmxSaberAttackRuntimeTick(g_ram);
      snapshot = MmxSaberAttackSnapshotGet();
    }
  } else {
    for (unsigned index = 0; index < target_index; ++index) {
      const MmxSaberAttack *attack = MmxSaberAttackRecord(
          (MmxSaberPadKind)(SABER_KIND_GROUND1 + index), (uint8_t)index);
      check(attack != NULL,
            "priority preserving ground attack record exists");
      for (unsigned tick = 1; tick < attack->chain_open_tick; ++tick) {
        MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
        MmxSaberAttackRuntimeTick(g_ram);
      }
      MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
      MmxSaberAttackRuntimeTick(g_ram);
    }
  }
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_ACTIVE,
        "priority preserving ground attack reaches an active phase");
  return saber_active_slot();
}

static unsigned priority_air_attack_preserving_history(void) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberAttackResetRam(g_ram);
  MmxSaberAttackStepWithWallAndDash(true, false, false, false, false, true,
                                    g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    MmxSaberAttackStepWithWallAndDash(false, false, false, false, false, true,
                                      g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.kind == SABER_KIND_AIR &&
            snapshot.phase == SABER_PHASE_ACTIVE,
        "priority preserving air attack reaches an active phase");
  return saber_active_slot();
}

typedef struct SaberBusterOracleSpec {
  const char *name;
  MmxSaberPriorityClass priority_class;
  unsigned priority;
  unsigned charge_frames;
  bool second_burst;
} SaberBusterOracleSpec;

static const SaberBusterOracleSpec kSaberBusterOracleSpecs[] = {
  {"small", MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL, 2, 30, false},
  {"full", MMX_SABER_PRIORITY_CLASS_CHARGE_FULL, 2, 90, false},
  {"max1", MMX_SABER_PRIORITY_CLASS_MAX_SHOT1, 2, 150, false},
  {"max2", MMX_SABER_PRIORITY_CLASS_MAX_SHOT2, 3, 150, true},
};

static unsigned priority_start_buster_oracle(
    const char *fixture, const SaberBusterOracleSpec *spec,
    unsigned *target, unsigned *walk_frames) {
  unsigned slot = 0;
  unsigned first_burst = 0;

  if (target) *target = walk_to_ground_enemy(fixture, walk_frames);
  check(target && *target == 0xea8,
        "buster oracle reaches the real Highway enemy slot $0EA8");
  g_ram[0xbdb] = 0;
  g_ram[0xc0f] = 3;
  g_ram[0x1f12] = 0;
  g_ram[0x1f99] = 0;
  g_ram[0xbaa] = 0;
  g_ram[0xbab] = 0;
  g_ram[0xd1] = 2;
  g_ram[0xd2] = 4;
  g_ram[0xd3] = 4;
  g_ram[0x1f0c] = 0;
  g_ram[0x1f10] = 0;
  g_ram[0xbbe] = 0;
  MmxZeroCancel(g_ram);
  write_ram_word(g_ram, 0x0bad, read_ram_word(g_ram, *target + 5) - 600);
  g_ram[0xc11] |= 0x40;
  g_ram[0xbb9] |= 0x40;
  g_ram[*target + 0x27] = 16;
  g_ram[*target + 0x35] = 0;
  MmxSaberFrameReset();

  release_charge_button(spec->charge_frames, SNES_PAD_X);
  if (!spec->second_burst) {
    slot = priority_wait_for_class(spec->priority_class, spec->priority);
  } else {
    first_burst = priority_wait_for_class(
        MMX_SABER_PRIORITY_CLASS_MAX_SHOT1, 2);
    check(first_burst != 0,
          "buster oracle publishes the first maximum-shot burst");
    for (unsigned i = 0; i < 240 &&
         (MmxZeroGetState().burst || MmxZeroGetState().shot_mask ||
          g_ram[0xc25]); ++i)
      frame(0);
    g_ram[*target + 0x27] = 16;
    g_ram[*target + 0x35] = 0;
    frame(SNES_PAD_X);
    slot = priority_wait_for_class(spec->priority_class, spec->priority);
  }
  check(slot != 0, "buster oracle publishes the requested buster class");
  check(MmxSaberPriorityTuned(spec->priority_class) ==
            (int)spec->priority,
        "buster oracle sees the configured priority for the requested class");
  write_ram_word(g_ram, 0x0bad, read_ram_word(g_ram, *target + 5) - 27);
  g_ram[*target] = 1;
  g_ram[*target + 0x27] = 16;
  g_ram[*target + 0x35] = 0;
  write_ram_word(g_ram, slot + 5, read_ram_word(g_ram, *target + 5));
  write_ram_word(g_ram, slot + 8, read_ram_word(g_ram, *target + 8));
  g_ram[slot + 0x11] |= 0x40;
  return slot;
}

typedef struct ArmadilloPriorityTrace {
  unsigned response_calls;
  unsigned native_response;
  unsigned extension_response;
  unsigned response_row;
  unsigned response_timer;
  unsigned response_kind;
  unsigned response_projectile;
  unsigned response_projectile_class;
  unsigned response_cpu_y;
  unsigned response_cpu_db;
  unsigned response_cpu_pb;
  unsigned response_cpu_x_flag;
  unsigned response_cpu_m_flag;
  unsigned response_cpu_p;
  unsigned response_native_class;
  unsigned weapons_enabled;
  unsigned damage_calls;
  unsigned damage_projectile;
  unsigned damage_input;
  unsigned damage_output;
} ArmadilloPriorityTrace;

static ArmadilloPriorityTrace armadillo_priority_trace;
static unsigned armadillo_priority_frame;

static unsigned armadillo_priority_response(uint8_t *ram, unsigned enemy,
                                            unsigned projectile,
                                            unsigned original) {
  unsigned response;
  ++armadillo_priority_trace.response_calls;
  armadillo_priority_trace.native_response = original;
  armadillo_priority_trace.response_row = ram[enemy + 0x28];
  armadillo_priority_trace.response_timer = ram[enemy + 0x38];
  armadillo_priority_trace.response_kind = ram[enemy + 0x0a];
  armadillo_priority_trace.response_projectile = projectile;
  armadillo_priority_trace.response_projectile_class = ram[projectile + 0x0a];
  armadillo_priority_trace.response_cpu_y = g_cpu.Y;
  armadillo_priority_trace.response_cpu_db = g_cpu.DB;
  armadillo_priority_trace.response_cpu_pb = g_cpu.PB;
  armadillo_priority_trace.response_cpu_x_flag = g_cpu.x_flag;
  armadillo_priority_trace.response_cpu_m_flag = g_cpu.m_flag;
  armadillo_priority_trace.response_cpu_p = g_cpu.P;
  armadillo_priority_trace.response_native_class = ram[0];
  armadillo_priority_trace.weapons_enabled = MmxWeaponsEnabled();
  printf("reference: armadillo-frame=%u response native=0x%02X row=0x%02X "
         "hp=0x%02X +38=0x%02X +39=0x%02X +37=0x%02X +30=0x%02X "
         "projectile=0x%X pclass=0x%02X Y=0x%04X DB=0x%02X PB=0x%02X "
         "M=%u X=%u P=0x%02X\n",
         armadillo_priority_frame, original, ram[enemy + 0x28],
         ram[enemy + 0x27], ram[enemy + 0x38], ram[enemy + 0x39],
         ram[enemy + 0x37], ram[enemy + 0x30], projectile,
         ram[projectile + 0x0a], armadillo_priority_trace.response_cpu_y,
         armadillo_priority_trace.response_cpu_db,
         armadillo_priority_trace.response_cpu_pb,
         armadillo_priority_trace.response_cpu_m_flag,
         armadillo_priority_trace.response_cpu_x_flag,
         armadillo_priority_trace.response_cpu_p);
  response = MmxSaberPriorityResponse(ram, enemy, projectile, original);
  armadillo_priority_trace.extension_response = response;
  return response;
}

static unsigned armadillo_priority_damage(uint8_t *ram, unsigned enemy,
                                          unsigned projectile,
                                          unsigned original) {
  unsigned damage;
  ++armadillo_priority_trace.damage_calls;
  armadillo_priority_trace.damage_projectile = projectile;
  armadillo_priority_trace.damage_input = original;
  damage = MmxSaberAttackDamage(ram, enemy, projectile, original);
  armadillo_priority_trace.damage_output = damage;
  printf("reference: armadillo-frame=%u damage native=0x%02X output=%u "
         "hp=0x%02X +38=0x%02X +39=0x%02X\n",
         armadillo_priority_frame, original, damage, ram[enemy + 0x27],
         ram[enemy + 0x38], ram[enemy + 0x39]);
  return damage;
}

static const MmxZeroExtension armadillo_priority_extension = {
    .response = armadillo_priority_response,
    .damage = armadillo_priority_damage,
};

/* Enter the generated native collision routine at $849E15 with the real
 * enemy/projectile slots from the fixture. This keeps the native response and
 * damage path as the oracle; the test does not reproduce either table lookup. */
static void native_buster_contact(unsigned enemy, unsigned projectile) {
  CpuState saved_cpu = g_cpu;
  RecompReturn result;

  g_cpu.D = (uint16_t)enemy;
  g_cpu.X = (uint16_t)projectile;
  g_cpu.DB = 0x86;
  g_cpu.PB = 0x84;
  g_cpu.m_flag = 1;
  g_cpu.x_flag = 0;
  g_cpu.P = (uint8_t)((g_cpu.P & (uint8_t)~0x38) | 0x20);
  g_cpu._flag_D = 0;
  g_cpu.A = 0;
  g_cpu.Y = 0;
  g_cpu.host_return_valid = 0;
  result = cpu_dispatch_call_pc(&g_cpu, 0x849e15, 0x849e12);
  g_cpu = saved_cpu;
  check(result == RECOMP_RETURN_NORMAL,
        "native buster oracle returns from the generated collision routine");
}

/* Enter Armored Armadillo after his state-handler dispatch at $83B2ED. This
 * keeps row selection, the post-hit timer, generic collision, and the native
 * post-collision restore/reaction code in the oracle path. */
static void native_armadillo_contact(unsigned enemy, unsigned projectile) {
  CpuState saved_cpu = g_cpu;
  RecompReturn result;

  g_cpu.D = (uint16_t)enemy;
  g_cpu.X = (uint16_t)projectile;
  g_cpu.DB = 0x00;
  g_cpu.PB = 0x83;
  g_cpu.m_flag = 1;
  g_cpu.x_flag = 1;
  g_cpu.P = (uint8_t)((g_cpu.P & (uint8_t)~0x38) | 0x30);
  g_cpu._flag_D = 0;
  g_cpu._flag_C = 0;
  g_cpu._flag_V = 0;
  g_cpu._flag_Z = 0;
  g_cpu._flag_N = 0;
  g_cpu.A = 0;
  g_cpu.Y = 0;
  g_cpu.host_return_valid = 0;
  result = cpu_dispatch_call_pc(&g_cpu, 0x83b2ed, 0x83b2ea);
  printf("reference: Armadillo direct dispatch result=%d PB=0x%02X S=0x%04X "
         "host_return_valid=%u HP=0x%02X timer=0x%02X\n",
         (int)result, g_cpu.PB, g_cpu.S, g_cpu.host_return_valid,
         g_ram[enemy + 0x27], g_ram[enemy + 0x38]);
  g_cpu = saved_cpu;
  check(result == RECOMP_RETURN_NORMAL || result == RECOMP_RETURN_SKIP_1,
        "native Armadillo oracle reaches the post-collision continuation");
}

static unsigned measure_native_buster_drop(
    const char *fixture, const SaberBusterOracleSpec *spec) {
  unsigned target = 0;
  unsigned walk_frames = 0;
  unsigned slot = priority_start_buster_oracle(
      fixture, spec, &target, &walk_frames);
  const unsigned initial_hp = g_ram[target + 0x27] & 127;
  unsigned final_hp = initial_hp;
  native_buster_contact(target, slot);
  final_hp = g_ram[target + 0x27] & 127;
  check(slot != 0, "native buster oracle publishes a real buster slot");
  printf("reference: buster-native class=%s walk=%u HP=%u->%u drop=%u\n",
         spec->name, walk_frames, initial_hp, final_hp,
         initial_hp - final_hp);
  return initial_hp - final_hp;
}

static unsigned measure_bypassed_buster_drop(
    const char *fixture, const SaberBusterOracleSpec *spec) {
  unsigned target = 0;
  unsigned walk_frames = 0;
  unsigned slot = priority_start_buster_oracle(
      fixture, spec, &target, &walk_frames);
  unsigned lower_slot = priority_air_attack_preserving_history();
  unsigned lower_damage = MmxSaberAttackDamage(g_ram, target, lower_slot, 1);
  unsigned lower_drop = apply_test_damage(target, lower_damage);
  MmxSaberPriorityHistory lower_history;
  const bool lower_history_found =
      MmxSaberPriorityHistoryLookup(g_ram, target, &lower_history);
  check(lower_slot != 0 && lower_drop == lower_damage && lower_damage != 0 &&
            lower_history_found &&
            lower_history.priority == 1,
        "buster oracle records a real lower-priority Saber HP drop");

  /* This is the native post-hit state observed for the real enemy: row 5 and
   * timer $46 make the next buster response read zero. The native positive
   * row remains the row captured by the accepted lower hit. */
  g_ram[target + 0x28] = 0x05;
  g_ram[target + 0x35] = 0x46;
  const unsigned before_bypass = g_ram[target + 0x27] & 127;
  const unsigned response = MmxZeroResponse(g_ram, target, slot, 0);
  native_buster_contact(target, slot);
  const unsigned final_hp = g_ram[target + 0x27] & 127;
  const unsigned bypass_drop = before_bypass - final_hp;
  check(response == 1 && bypass_drop != 0 &&
            final_hp == before_bypass - bypass_drop,
        "buster oracle bypass produces a real HP drop through the native pending-token path");
  printf("reference: buster-bypass class=%s walk=%u HP=%u->%u drop=%u\n",
         spec->name, walk_frames, before_bypass, final_hp, bypass_drop);
  return bypass_drop;
}

static void saber_priority_buster_oracle_priorities(bool restore) {
  const char *package = "megaman-x.character.saber-zero";
  const char *feature = "saber-zero";
  const char *small_priority = restore ? "1" : "2";
  const char *full_priority = restore ? "1" : "2";
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, package, feature,
            "charge_small_priority", small_priority),
        "buster oracle sets charge-small priority in the isolated catalog");
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, package, feature,
            "charge_full_priority", full_priority),
        "buster oracle sets charge-full priority in the isolated catalog");
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled(), "buster oracle priority reactivation keeps Saber enabled");
}

static unsigned priority_direct_context(MmxSaberPadKind expected_kind,
                                         bool grounded, bool wall,
                                         bool dash) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberFrameReset();
  MmxSaberAttackStepWithWallAndDash(
      true, grounded, wall, dash, false, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    MmxSaberAttackStepWithWallAndDash(
        false, grounded, wall, dash, false, true, g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.kind == expected_kind &&
            snapshot.phase == SABER_PHASE_ACTIVE,
        "priority direct context reaches an active Saber slot");
  return saber_active_slot();
}

static void saber_priority_classify_checks(const char *fixture) {
  unsigned slot;

  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(30, SNES_PAD_X);
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL, 1,
                         "tier-4 live shot classifies as small charge");

  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(90, SNES_PAD_X);
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_CHARGE_FULL, 1,
                         "tier-6 live shot classifies as full charge");

  load_fixture(fixture);
  MmxSaberFrameReset();
  release_charge_button(150, SNES_PAD_X);
  slot = priority_wait_for_class(MMX_SABER_PRIORITY_CLASS_MAX_SHOT1, 2);
  check(slot != 0,
        "burst-1 live shot classifies as maximum shot 1 with tuned priority");
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask ||
        g_ram[0xc25]); ++i)
    frame(0);
  frame(SNES_PAD_X);
  slot = priority_wait_for_class(MMX_SABER_PRIORITY_CLASS_MAX_SHOT2, 3);
  check(slot != 0,
        "burst-2 live shot classifies as maximum shot 2 with tuned priority");

  load_fixture(fixture);
  slot = priority_direct_ground(0);
  check(slot != 0, "ground slash 1 publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_SLASH1, 2,
                         "ground slash 1 slot classifies correctly");
  slot = priority_direct_ground(1);
  check(slot != 0, "ground slash 2 publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_SLASH2, 3,
                         "ground slash 2 slot classifies correctly");
  slot = priority_direct_ground(2);
  check(slot != 0, "ground slash 3 publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_SLASH3, 4,
                         "ground slash 3 slot classifies correctly");

  slot = priority_direct_context(SABER_KIND_AIR, false, false, false);
  check(slot != 0, "air slash publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_AIR, 1,
                         "air slash slot classifies correctly");
  slot = priority_direct_context(SABER_KIND_WALL, false, true, false);
  check(slot != 0, "wall slash publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_WALL, 1,
                         "wall slash slot classifies correctly");
  slot = priority_direct_context(SABER_KIND_DASH, true, false, true);
  check(slot != 0, "dash slash publishes a slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_DASH, 5,
                         "dash slash slot classifies correctly");

  slot = saber_wave_launch(fixture, false);
  check(slot != 0, "priority finisher setup publishes a live wave");
  check(saber_finisher_slot_with_tag(0x5a53) != 0,
        "priority finisher setup keeps a live $5A53 slot");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_X3_FINISHER, 4,
                         "$5A53 finisher slot classifies correctly");
  priority_classify_live(MMX_SABER_PRIORITY_CLASS_WAVE, 5,
                         "$5600 wave slot classifies correctly");

  puts("ok: saber-priority-classify");
}

static unsigned saber_priority_penguin_room(const char *fixture,
                                            const char *fallback) {
  unsigned walk_frames = 0;
  unsigned target = walk_to_response_enemy(fixture, &walk_frames);
  if (target != 0x0e68 && fallback && strcmp(fixture, fallback)) {
    printf("reference: penguin-room has no live response target; using "
           "penguin-fight fallback at slot $0E68\n");
    walk_frames = 0;
    target = walk_to_response_enemy(fallback, &walk_frames);
  }
  check(target == 0x0e68 && walk_frames != 0,
        "penguin priority walk reaches native i-frame enemy slot $0E68");
  return target;
}

static void saber_priority_checks(const char *x1_rom, const char *x3_rom,
                                  const char *fixture, const char *assets,
                                  const char *fixture_dir) {
  char room_path[4096];
  char fight_path[4096];
  const unsigned enemy = 0x0e68;
  const unsigned room_damage_row = 0x05;
  const unsigned room_iframe_timer = 0x46;
  int written;

  written = snprintf(room_path, sizeof(room_path), "%s/%s", fixture_dir,
                     "penguin-room.sav");
  check(written >= 0 && written < (int)sizeof(room_path),
        "saber-priority penguin-room path fits");
  written = snprintf(fight_path, sizeof(fight_path), "%s/%s", fixture_dir,
                     "penguin-fight.sav");
  check(written >= 0 && written < (int)sizeof(fight_path),
        "saber-priority penguin-fight path fits");
  check(readable_file(room_path) && readable_file(fight_path),
        "saber-priority penguin fixtures exist");

  /* Penguin-room: the fixture's documented native post-hit state is row 5
   * with timer $46. The first positive Saber hit establishes history; the
   * following slash 3 calls the real response and damage extension seams
   * with the native response value forced to zero. */
  unsigned room_target = saber_priority_penguin_room(room_path, fight_path);
  check(g_ram[enemy] && (g_ram[enemy + 0x27] & 127),
        "penguin-room native i-frame target is live at slot $0E68");
  check(room_target == enemy, "penguin-room priority target uses slot $0E68");
  MmxSaberFrameReset();
  unsigned initial_hp = g_ram[enemy + 0x27] & 127;
  unsigned lower_slot = priority_ground_attack_preserving_history(0);
  unsigned lower_damage = MmxZeroDamage(g_ram, enemy, lower_slot, 1);
  unsigned after_lower_hp = g_ram[enemy + 0x27] & 127;
  apply_test_damage(enemy, lower_damage);
  after_lower_hp = g_ram[enemy + 0x27] & 127;
  check(lower_slot != 0 && lower_damage == 1,
        "penguin-room default boss slash 1 damage is 1");

  g_ram[enemy + 0x28] = room_damage_row;
  g_ram[enemy + 0x35] = room_iframe_timer;
  unsigned higher_slot = priority_ground_attack_preserving_history(2);
  unsigned higher_response = MmxZeroResponse(g_ram, enemy, higher_slot, 0);
  unsigned higher_damage = MmxZeroDamage(g_ram, enemy, higher_slot, 0);
  unsigned after_higher_hp = g_ram[enemy + 0x27] & 127;
  apply_test_damage(enemy, higher_damage);
  after_higher_hp = g_ram[enemy + 0x27] & 127;
  unsigned second_bypass_damage = MmxZeroDamage(g_ram, enemy, higher_slot, 0);
  check(higher_response == 1 && higher_damage == 2 &&
            second_bypass_damage == 0,
        "penguin-room default boss slash 3 damage is 2 and bypasses once");

  unsigned equal_response = MmxZeroResponse(g_ram, enemy, higher_slot, 0x00);
  unsigned equal_damage = MmxZeroDamage(g_ram, enemy, higher_slot, 0);
  unsigned lower_again = priority_ground_attack_preserving_history(0);
  unsigned lower_response = MmxZeroResponse(g_ram, enemy, lower_again, 0);
  unsigned lower_again_damage = MmxZeroDamage(g_ram, enemy, lower_again, 0);
  check(equal_response == 0 && equal_damage == 0 && lower_response == 0 &&
            lower_again_damage == 0 &&
            (g_ram[enemy + 0x27] & 127) == after_higher_hp,
        "equal and lower priority contacts remain swallowed during i-frames");
  check(MmxZeroResponse(g_ram, enemy, higher_slot, 0x80) == 0x80,
        "reflection bit 7 is preserved on the response seam");
  printf("reference: penguin-room enemy=0x%X native_row=0x%02X timer=0x%02X "
         "HP=%u->%u->%u equal=%u lower=%u once=%u\n",
         enemy, g_ram[enemy + 0x28], g_ram[enemy + 0x35], initial_hp,
         after_lower_hp, after_higher_hp, equal_damage, lower_again_damage,
         second_bypass_damage);

  /* The inclusive window is checked with a higher candidate at frame 71,
   * after a slash 2 history at frame 0. */
  saber_priority_penguin_room(room_path, fight_path);
  MmxSaberFrameReset();
  unsigned window_lower = priority_ground_attack_preserving_history(1);
  check(MmxZeroDamage(g_ram, enemy, window_lower, 1) != 0,
        "window setup records a positive slash 2 hit");
  g_ram[enemy + 0x28] = room_damage_row;
  unsigned window_higher = priority_ground_attack_preserving_history(2);
  for (unsigned i = 0; i < 71; ++i)
    MmxSaberPriorityObservePrePlayer(g_ram, 0, 0);
  check(MmxZeroResponse(g_ram, enemy, window_higher, 0) == 0 &&
            MmxZeroDamage(g_ram, enemy, window_higher, 0) == 0,
        "a higher priority after window+1 frames cannot bypass");

  /* MmxSaberFrameReset is the same non-serialized state_reset owner path used
   * by load/rewind. It must retire the eligible history before this candidate. */
  saber_priority_penguin_room(room_path, fight_path);
  MmxSaberFrameReset();
  unsigned reset_lower = priority_ground_attack_preserving_history(0);
  check(MmxZeroDamage(g_ram, enemy, reset_lower, 1) != 0,
        "reset setup records a positive slash 1 hit");
  g_ram[enemy + 0x28] = room_damage_row;
  MmxSaberFrameReset();
  unsigned reset_higher = priority_ground_attack_preserving_history(2);
  check(MmxZeroResponse(g_ram, enemy, reset_higher, 0) == 0 &&
            MmxZeroDamage(g_ram, enemy, reset_higher, 0) == 0,
        "state_reset clears history before an otherwise eligible bypass");

  /* Ground combo damage is measured through all three active members. Each
   * direct member is started without clearing the priority sidecar, so the
   * positive damage callback records 2 -> 3 -> 4 in one quick sequence. */
  unsigned walk_frames = 0;
  unsigned combo_enemy = walk_to_ground_enemy(fixture, &walk_frames);
  check(combo_enemy == 0xea8,
        "saber-priority combo reaches the documented Highway enemy slot");
  g_ram[combo_enemy + 0x27] = 32;
  unsigned combo_hp[4] = {32, 0, 0, 0};
  unsigned combo_damage[3] = {0, 0, 0};
  for (unsigned i = 0; i < 3; ++i) {
    unsigned slot = priority_ground_attack_preserving_history(i);
    combo_damage[i] = MmxSaberAttackDamage(g_ram, combo_enemy, slot, 1);
    apply_test_damage(combo_enemy, combo_damage[i]);
    combo_hp[i + 1] = g_ram[combo_enemy + 0x27] & 127;
  }
  check(combo_damage[0] == MmxSaberTuningNormalDamage(
                             MMX_SABER_TUNING_DAMAGE_SLASH1) &&
            combo_damage[1] == MmxSaberTuningNormalDamage(
                             MMX_SABER_TUNING_DAMAGE_SLASH2) &&
            combo_damage[2] == MmxSaberTuningNormalDamage(
                             MMX_SABER_TUNING_DAMAGE_SLASH3),
        "ground combo records and returns the three tuned normal damages");
  printf("reference: ground-combo enemy=0x%X walk=%u priorities=2/3/4 "
         "HP=%u->%u->%u->%u damage=%u/%u/%u\n",
         combo_enemy, walk_frames, combo_hp[0], combo_hp[1], combo_hp[2],
         combo_hp[3], combo_damage[0], combo_damage[1], combo_damage[2]);

  /* The buster oracle is empirical: each class first hits the real Highway
   * enemy through the native collision path, then repeats after a real lower
   * priority Saber HP drop has put that same enemy in its native i-frame
   * state. No ROM table byte is read or reconstructed by this test. */
  saber_priority_buster_oracle_priorities(false);
  unsigned native_buster_drops[
      sizeof(kSaberBusterOracleSpecs) / sizeof(kSaberBusterOracleSpecs[0])];
  unsigned bypass_buster_drops[
      sizeof(kSaberBusterOracleSpecs) / sizeof(kSaberBusterOracleSpecs[0])];
  for (unsigned i = 0; i < sizeof(kSaberBusterOracleSpecs) /
                               sizeof(kSaberBusterOracleSpecs[0]); ++i)
    native_buster_drops[i] = measure_native_buster_drop(
        fixture, &kSaberBusterOracleSpecs[i]);
  for (unsigned i = 0; i < sizeof(kSaberBusterOracleSpecs) /
                               sizeof(kSaberBusterOracleSpecs[0]); ++i)
    bypass_buster_drops[i] = measure_bypassed_buster_drop(
        fixture, &kSaberBusterOracleSpecs[i]);
  for (unsigned i = 0; i < sizeof(kSaberBusterOracleSpecs) /
                               sizeof(kSaberBusterOracleSpecs[0]); ++i) {
    check(native_buster_drops[i] == bypass_buster_drops[i] &&
              native_buster_drops[i] > 0 && native_buster_drops[i] <= 32,
          "priority-bypassed buster HP drop equals its native HP oracle");
    printf("reference: buster-oracle class=%s native_drop=%u bypass_drop=%u\n",
           kSaberBusterOracleSpecs[i].name, native_buster_drops[i],
           bypass_buster_drops[i]);
  }
  saber_priority_buster_oracle_priorities(true);

  /* Boss bypasses use the tuned boss value directly: no HP-1 clamp. */
  unsigned boss_walk = 0;
  unsigned boss_target = walk_to_response_enemy(fight_path, &boss_walk);
  check(boss_target == enemy && boss_walk != 0,
        "penguin-fight walk reaches the boss at slot $0E68");
  MmxSaberFrameReset();
  unsigned boss = saber_damage_boss_slot();
  unsigned boss_slash1 = MmxSaberTuningBossDamage(
      MMX_SABER_TUNING_DAMAGE_SLASH1);
  unsigned boss_slash3 = MmxSaberTuningBossDamage(
      MMX_SABER_TUNING_DAMAGE_SLASH3);
  check(boss != 0 && MmxWidePolicy_IsBossEncounter(g_ram[boss + 0x0a]),
        "penguin-fight exposes the boss target for priority damage");
  check(boss_slash1 == 1 && boss_slash3 == 2,
        "penguin-fight default boss slash damage is 1 and 2");
  g_ram[boss + 0x27] = (uint8_t)(boss_slash1 + boss_slash3);
  unsigned boss_lower = priority_ground_attack_preserving_history(0);
  unsigned boss_first = MmxSaberAttackDamage(g_ram, boss, boss_lower, 1);
  apply_test_damage(boss, boss_first);
  g_ram[boss + 0x28] = room_damage_row;
  unsigned boss_higher = priority_ground_attack_preserving_history(2);
  check(MmxZeroResponse(g_ram, boss, boss_higher, 0) == 1,
        "penguin-fight boss accepts the higher priority bypass response");
  unsigned boss_bypass = MmxZeroDamage(g_ram, boss, boss_higher, 0);
  apply_test_damage(boss, boss_bypass);
  printf("reference: boss kind=0x%02X HP=%u->%u->%u damage=%u/%u\n",
         g_ram[boss + 0x0a], boss_slash1 + boss_slash3,
         boss_slash3, g_ram[boss + 0x27] & 127, boss_first, boss_bypass);
  check(boss_first == boss_slash1 && boss_bypass == boss_slash3 &&
            (g_ram[boss + 0x27] & 127) == 0,
        "boss bypass deals the full tuned boss damage and reaches HP zero");

  /* With Saber disabled its extension is not registered, so the same native
   * zero response and zero damage remain byte-identical and HP is unchanged. */
  activate_zero(x1_rom, x3_rom, assets, false, false);
  saber_priority_penguin_room(room_path, fight_path);
  check(!MmxSaberEnabled(), "saber-priority disabled check turns Saber off");
  const unsigned disabled_slot = 0x1228;
  memset(g_ram + disabled_slot, 0, 64);
  g_ram[disabled_slot] = 1;
  g_ram[disabled_slot + 0x0a] = 3;
  g_ram[disabled_slot + 0x3e] = 0x01;
  g_ram[disabled_slot + 0x3f] = 0x53;
  g_ram[enemy + 0x27] = 32;
  unsigned disabled_initial = g_ram[enemy + 0x27] & 127;
  unsigned disabled_first_response = MmxZeroResponse(g_ram, enemy,
                                                      disabled_slot, 1);
  unsigned disabled_first_damage = MmxZeroDamage(g_ram, enemy, disabled_slot, 3);
  apply_test_damage(enemy, disabled_first_damage);
  g_ram[enemy + 0x28] = room_damage_row;
  unsigned disabled_second_response = MmxZeroResponse(g_ram, enemy,
                                                       disabled_slot, 0);
  unsigned disabled_second_damage = MmxZeroDamage(g_ram, enemy, disabled_slot, 0);
  check(disabled_first_response == 1 && disabled_first_damage == 3 &&
            disabled_second_response == 0 && disabled_second_damage == 0 &&
            (g_ram[enemy + 0x27] & 127) == disabled_initial - 3,
        "Saber-disabled sequence leaves the native zero-response hit swallowed");
  printf("reference: disabled HP=%u->%u response=%u->%u damage=%u->%u\n",
         disabled_initial, g_ram[enemy + 0x27] & 127,
         disabled_first_response, disabled_second_response,
         disabled_first_damage, disabled_second_damage);
  puts("ok: saber-priority");
}

static void armadillo_fixture_reset(const char *path, unsigned enemy,
                                    bool saber_extension) {
  check(RtlLoadSnapshot(path), "armadillo-fight.sav loads");
  check(MmxZeroActive() && !MmxZeroModern(),
        "Armadillo fixture runs as upstream X3 Zero");
  MmxZeroCancel(g_ram);
  MmxSaberFrameReset();
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  MmxZeroSetExtension(saber_extension ? &armadillo_priority_extension : NULL);
  check(g_ram[enemy] && g_ram[enemy + 0x0a] == 0x14 &&
            (g_ram[enemy + 0x27] & 127) == 0x20 && g_ram[enemy + 0x30] == 0,
        "Armadillo fixture has slot $0E68 kind $14, HP $20, and +$30=0");
}

static void armadillo_overlap_projectile(unsigned enemy, unsigned projectile) {
  write_ram_word(g_ram, projectile + 5, read_ram_word(g_ram, enemy + 5));
  write_ram_word(g_ram, projectile + 8, read_ram_word(g_ram, enemy + 8));
  g_ram[projectile + 0x11] =
      (uint8_t)((g_ram[projectile + 0x11] & (uint8_t)~0x40) |
                (g_ram[0x0c11] & 0x40));
}

static unsigned armadillo_live_slash(unsigned enemy, unsigned target_index) {
  MmxSaberPriorityClassification classification;
  unsigned projectile = priority_ground_attack_preserving_history(target_index);

  check(projectile != 0 &&
            MmxSaberPriorityClassify(g_ram, projectile, &classification) &&
            classification.priority_class ==
                (target_index ? MMX_SABER_PRIORITY_CLASS_SLASH2 :
                                 MMX_SABER_PRIORITY_CLASS_SLASH1) &&
            classification.priority == (target_index ? 3 : 2),
        target_index ? "Armadillo publishes a live slash-2 projectile" :
                       "Armadillo publishes a live slash-1 projectile");
  MmxSaberAttackCollisionRom(g_snes->cart->rom, g_snes->cart->romSize);
  MmxSaberAttackRuntimeTick(g_ram);
  if (!target_index) {
    for (unsigned tick = 0; tick < 12 &&
         read_ram_word(g_ram, projectile + 0x20) != 0xffe4; ++tick) {
      MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
      MmxSaberAttackRuntimeTick(g_ram);
    }
    check(read_ram_word(g_ram, projectile + 0x20) == 0xffe4,
          "Armadillo slash-1 reaches its real late active hitbox record");
  } else {
    check(read_ram_word(g_ram, projectile + 0x20) == 0xffe8,
          "Armadillo slash-2 reaches its real active hitbox record");
  }
  armadillo_overlap_projectile(enemy, projectile);
  check(g_ram[projectile] && g_ram[projectile + 0x0a] == 3 &&
            read_ram_word(g_ram, projectile + 0x20) != 0,
        target_index ? "Armadillo slash-2 remains a live Saber contact" :
                       "Armadillo slash-1 remains a live Saber contact");
  return projectile;
}

static unsigned armadillo_priority_frame_tick(void) {
  const MmxZeroState zero = MmxZeroGetState();
  MmxSaberPriorityObservePrePlayer(g_ram, zero.shot_mask, zero.burst);
  MmxSaberPriorityObservePlayerEnd(g_ram, zero.shot_mask, zero.burst);
  armadillo_priority_frame = MmxSaberPriorityCurrentFrame();
  return armadillo_priority_frame;
}

static bool armadillo_no_pending(unsigned enemy, unsigned projectile) {
  MmxSaberPriorityClassification ignored;
  return !MmxSaberPriorityConsumePending(g_ram, enemy, projectile, &ignored);
}

static void armadillo_idle_frames(unsigned enemy, unsigned projectile,
                                  unsigned count) {
  uint8_t saved_projectile[64];
  memcpy(saved_projectile, g_ram + projectile, sizeof(saved_projectile));
  while (count--) {
    armadillo_priority_frame_tick();
    memset(g_ram + projectile, 0, sizeof(saved_projectile));
    native_armadillo_contact(enemy, projectile);
    memcpy(g_ram + projectile, saved_projectile, sizeof(saved_projectile));
  }
}

static void saber_armadillo_checks(const char *x1_rom, const char *x3_rom,
                                   const char *assets,
                                   const char *fixture_dir) {
  const unsigned enemy = 0x0e68;
  const unsigned expected_slash1 = (unsigned)MmxSaberTuningBossDamage(
      MMX_SABER_TUNING_DAMAGE_SLASH1);
  const unsigned expected_slash2 = (unsigned)MmxSaberTuningBossDamage(
      MMX_SABER_TUNING_DAMAGE_SLASH2);
  char path[4096];
  MmxSaberPriorityClassification classification;
  MmxSaberPriorityHistory history;
  unsigned slash1;
  unsigned slash2;
  unsigned before;
  unsigned after_first;
  unsigned after_second;
  unsigned first_full;
  unsigned second_full;
  bool no_pending;
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         "armadillo-fight.sav");

  check(written >= 0 && written < (int)sizeof(path),
        "Armadillo HP2 fixture path fits");
  check(readable_file(path), "Armadillo HP2 fixture exists");
  check(expected_slash1 == 1 && expected_slash2 == 1,
        "Armadillo Saber default boss slash damage is 1 and 1");

  /* (a) A real slash-1 contact on the exposed row is native: it is not
   * admitted by the response seam and its HP drop seeds the history. */
  armadillo_fixture_reset(path, enemy, true);
  printf("reference: Armadillo initial HP=0x%02X +37=0x%02X +38=0x%02X "
         "+39=0x%02X +30=0x%02X +28=0x%02X\n",
         g_ram[enemy + 0x27], g_ram[enemy + 0x37], g_ram[enemy + 0x38],
         g_ram[enemy + 0x39], g_ram[enemy + 0x30], g_ram[enemy + 0x28]);
  slash1 = armadillo_live_slash(enemy, 0);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  before = g_ram[enemy + 0x27] & 127;
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  after_first = g_ram[enemy + 0x27] & 127;
  first_full = g_ram[enemy + 0x27];
  check(armadillo_priority_trace.response_calls == 1 &&
            armadillo_priority_trace.native_response == 0x11 &&
            armadillo_priority_trace.extension_response == 0x11 &&
            armadillo_priority_trace.response_row == 0x0b &&
            armadillo_priority_trace.response_timer == 0x00 &&
            armadillo_priority_trace.damage_calls == 1 &&
            armadillo_priority_trace.damage_input == 0x11 &&
            armadillo_priority_trace.damage_output == expected_slash1 &&
            after_first == before - expected_slash1 &&
            MmxSaberPriorityHistoryLookup(g_ram, enemy, &history) &&
            history.priority == 2,
        "Armadillo exposed slash-1 uses native $11 and records its HP drop");
  printf("reference: armadillo-a frame=%u native=0x%02X extension=0x%02X "
         "row=0x%02X timer=0x%02X damage=%u HP=0x%02X(%u)->0x%02X(%u) "
         "+38=0x%02X +39=0x%02X history_priority=%u\n",
         armadillo_priority_frame, armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.response_row,
         armadillo_priority_trace.response_timer,
         armadillo_priority_trace.damage_output, 0x20, before, first_full,
         after_first, g_ram[enemy + 0x38], g_ram[enemy + 0x39],
         history.priority);

  /* (b) Slash 2 is the accepted strict upgrade. The native callback sees
   * protected +$38=$3B and $11, while the token supplies tuned slash-2
   * damage and clears the restore gate before Armadillo reaches B35C. */
  slash2 = armadillo_live_slash(enemy, 1);
  before = g_ram[enemy + 0x27] & 127;
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash2);
  after_second = g_ram[enemy + 0x27] & 127;
  second_full = g_ram[enemy + 0x27];
  no_pending = armadillo_no_pending(enemy, slash2);
  check(armadillo_priority_trace.response_calls == 1 &&
            armadillo_priority_trace.native_response == 0x11 &&
            armadillo_priority_trace.extension_response == 0x01 &&
            armadillo_priority_trace.response_row == 0x0b &&
            armadillo_priority_trace.response_timer == 0x3b &&
            armadillo_priority_trace.damage_calls == 1 &&
            armadillo_priority_trace.damage_input == 0x11 &&
            armadillo_priority_trace.damage_output == expected_slash2 &&
            after_second == after_first - expected_slash2 && no_pending &&
            g_ram[enemy + 0x38] == 0x3c && g_ram[enemy + 0x39] == first_full,
        "Armadillo slash-2 bypass keeps tuned damage after native restore");
  printf("reference: armadillo-b frame=%u native=0x%02X extension=0x%02X "
         "row=0x%02X timer=0x%02X damage=%u HP=0x%02X(%u)->0x%02X(%u) "
         "+38=0x%02X +39=0x%02X pending=%u\n",
         armadillo_priority_frame, armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.response_row,
         armadillo_priority_trace.response_timer,
         armadillo_priority_trace.damage_output, first_full, before,
         second_full, after_second, g_ram[enemy + 0x38], g_ram[enemy + 0x39],
         !no_pending);
  armadillo_idle_frames(enemy, slash2, 3);
  check((g_ram[enemy + 0x27] & 127) == after_second,
        "Armadillo keeps the slash-2 HP drop for three later native frames");
  printf("reference: armadillo-b-later frames=3 HP=0x%02X(%u) +38=0x%02X "
         "+39=0x%02X\n", g_ram[enemy + 0x27],
         g_ram[enemy + 0x27] & 127, g_ram[enemy + 0x38], g_ram[enemy + 0x39]);

  /* (c) Equal and lower priority contacts remain native hits that the
   * protection restore undoes; neither can arm a token. */
  armadillo_fixture_reset(path, enemy, true);
  slash1 = armadillo_live_slash(enemy, 0);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  before = g_ram[enemy + 0x27] & 127;
  slash1 = armadillo_live_slash(enemy, 0);
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  after_second = g_ram[enemy + 0x27] & 127;
  no_pending = armadillo_no_pending(enemy, slash1);
  check(armadillo_priority_trace.native_response == 0x11 &&
            armadillo_priority_trace.extension_response == 0x11 &&
            armadillo_priority_trace.damage_output == expected_slash1 &&
            after_second == before && no_pending,
        "Armadillo equal-priority slash-1 follow-up is restored natively");
  printf("reference: armadillo-c-equal native=0x%02X extension=0x%02X "
         "damage=%u HP=%u->%u pending=%u timer=0x%02X\n",
         armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.damage_output, before, after_second,
         !no_pending, g_ram[enemy + 0x38]);

  armadillo_fixture_reset(path, enemy, true);
  slash2 = armadillo_live_slash(enemy, 1);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash2);
  before = g_ram[enemy + 0x27] & 127;
  slash1 = armadillo_live_slash(enemy, 0);
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  after_second = g_ram[enemy + 0x27] & 127;
  no_pending = armadillo_no_pending(enemy, slash1);
  check(armadillo_priority_trace.native_response == 0x11 &&
            armadillo_priority_trace.extension_response == 0x11 &&
            armadillo_priority_trace.damage_output == expected_slash1 &&
            after_second == before && no_pending,
        "Armadillo lower-priority slash-1 follow-up is restored natively");
  printf("reference: armadillo-c-lower native=0x%02X extension=0x%02X "
         "damage=%u HP=%u->%u pending=%u timer=0x%02X\n",
         armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.damage_output, before, after_second,
         !no_pending, g_ram[enemy + 0x38]);

  /* (d) The generated X1 table's class-3 row-00 control is a positive
   * armor response ($4A) in this real fixture, not the old unverified $80
   * premise. It still restores HP and must not arm a token. A separate
   * native bit-7 control below verifies the locked reflection rule. */
  armadillo_fixture_reset(path, enemy, true);
  slash1 = armadillo_live_slash(enemy, 0);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  slash2 = armadillo_live_slash(enemy, 1);
  before = g_ram[enemy + 0x27] & 127;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 1;
  g_ram[enemy + 0x38] = 0x3c;
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash2);
  after_second = g_ram[enemy + 0x27] & 127;
  no_pending = armadillo_no_pending(enemy, slash2);
  check(armadillo_priority_trace.response_calls == 1 &&
            armadillo_priority_trace.response_row == 0x00 &&
            armadillo_priority_trace.native_response == 0x4a &&
            armadillo_priority_trace.extension_response == 0x4a &&
            armadillo_priority_trace.damage_calls == 1 && no_pending &&
            after_second == before,
        "Armadillo armor row $00 remains restoring with no token");
  printf("reference: armadillo-d row=0x%02X native=0x%02X extension=0x%02X "
         "damage_calls=%u damage=%u HP=%u->%u pending=%u +38=0x%02X\n",
         armadillo_priority_trace.response_row,
         armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.damage_calls,
         armadillo_priority_trace.damage_output, before, after_second,
         !no_pending, g_ram[enemy + 0x38]);

  check(MmxSaberPriorityResponse(g_ram, enemy, slash2, 0x80) == 0x80 &&
            armadillo_no_pending(enemy, slash2),
        "Armadillo bit-7 guard/reflection response remains blocking");
  printf("reference: armadillo-d-bit7 native=0x80 extension=0x80 "
         "pending=0\n");

  armadillo_fixture_reset(path, enemy, true);
  slash1 = armadillo_live_slash(enemy, 0);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash1);
  slash2 = armadillo_live_slash(enemy, 1);
  before = g_ram[enemy + 0x27] & 127;
  g_ram[enemy + 0x37] = 1;
  g_ram[enemy + 0x38] = 0x3c;
  g_ram[slash2 + 0x0a] = 4;
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash2);
  after_second = g_ram[enemy + 0x27] & 127;
  no_pending = armadillo_no_pending(enemy, slash2);
  check(armadillo_priority_trace.response_row == 0x00 &&
            armadillo_priority_trace.native_response == 0xaa &&
            armadillo_priority_trace.extension_response == 0xaa &&
            armadillo_priority_trace.damage_calls == 0 && no_pending &&
            after_second == before,
        "Armadillo generated bit-7 armor response remains blocking");
  printf("reference: armadillo-d-bit7-native row=0x%02X class=0x%02X "
         "native=0x%02X extension=0x%02X damage_calls=%u HP=%u->%u "
         "pending=%u +38=0x%02X\n",
         armadillo_priority_trace.response_row,
         armadillo_priority_trace.response_projectile_class,
         armadillo_priority_trace.native_response,
         armadillo_priority_trace.extension_response,
         armadillo_priority_trace.damage_calls, before, after_second,
         !no_pending, g_ram[enemy + 0x38]);

  /* (e) The native +$30 hard skip prevents the response seam from running. */
  armadillo_fixture_reset(path, enemy, true);
  slash2 = armadillo_live_slash(enemy, 1);
  before = g_ram[enemy + 0x27] & 127;
  g_ram[enemy + 0x30] = 1;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0x3c;
  memset(&armadillo_priority_trace, 0, sizeof(armadillo_priority_trace));
  armadillo_priority_frame_tick();
  native_armadillo_contact(enemy, slash2);
  after_second = g_ram[enemy + 0x27] & 127;
  no_pending = armadillo_no_pending(enemy, slash2);
  check(armadillo_priority_trace.response_calls == 0 &&
            armadillo_priority_trace.damage_calls == 0 && no_pending &&
            after_second == before,
        "Armadillo +$30 hard skip blocks the bypass");
  printf("reference: armadillo-e +30=0x%02X response_calls=%u damage_calls=%u "
         "HP=%u->%u pending=%u +38=0x%02X\n", g_ram[enemy + 0x30],
         armadillo_priority_trace.response_calls,
         armadillo_priority_trace.damage_calls, before, after_second,
         !no_pending, g_ram[enemy + 0x38]);

  /* (f) Without the Saber package, the same live two-contact sequence is
   * swallowed by native Armadillo restore logic. */
  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(!MmxSaberEnabled(), "Armadillo disabled control turns Saber off");
  armadillo_fixture_reset(path, enemy, false);
  slash1 = armadillo_live_slash(enemy, 0);
  g_ram[enemy + 0x27] = 0x20;
  g_ram[enemy + 0x30] = 0;
  g_ram[enemy + 0x37] = 0;
  g_ram[enemy + 0x38] = 0;
  armadillo_priority_frame = 1;
  native_armadillo_contact(enemy, slash1);
  before = g_ram[enemy + 0x27] & 127;
  slash2 = armadillo_live_slash(enemy, 1);
  armadillo_priority_frame = 2;
  native_armadillo_contact(enemy, slash2);
  after_second = g_ram[enemy + 0x27] & 127;
  check(before < 0x20 && after_second == before,
        "Saber-disabled Armadillo follow-up is swallowed by native restore");
  printf("reference: armadillo-f disabled native HP=0x%02X(%u)->0x%02X(%u) "
         "+38=0x%02X +39=0x%02X\n", 0x20, 0x20, g_ram[enemy + 0x27],
         after_second, g_ram[enemy + 0x38], g_ram[enemy + 0x39]);

  activate_zero(x1_rom, x3_rom, assets, true, true);
  MmxZeroSetExtension(MmxSaberFrameExtension());
  MmxSaberFrameReset();
  puts("ok: saber-armadillo");
}

static unsigned renderer_world_sprite_count(void) {
  MmxRenderWorldSprite snapshot[8];
  return MmxRendererWorldSpriteSnapshot(snapshot,
                                        sizeof(snapshot) / sizeof(snapshot[0]));
}

static void saber_wave_render_checks(const char *x1_rom, const char *x3_rom,
                                     const char *fixture, const char *assets) {
  unsigned slot = saber_wave_launch(fixture, false);
  bool live_snapshots = true;

  /* The provider is sampled once before each draw frame. A reservation is not
   * live, while the published slot remains one world sprite until retirement. */
  for (unsigned i = 0; i < 12 && MmxSaberWaveRuntimeOwns(g_ram, slot); ++i) {
    MmxRendererBeginFrame(g_ram);
    live_snapshots &= renderer_world_sprite_count() == 1;
    saber_wave_keep_onscreen(slot);
    frame(0);
  }
  check(live_snapshots,
        "BeginFrame snapshots one world sprite for every live wave frame");

  while (MmxSaberWaveRuntimeOwns(g_ram, slot)) {
    MmxRendererBeginFrame(g_ram);
    live_snapshots &= renderer_world_sprite_count() == 1;
    saber_wave_keep_onscreen(slot);
    frame(0);
  }
  MmxRendererBeginFrame(g_ram);
  check(live_snapshots && renderer_world_sprite_count() == 0,
        "the world snapshot becomes empty immediately after wave retirement");

  slot = saber_wave_launch(fixture, false);
  while (MmxZeroGetState().slash) {
    saber_wave_keep_onscreen(slot);
    frame(0);
  }
  frame(SNES_PAD_SELECT);
  check(MmxZeroSwapping(), "wave-render exchange probe starts the X exchange");
  bool exchange_snapshots = true;
  bool reached_x = !MmxZeroActive();
  for (unsigned i = 0; i < 80 && MmxZeroSwapping(); ++i) {
    MmxRendererBeginFrame(g_ram);
    exchange_snapshots &= renderer_world_sprite_count() == 1;
    reached_x |= !MmxZeroActive();
    saber_wave_keep_onscreen(slot);
    frame(0);
  }
  MmxRendererBeginFrame(g_ram);
  check(reached_x && !MmxZeroActive() && exchange_snapshots &&
            MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            renderer_world_sprite_count() == 1,
        "the live world wave remains visible during exchange to X");

  /* Disable through the real plugin path: the provider is cleared before the
   * runtime reset and sidecar release, so its prior snapshot is gone now. */
  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(renderer_world_sprite_count() == 0,
        "disabling Saber clears the world-sprite snapshot immediately");
  MmxRendererBeginFrame(g_ram);
  check(renderer_world_sprite_count() == 0,
        "a disabled Saber provider yields no world sprites on the next frame");
  puts("ok: saber-wave-render");
}

static bool saber_wave_record_matches_asset(void) {
  const char *cache = getenv("MMX_SABER_TEST_CACHE");
  char path[4096];
  char reason[128] = {0};
  MmxSaberWave *wave;
  bool matches;
  if (!cache || !cache[0] ||
      snprintf(path, sizeof(path), "%s/mmx-source/x3-saber-wave-v1.bin",
               cache) >= (int)sizeof(path)) return false;
  wave = MmxSaberWaveLoadFile(path, reason, sizeof(reason));
  if (!wave) return false;
  matches = MmxSaberWaveCollisionSize(wave) ==
          MMX_SABER_WAVE_COLLISION_RECORD_BYTES &&
      !memcmp(g_snes->cart->rom + MMX_SABER_WAVE_COLLISION_ROM_OFFSET,
              MmxSaberWaveCollisionRecord(wave),
              MMX_SABER_WAVE_COLLISION_RECORD_BYTES);
  MmxSaberWaveFree(wave);
  return matches;
}

static void saber_wave_travel_checks(const char *x1_rom, const char *x3_rom,
                                     const char *fixture, const char *assets) {
  unsigned slot;

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(0);
  check(saber_wave_record_matches_asset(),
        "Saber installs the validated old four-byte wave record at $37FA0");

  slot = saber_wave_launch(fixture, false);
  const int launch_x = (int16_t)read_ram_word(g_ram, slot + 5);
  const unsigned launch_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  const bool had_birth = g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH] != 0;
  if (had_birth) {
    saber_wave_keep_onscreen(slot);
    check(MmxZeroWeaponTick(g_ram, slot, 1) == 0 && g_ram[slot] &&
              (int16_t)read_ram_word(g_ram, slot + 5) == launch_x &&
              g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] == launch_age &&
              !g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH],
          "birth guard consumes the publication tick without movement");
  } else {
    check(launch_age == 0, "publication frame has no active travel age");
  }
  bool right_trajectory = true;
  for (unsigned i = 0; i < 3; ++i) {
    const int before_x = (int16_t)read_ram_word(g_ram, slot + 5);
    const unsigned before_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
    saber_wave_keep_onscreen(slot);
    MmxZeroWeaponTick(g_ram, slot, 1);
    right_trajectory &= g_ram[slot] &&
        (int16_t)read_ram_word(g_ram, slot + 5) == before_x + 8 &&
        g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] == before_age + 1;
  }
  check(right_trajectory, "right-facing wave advances exactly 8 px per active tick");
  const unsigned enemy = empty_enemy_slot();
  check(MmxZeroHitbox(g_ram, enemy, slot, 1) == 1 &&
            MmxZeroDamage(g_ram, enemy, slot, 0) == 0 &&
            g_ram[slot + MMX_SABER_WAVE_SLOT_STATE] ==
                MMX_SABER_WAVE_SLOT_STATE_TRAVEL,
        "wave travel keeps its hitbox and native immunity leaves it travelling");

  slot = saber_wave_launch(fixture, true);
  bool left_trajectory = true;
  if (g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH]) {
    const int before_x = (int16_t)read_ram_word(g_ram, slot + 5);
    saber_wave_keep_onscreen(slot);
    MmxZeroWeaponTick(g_ram, slot, 1);
    left_trajectory &= (int16_t)read_ram_word(g_ram, slot + 5) == before_x &&
        !g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH];
  }
  for (unsigned i = 0; i < 3; ++i) {
    const int before_x = (int16_t)read_ram_word(g_ram, slot + 5);
    const unsigned before_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
    saber_wave_keep_onscreen(slot);
    MmxZeroWeaponTick(g_ram, slot, 1);
    left_trajectory &= g_ram[slot] &&
        (int16_t)read_ram_word(g_ram, slot + 5) == before_x - 8 &&
        g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] == before_age + 1;
  }
  check(left_trajectory, "left-facing wave advances exactly 8 px per active tick");

  slot = saber_wave_launch(fixture, false);
  unsigned lifetime_ticks = 0;
  while (g_ram[slot] && lifetime_ticks < MMX_SABER_WAVE_LIFETIME) {
    const bool birth = g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH] != 0;
    saber_wave_keep_onscreen(slot);
    MmxZeroWeaponTick(g_ram, slot, 1);
    if (!birth) ++lifetime_ticks;
  }
  check(lifetime_ticks == MMX_SABER_WAVE_LIFETIME && !g_ram[slot],
        "onscreen wave retires on active tick 96");

  slot = saber_wave_launch(fixture, false);
  g_ram[0x1f7a] ^= 1;
  MmxSaberWaveRuntimeObserveStage(g_ram);
  check(!g_ram[slot], "stage change retires the wave without a weapon tick");

  slot = saber_wave_launch(fixture, false);
  unsigned natural_ticks = 0;
  while (g_ram[slot] && natural_ticks <= MMX_SABER_WAVE_LIFETIME) {
    const bool birth = g_ram[slot + MMX_SABER_WAVE_SLOT_BIRTH] != 0;
    MmxZeroWeaponTick(g_ram, slot, 1);
    if (!birth) ++natural_ticks;
  }
  check(!g_ram[slot] && natural_ticks <= MMX_SABER_WAVE_LIFETIME,
        "save0 wave retires by the 96-tick/offscreen rule");
  printf("reference: save0 wave retirement=%s active_ticks=%u\n",
         natural_ticks == MMX_SABER_WAVE_LIFETIME ? "lifetime" : "offscreen",
         natural_ticks);

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(0);
  unsigned first = 0, second = 0;
  check(MmxSaberWaveRuntimeReserve(g_ram, &first) &&
            MmxSaberWaveRuntimeReserve(g_ram, &second) && first != second,
        "two wave reservations allocate distinct generations");
  const unsigned first_tag = read_ram_word(g_ram, first + 0x3e);
  const unsigned second_tag = read_ram_word(g_ram, second + 0x3e);
  check(first_tag != second_tag &&
            MmxSaberWaveRuntimePublish(g_ram, first) &&
            MmxSaberWaveRuntimePublish(g_ram, second) &&
            saber_wave_live_count() == 2,
        "two published generations coexist");
  g_ram[first + MMX_SABER_WAVE_SLOT_BIRTH] = 0;
  g_ram[second + MMX_SABER_WAVE_SLOT_BIRTH] = 0;
  g_ram[first + MMX_SABER_WAVE_SLOT_AGE] = MMX_SABER_WAVE_LIFETIME - 1;
  saber_wave_keep_onscreen(first);
  saber_wave_keep_onscreen(second);
  MmxZeroWeaponTick(g_ram, first, 1);
  check(!g_ram[first] && g_ram[second] &&
            read_ram_word(g_ram, second + 0x3e) == second_tag,
        "retiring the first generation leaves the second wave untouched");

  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(saber_wave_record_empty(g_snes->cart->rom +
                                    MMX_SABER_WAVE_COLLISION_ROM_OFFSET),
        "disabling Saber restores the wave collision record to $FF");
  puts("ok: saber-wave-travel");
}

static void saber_wave_lifecycle_checks(const char *x1_rom, const char *x3_rom,
                                        const char *fixture,
                                        const char *assets) {
  unsigned slot;

  /* D4a: a hurt is a pending-finisher cancellation, not a launched-wave
   * lifecycle boundary. Keep the wave near the camera while native hurt runs
   * so movement is observable without depending on an enemy. */
  slot = saber_wave_launch(fixture, false);
  const unsigned hurt_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  const int hurt_x = (int16_t)read_ram_word(g_ram, slot + 5);
  bool hurt_moved = false;
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  for (unsigned i = 0; i < 24 && MmxSaberWaveRuntimeOwns(g_ram, slot); ++i) {
    saber_wave_keep_onscreen(slot);
    frame(0);
    if (g_ram[slot] &&
        (g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] > hurt_age ||
         (int16_t)read_ram_word(g_ram, slot + 5) != hurt_x))
      hurt_moved = true;
  }
  check(MmxSaberWaveRuntimeOwns(g_ram, slot) && hurt_moved,
        "hurt preserves a launched wave and the wave keeps moving");

  /* The same injected hurt, before Y, must revoke only the unlaunched
   * reservation/window. */
  saber_finisher_window_setup(fixture);
  check(MmxSaberComboWindowTicks() != 0 && !MmxSaberComboReservedSlot(),
        "hurt-window probe starts with only an unlaunched finisher window");
  g_ram[0xbaa] = 0x0e;
  g_ram[0xbab] = 0;
  frame(0);
  check(!MmxSaberComboWindowTicks() && !MmxSaberComboReservedSlot() &&
            !saber_finisher_wave_slot(),
        "hurt before Y closes the finisher window without a reservation");

  /* D4b: this ROM harness has no direct paused scheduler pump. Its approved
   * pause model is to stop calling frames; no wave callback can tick there. */
  printf("reference: pause method=no frame ticks in the ROM harness\n");
  slot = saber_wave_launch(fixture, false);
  const unsigned paused_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  const int paused_x = (int16_t)read_ram_word(g_ram, slot + 5);
  for (unsigned i = 0; i < 30; ++i)
    if (!MmxSaberWaveRuntimeOwns(g_ram, slot) ||
        g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] != paused_age ||
        (int16_t)read_ram_word(g_ram, slot + 5) != paused_x)
      break;
  check(MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] == paused_age &&
            (int16_t)read_ram_word(g_ram, slot + 5) == paused_x,
        "no scheduler ticks freeze wave age and position during pause");
  bool resumed = false;
  for (unsigned i = 0; i < 4 && MmxSaberWaveRuntimeOwns(g_ram, slot); ++i) {
    saber_wave_keep_onscreen(slot);
    frame(0);
    resumed |= g_ram[slot] &&
        (g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] > paused_age ||
         (int16_t)read_ram_word(g_ram, slot + 5) != paused_x);
  }
  check(MmxSaberWaveRuntimeOwns(g_ram, slot) && resumed,
        "wave travel resumes after the paused no-tick interval");

  /* D4c: exchange clears the pending finisher context, but the world-owned
   * wave remains active after X becomes the playable character. */
  slot = saber_wave_launch(fixture, false);
  for (unsigned i = 0; i < 60 && MmxZeroGetState().slash; ++i) {
    saber_wave_keep_onscreen(slot);
    frame(0);
  }
  check(!MmxZeroGetState().slash && MmxSaberWaveRuntimeOwns(g_ram, slot),
        "launched wave survives finisher recovery before exchange");
  frame(SNES_PAD_SELECT);
  check(MmxZeroSwapping(), "Select starts the native X/Zero exchange");
  const unsigned exchange_age = g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  const int exchange_x = (int16_t)read_ram_word(g_ram, slot + 5);
  bool exchange_moved = false;
  bool reached_x = !MmxZeroActive();
  for (unsigned i = 0; i < 80 && MmxZeroSwapping(); ++i) {
    saber_wave_keep_onscreen(slot);
    frame(0);
    if (g_ram[slot] &&
        (g_ram[slot + MMX_SABER_WAVE_SLOT_AGE] > exchange_age ||
         (int16_t)read_ram_word(g_ram, slot + 5) != exchange_x))
      exchange_moved = true;
    reached_x |= !MmxZeroActive();
  }
  check(reached_x && !MmxZeroActive() && MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            exchange_moved,
        "exchange to X preserves a launched wave and X frames move it");

  /* D4d: both locked death indicators use the same player-end lifecycle
   * branch; HP zero is set explicitly alongside the native death action. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  g_ram[0xbcf] = 0;
  g_ram[0xbaa] = 0x0c;
  g_ram[0xbab] = 0;
  frame(0);
  check(saber_wave_finisher_empty(),
        "HP zero/death retires every launched wave and pending finisher state");

  /* D4e/D2: call the exact respawn seam used by hook $00:9DCA while a wave is
   * still present. This isolates the required upstream extension reset from
   * the already-tested death branch. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  g_ram[0xbcf] = 0;
  MmxZeroHealthRespawn(g_ram);
  check(saber_wave_finisher_empty(),
        "respawn calls the extension reset and removes wave/reservation state");

  /* D4f: exercise the stage observer directly, as allowed for a fixture whose
   * stage does not change during this short route. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  g_ram[0x1f7a] ^= 1;
  MmxSaberWaveRuntimeObserveStage(g_ram);
  check(saber_wave_finisher_empty(),
        "stage-id change retires the wave immediately without a weapon tick");

  saber_finisher_window_setup(fixture);
  g_ram[0x1f7a] ^= 1;
  frame(0);
  check(!MmxSaberComboWindowTicks() && !MmxSaberComboReservedSlot(),
        "stage-id change also cancels an unlaunched finisher window");

  /* D4g reset: the Saber-owned reset callback clears the world slot and the
   * wave collision record while preserving no pending combo state. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  MmxSaberFrameReset();
  check(saber_wave_finisher_empty() &&
            saber_wave_record_empty(g_snes->cart->rom +
                                    MMX_SABER_WAVE_COLLISION_ROM_OFFSET),
        "Saber reset retires waves, reservations, window, and $37FA0 ownership");

  /* Save-state load: save a live guest wave, advance away from it, then load
   * the same snapshot. state_reset must erase the restored non-serialized
   * Saber tail, including the guest wave slot. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  const size_t snapshot_capacity = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *snapshot = malloc(snapshot_capacity);
  check(snapshot_capacity != 0 && snapshot != NULL,
        "wave lifecycle allocates a save-state snapshot buffer");
  const size_t snapshot_size = RtlSaveSnapshotToMemory(snapshot,
                                                        snapshot_capacity);
  check(snapshot_size != 0 && MmxSaberWaveRuntimeOwns(g_ram, slot),
        "wave lifecycle saves a mid-flight wave snapshot");
  saber_wave_keep_onscreen(slot);
  frame(0);
  check(RtlLoadSnapshotFromMemory(snapshot, snapshot_size),
        "mid-flight wave save-state loads through the existing state path");
  check(saber_wave_finisher_empty(),
        "save-state load state_reset removes the restored wave and reservation");
  free(snapshot);

  /* The rewind ring uses the same RtlLoadSnapshotFromMemory path. Configure a
   * small private ring so this remains a real rewind, not a direct reset. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  check(set_test_env("SNESRECOMP_REWIND", "1") == 0,
        "wave lifecycle enables the rewind harness path");
  snes_rewind_shutdown();
  snes_rewind_set_defaults(1, 4, 1);
  snes_rewind_configure();
  for (unsigned i = 0; i < 5; ++i) {
    saber_wave_keep_onscreen(slot);
    frame(0);
    snes_rewind_note_frame();
  }
  check(snes_rewind_open(), "rewind opens with several live-wave snapshots");
  snes_rewind_step(-1);
  snes_rewind_commit();
  check(saber_wave_finisher_empty(),
        "rewind load invokes state_reset and removes the live wave");
  snes_rewind_shutdown();

  /* Plugin disable/reset must retire the guest slots before sidecars and
   * providers are released, and must restore the owned collision bytes. */
  load_fixture(fixture);
  slot = saber_wave_launch(fixture, false);
  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(!MmxSaberEnabled() && saber_wave_finisher_empty() &&
            saber_wave_record_empty(g_snes->cart->rom +
                                    MMX_SABER_WAVE_COLLISION_ROM_OFFSET),
        "plugin disable retires waves and restores $37FA0 to $FF");
  activate_zero(x1_rom, x3_rom, assets, true, true);

  puts("ok: saber-wave-lifecycle");
}

static void saber_finisher_window_at_current_position(void) {
  bool opened = false;

  release_charge_button(150, SNES_PAD_X);
  check(MmxZeroGetState().combo == 1,
        "wave damage setup stores the capped tier-8 first burst");
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask ||
        g_ram[0xc25]); ++i)
    frame(0);
  frame(SNES_PAD_X);
  check(MmxZeroGetState().burst == 2,
        "wave damage setup starts the X3 second burst");
  if (MmxSaberComboWindowTicks()) opened = true;
  for (unsigned i = 0; i < 120 && !opened; ++i) {
    frame(0);
    opened = MmxSaberComboWindowTicks() != 0;
  }
  check(opened, "wave damage setup opens the second-shot finisher window");
}

static void retire_non_wave_projectiles(unsigned wave_slot) {
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    if (d == wave_slot || !g_ram[d]) continue;
    memset(g_ram + d, 0, 64);
    if (g_ram[0xbdd]) --g_ram[0xbdd];
  }
}

static unsigned apply_test_damage(unsigned enemy, unsigned damage) {
  unsigned hp = g_ram[enemy + 0x27] & 127;
  unsigned drop = hp < damage ? hp : damage;
  g_ram[enemy + 0x27] = (uint8_t)(hp > damage ? hp - damage : 0);
  return drop;
}

static void saber_wave_damage_checks(const char *fixture) {
  unsigned target, walk_frames = 0;
  unsigned slot, pulse_count = 0, retire_frame = 0;
  unsigned pulse_frames[MMX_SABER_WAVE_MAX_PULSES] = {0};
  unsigned pulse_drops[MMX_SABER_WAVE_MAX_PULSES] = {0};
  unsigned hp_before_wave;
  uint8_t target_record[64];

  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target == 0xea8,
        "wave damage fixture reaches the documented Highway enemy slot 0xEA8");
  printf("reference: wave damage walk_frames=%u Zero=(%u,%u) enemy_slot=0x%X\n",
         walk_frames, read_ram_word(g_ram, 0x0bad), read_ram_word(g_ram, 0x0bb0),
         target);

  /* save0's probe enemy begins applying native hurt for a close player. Keep
   * the documented walked target and move one wave-length back before the
   * two charged shots, so the shot-shot-Y sequence can complete. */
  unsigned target_x = read_ram_word(g_ram, target + 5);
  write_ram_word(g_ram, 0x0bad, target_x - 80);
  g_ram[0xbc2] = g_ram[0xbc3] = 0;
  g_ram[0xc04] = g_ram[0xc05] = 0;
  g_ram[0xbfa] = 0;

  /* The route uses R after frame 180 to keep the native enemy probe stable;
   * restore the fixture's buster selection before the X3 shot-shot-Y route. */
  g_ram[0xbdb] = 0;
  g_ram[0xc0f] = 3;
  g_ram[0x1f12] = 0;
  g_ram[0xbaa] = 0;
  g_ram[0xbab] = 0;
  MmxZeroCancel(g_ram);
  MmxSaberFrameReset();
  memcpy(target_record, g_ram + target, sizeof(target_record));
  memset(g_ram + target, 0, sizeof(target_record));
  saber_finisher_window_at_current_position();
  memcpy(g_ram + target, target_record, sizeof(target_record));
  g_ram[0x0c11] = 0x40;
  g_ram[0x0bb9] = (g_ram[0x0bb9] & (uint8_t)~0x40) | 0x40;
  frame(SNES_PAD_Y);
  slot = saber_finisher_wave_slot();
  check(slot != 0 && MmxZeroGetState().slash == 1,
        "shot-shot-Y accepts the finisher request at the reachable enemy");
  for (unsigned age = 2; age <= 7; ++age) frame(0);
  check(MmxSaberWaveRuntimeOwns(g_ram, slot),
        "shot-shot-Y publishes a live wave before the damage probe");
  /* Clear the native buster/slash objects after the real impact setup. The
   * fixture target starts at 16 HP, so give this measurement probe enough
   * HP to observe all three native six-point results without a death branch. */
  MmxZeroCancel(g_ram);
  retire_non_wave_projectiles(slot);
  g_ram[target + 0x27] = 64;
  hp_before_wave = g_ram[target + 0x27] & 127;
  for (unsigned frame_number = 1; frame_number <= 48 && g_ram[slot];
       ++frame_number) {
    unsigned hp_before = g_ram[target + 0x27] & 127;
    frame(0);
    unsigned hp_after = g_ram[target + 0x27] & 127;
    if (hp_before > hp_after && pulse_count < MMX_SABER_WAVE_MAX_PULSES) {
      pulse_frames[pulse_count] = frame_number;
      pulse_drops[pulse_count] = hp_before - hp_after;
      ++pulse_count;
    }
    if (g_ram[slot] &&
        g_ram[slot + MMX_SABER_WAVE_SLOT_STATE] ==
            MMX_SABER_WAVE_SLOT_STATE_CUTTING &&
        read_ram_word(g_ram, slot + 0x20) ==
            MMX_SABER_WAVE_COLLISION_POINTER) {
      unsigned hitbox = MmxZeroHitbox(g_ram, target, slot, 1);
      unsigned damage = hitbox ? MmxZeroDamage(g_ram, target, slot, 1) : 0;
      if (damage && pulse_count < MMX_SABER_WAVE_MAX_PULSES) {
        pulse_frames[pulse_count] = frame_number;
        pulse_drops[pulse_count] = apply_test_damage(target, damage);
        ++pulse_count;
      }
    }
    if (!g_ram[slot]) retire_frame = frame_number;
  }
  printf("reference: wave HP before=%u drops=%u,%u,%u frames=%u,%u,%u retire_frame=%u final_hp=%u\n",
         hp_before_wave, pulse_drops[0], pulse_drops[1], pulse_drops[2],
         pulse_frames[0], pulse_frames[1], pulse_frames[2], retire_frame,
         g_ram[target + 0x27] & 127);
  check(pulse_count == MMX_SABER_WAVE_MAX_PULSES &&
            pulse_drops[0] ==
                (unsigned)MmxSaberTuningNormalDamage(
                    MMX_SABER_TUNING_DAMAGE_WAVE) &&
            pulse_drops[1] ==
                (unsigned)MmxSaberTuningNormalDamage(
                    MMX_SABER_TUNING_DAMAGE_WAVE) &&
            pulse_drops[2] ==
                (unsigned)MmxSaberTuningNormalDamage(
                    MMX_SABER_TUNING_DAMAGE_WAVE) &&
            pulse_frames[1] - pulse_frames[0] == MMX_SABER_WAVE_PULSE_FRAMES + 1 &&
            pulse_frames[2] - pulse_frames[1] == MMX_SABER_WAVE_PULSE_FRAMES &&
            retire_frame == pulse_frames[2] && !g_ram[slot],
        "wave deals three tuned pulses at four-tick gaps and retires after pulse 3");

  /* The direct callback check is the stable proof for the blade even when
   * this fixture's moving enemy geometry does not overlap $5A53. */
  saber_finisher_window_setup(fixture);
  frame(SNES_PAD_Y);
  unsigned slash_slot = saber_finisher_slot_with_tag(0x5a53);
  unsigned empty = empty_enemy_slot();
  unsigned finisher_first = MmxZeroDamage(g_ram, empty, slash_slot, 3);
  unsigned finisher_second = MmxZeroDamage(g_ram, empty, slash_slot, 3);
  printf("reference: finisher callback tag=0x5A53 damage=%u then=%u\n",
         finisher_first, finisher_second);
  check(slash_slot && finisher_first ==
            (unsigned)MmxSaberTuningNormalDamage(
                MMX_SABER_TUNING_DAMAGE_X3_FINISHER) &&
            finisher_second == 0,
        "the Saber $5A53 finisher blade deals its tuned damage exactly once per enemy");

  slot = saber_wave_launch(fixture, false);
  empty = empty_enemy_slot();
  check(MmxZeroDamage(g_ram, empty, slot, 0) == 0 && g_ram[slot] &&
            g_ram[slot + MMX_SABER_WAVE_SLOT_STATE] ==
                MMX_SABER_WAVE_SLOT_STATE_TRAVEL,
        "native immunity returns zero and leaves the wave travelling");
  check(MmxZeroDamage(g_ram, empty, slot, 128) == 128 && !g_ram[slot],
        "reflection returns its special response and retires the wave");

  /* D-OP-47: only the upstream finisher animation gates fire. The published
   * wave remains a lifecycle object, not an input-gate condition. */
  slot = saber_wave_launch(fixture, false);
  unsigned char previous[8] = {0};
  const unsigned blocked_launch_slash = MmxZeroGetState().slash;
  const unsigned blocked_launch_age =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  shot_presence(0, previous);
  MmxSaberPadSaber pad = MmxSaberAttackPadState(false);
  check(blocked_launch_slash != 0 && blocked_launch_age == 0 &&
            MmxSaberWaveRuntimeOwns(g_ram, slot) && pad.finisher_active,
        "the finisher gate sees slash active while a published wave is live");
  const bool buster_finisher_active = MmxZeroGetState().slash != 0;
  frame(SNES_PAD_X);
  unsigned buster_births = new_projectiles(0, previous);
  check(buster_finisher_active && buster_births == 0,
        "a buster X tap fires nothing during the finisher animation");
  frame(0);
  g_ram[0xbdb] = 4;
  g_ram[0x1f89] = 0;
  g_ram[0x1f8a] = 0xdc;
  memset(previous, 0, sizeof(previous));
  shot_presence(8, previous);
  const bool special_finisher_active = MmxZeroGetState().slash != 0;
  frame(SNES_PAD_X);
  unsigned special_births = new_projectiles(8, previous);
  check(special_finisher_active && special_births == 0,
        "a Fire Wave X tap fires nothing during the finisher animation");
  g_ram[0xbdb] = 0;
  for (unsigned i = 0; i < 160 && MmxSaberWaveRuntimeActive(g_ram); ++i)
    frame(0);
  check(!MmxSaberWaveRuntimeActive(g_ram),
        "blocked fire inputs leave the launched wave's lifecycle independent");

  slot = saber_wave_launch(fixture, false);
  const unsigned launch_slash = MmxZeroGetState().slash;
  unsigned finisher_frames = 0;
  while (MmxZeroGetState().slash) {
    frame(0);
    ++finisher_frames;
  }
  const unsigned wave_age_after_finisher =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  printf("reference: D-OP-47 launch_slash=%u finisher_frames=%u "
         "total_finisher_frames=%u wave_age_at_idle=%u wave_state=%u\n",
         launch_slash, finisher_frames, launch_slash - 1 + finisher_frames,
         wave_age_after_finisher,
         g_ram[slot + MMX_SABER_WAVE_SLOT_STATE]);
  check(launch_slash == 7 && !MmxZeroGetState().slash &&
            MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            g_ram[slot + MMX_SABER_WAVE_SLOT_STATE] ==
                MMX_SABER_WAVE_SLOT_STATE_TRAVEL,
        "the wave slot is still travelling after the 44-frame finisher ends");

  retire_non_wave_projectiles(slot);
  memset(previous, 0, sizeof(previous));
  shot_presence(0, previous);
  const unsigned wave_age_before_plain =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  frame(SNES_PAD_X);
  unsigned post_finisher_buster_births = new_projectiles(0, previous);
  const unsigned wave_age_after_plain =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  check(post_finisher_buster_births == 1 &&
            MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            wave_age_after_plain > wave_age_before_plain,
        "a plain X tap fires one shot while the finisher-ended wave flies");

  frame(0);
  g_ram[0xbdb] = 4;
  g_ram[0x1f89] = 0;
  g_ram[0x1f8a] = 0xdc;
  memset(previous, 0, sizeof(previous));
  shot_presence(8, previous);
  const unsigned wave_age_before_special =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  frame(SNES_PAD_X);
  unsigned post_finisher_special_births = new_projectiles(8, previous);
  const unsigned wave_age_after_special =
      g_ram[slot + MMX_SABER_WAVE_SLOT_AGE];
  check(post_finisher_special_births > 0 &&
            MmxSaberWaveRuntimeOwns(g_ram, slot) &&
            wave_age_after_special > wave_age_before_special,
        "a Fire Wave tap fires while the finisher-ended wave keeps flying");
  g_ram[0xbdb] = 0;
  for (unsigned i = 0; i < 160 && MmxSaberWaveRuntimeActive(g_ram); ++i)
    frame(0);
  check(!MmxSaberWaveRuntimeActive(g_ram),
        "the D-OP-47 probe lets the wave retire independently of fire gating");
  while (MmxZeroGetState().slash) frame(0);
  saber_plain_after_probe("a plain X tap fires after wave retirement");
  puts("ok: saber-wave-damage");
}

static void saber_damage_set_option(const char *option_id,
                                    const char *value) {
  check(g_mod_provider->feature_set_option(
            g_mod_provider->ctx, "megaman-x.character.saber-zero",
            "saber-zero", option_id, value),
        "saber-damage catalog option override is accepted");
  printf("reference: saber-damage option %s=%s\n", option_id, value);
}

static void saber_damage_reactivate(void) {
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled(), "saber-damage option reactivation keeps Saber enabled");
}

static unsigned saber_damage_start_ground1(void) {
  MmxSaberAttackSnapshot snapshot;
  MmxSaberAttackResetRam(g_ram);
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.kind == SABER_KIND_GROUND1 &&
            snapshot.phase == SABER_PHASE_ACTIVE,
        "saber-damage starts a ground slash 1 damage probe");
  return saber_active_slot();
}

static unsigned saber_damage_start_ground3(void) {
  const MmxSaberAttack *slash1 =
      MmxSaberAttackRecord(SABER_KIND_GROUND1, 0);
  const MmxSaberAttack *slash2 =
      MmxSaberAttackRecord(SABER_KIND_GROUND2, 1);
  MmxSaberAttackSnapshot snapshot;
  check(slash1 && slash2, "saber-damage finds the ground combo records");
  MmxSaberAttackResetRam(g_ram);
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  for (unsigned tick = 1; tick < slash1->chain_open_tick; ++tick) {
    MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
  }
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  for (unsigned tick = 1; tick < slash2->chain_open_tick; ++tick) {
    MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
  }
  MmxSaberAttackStep(true, true, true, g_ram[0x0c11], 0);
  MmxSaberAttackRuntimeTick(g_ram);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 32 && snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    MmxSaberAttackStep(false, true, true, g_ram[0x0c11], 0);
    MmxSaberAttackRuntimeTick(g_ram);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.kind == SABER_KIND_GROUND3 &&
            snapshot.phase == SABER_PHASE_ACTIVE,
        "saber-damage starts a ground slash 3 damage probe");
  return saber_active_slot();
}

static unsigned saber_damage_callback_probe(const char *label,
                                            unsigned enemy, unsigned slot,
                                            unsigned expected) {
  unsigned initial_hp = g_ram[enemy + 0x27] & 127;
  unsigned first = MmxSaberAttackDamage(g_ram, enemy, slot, 1);
  unsigned second = MmxSaberAttackDamage(g_ram, enemy, slot, 1);
  unsigned drop = apply_test_damage(enemy, first);
  unsigned final_hp = g_ram[enemy + 0x27] & 127;
  printf("reference: %s enemy_slot=0x%X hp=%u->%u callback=%u then=%u\n",
         label, enemy, initial_hp, final_hp, first, second);
  check(first == expected && second == 0,
        "saber-damage callback returns the tuned value once per target");
  (void)drop;
  return first;
}

static unsigned saber_damage_ground1_drop(const char *fixture,
                                          unsigned expected,
                                          unsigned *initial_hp,
                                          unsigned *final_hp) {
  unsigned target, walk_frames = 0, slot;
  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target == 0xea8,
        "saber-damage slash 1 reaches Highway enemy slot 0xEA8");
  slot = saber_damage_start_ground1();
  check(slot != 0, "saber-damage slash 1 publishes a tagged slot");
  *initial_hp = g_ram[target + 0x27] & 127;
  saber_damage_callback_probe("ground slash 1", target, slot, expected);
  *final_hp = g_ram[target + 0x27] & 127;
  printf("reference: ground slash 1 walk_frames=%u hp=%u->%u\n",
         walk_frames, *initial_hp, *final_hp);
  return *initial_hp - *final_hp;
}

static unsigned saber_damage_ground3_drop(const char *fixture,
                                          unsigned expected,
                                          unsigned *initial_hp,
                                          unsigned *final_hp) {
  unsigned target, walk_frames = 0, slot;
  target = walk_to_ground_enemy(fixture, &walk_frames);
  check(target == 0xea8,
        "saber-damage slash 3 reaches Highway enemy slot 0xEA8");
  slot = saber_damage_start_ground3();
  check(slot != 0, "saber-damage slash 3 publishes a tagged slot");
  *initial_hp = g_ram[target + 0x27] & 127;
  saber_damage_callback_probe("ground slash 3", target, slot, expected);
  *final_hp = g_ram[target + 0x27] & 127;
  printf("reference: ground slash 3 walk_frames=%u hp=%u->%u\n",
         walk_frames, *initial_hp, *final_hp);
  return *initial_hp - *final_hp;
}

static unsigned saber_damage_boss_slot(void) {
  for (unsigned d = 0xe68; d < 0x1228; d += 64)
    if (g_ram[d] && MmxWidePolicy_IsBossEncounter(g_ram[d + 0x0a])) return d;
  return 0;
}

static void saber_damage_boss_probe(const char *fixture, unsigned expected_boss,
                                    unsigned expected_normal) {
  unsigned boss, normal, slot;
  unsigned boss_first, boss_second, normal_first, normal_second;
  load_response_fixture(fixture);
  boss = saber_damage_boss_slot();
  normal = empty_enemy_slot();
  check(boss != 0 && MmxWidePolicy_IsBossEncounter(g_ram[boss + 0x0a]),
        "penguin-fight exposes a boss kind through the wide policy");
  check(normal != 0, "saber-damage has a normal enemy probe slot");
  g_ram[normal + 0x27] = 32;
  slot = saber_damage_start_ground1();
  check(slot != 0, "saber-damage boss probe publishes slash 1");
  boss_first = MmxSaberAttackDamage(g_ram, boss, slot, 1);
  boss_second = MmxSaberAttackDamage(g_ram, boss, slot, 1);
  normal_first = MmxSaberAttackDamage(g_ram, normal, slot, 1);
  normal_second = MmxSaberAttackDamage(g_ram, normal, slot, 1);
  unsigned boss_initial = g_ram[boss + 0x27] & 127;
  unsigned normal_initial = g_ram[normal + 0x27] & 127;
  apply_test_damage(boss, boss_first);
  apply_test_damage(normal, normal_first);
  printf("reference: boss kind=0x%02X boss_hp=%u->%u callback=%u then=%u "
         "normal_hp=%u->%u callback=%u then=%u\n",
         g_ram[boss + 0x0a], boss_initial, g_ram[boss + 0x27] & 127,
         boss_first, boss_second, normal_initial, g_ram[normal + 0x27] & 127,
         normal_first, normal_second);
  check(boss_first == expected_boss && boss_second == 0 &&
            normal_first == expected_normal && normal_second == 0,
        "boss and normal slash 1 damage use their respective tuning values");
}

static void saber_damage_disabled_finisher_probe(void) {
  MmxZeroState state = MmxZeroGetState();
  unsigned slot = 0;
  unsigned enemy;
  for (unsigned d = 0x1228; d < 0x1428; d += 64) {
    if (!g_ram[d]) {
      slot = d;
      break;
    }
  }
  check(slot != 0, "disabled finisher probe has a free native projectile slot");
  memset(g_ram + slot, 0, 64);
  g_ram[slot] = 1;
  g_ram[slot + 1] = 2;
  g_ram[slot + 0x3e] = 0x53;
  g_ram[slot + 0x3f] = 0x5a;
  ++g_ram[0xbdd];
  state.slash = 1;
  state.projectile = (uint16_t)slot;
  state.hit_slots = 0;
  MmxZeroSetState(state);
  enemy = empty_enemy_slot();
  check(MmxZeroDamage(g_ram, enemy, slot, 3) == 16,
        "Saber-disabled native $5A53 still deals upstream 16");
  MmxZeroCancel(g_ram);
}

static void saber_damage_checks(const char *x1_rom, const char *x3_rom,
                                const char *fixture, const char *assets,
                                const char *fixture_dir) {
  static const SaberWallRoute wall_route = {
    "OPEN-RIGHT", SNES_PAD_LEFT, SNES_PAD_B | SNES_PAD_LEFT,
    SNES_PAD_LEFT, 60, 20, 5142, 2665, 0x40, 1};
  char wall_path[4096];
  unsigned initial_hp, final_hp, drop, slot, enemy;
  int written = snprintf(wall_path, sizeof(wall_path), "%s/%s", fixture_dir,
                         "armadillo-fight.sav");
  check(written >= 0 && written < (int)sizeof(wall_path),
        "saber-damage wall fixture path fits");

  saber_damage_set_option("slash1_damage", "3");
  saber_damage_set_option("slash3_damage", "8");
  saber_damage_reactivate();
  drop = saber_damage_ground1_drop(
      fixture, MmxSaberTuningNormalDamage(MMX_SABER_TUNING_DAMAGE_SLASH1),
      &initial_hp, &final_hp);
  check(drop == 3 && final_hp == initial_hp - 3,
        "default ground slash 1 drops Highway HP by 3");

  saber_damage_set_option("slash1_damage", "5");
  saber_damage_reactivate();
  drop = saber_damage_ground1_drop(fixture, 5, &initial_hp, &final_hp);
  check(drop == 5 && final_hp == initial_hp - 5,
        "slash1_damage=5 drops Highway HP by 5");

  saber_damage_set_option("slash3_damage", "8");
  saber_damage_reactivate();
  drop = saber_damage_ground3_drop(fixture, 8, &initial_hp, &final_hp);
  check(drop == 8 && final_hp == initial_hp - 8,
        "default ground slash 3 drops Highway HP by 8");

  saber_damage_set_option("slash3_damage", "32");
  saber_damage_reactivate();
  drop = saber_damage_ground3_drop(fixture, 32, &initial_hp, &final_hp);
  check(drop == initial_hp && final_hp == 0,
        "slash3_damage=32 clamps to the native HP subtraction and kills Highway");

  saber_damage_set_option("air_damage", "11");
  saber_damage_set_option("wall_damage", "13");
  saber_damage_set_option("dash_damage", "17");
  saber_damage_reactivate();

  begin_air_landing_probe(fixture);
  advance_air_landing_probe(6);
  slot = saber_active_slot();
  enemy = empty_enemy_slot();
  check(slot != 0 && enemy != 0,
        "saber-damage air setup publishes a live active slot");
  g_ram[enemy + 0x27] = 32;
  saber_damage_callback_probe("air slash", enemy, slot, 11);

  check(saber_wall_setup(wall_path, &wall_route, false) ==
            wall_route.expected_wall_frame,
        "saber-damage wall setup reaches the armadillo cling");
  frame(wall_route.travel_input | SNES_PAD_Y);
  slot = saber_active_slot();
  enemy = empty_enemy_slot();
  check(slot != 0 && enemy != 0,
        "saber-damage wall setup publishes a live active slot");
  g_ram[enemy + 0x27] = 32;
  saber_damage_callback_probe("wall slash", enemy, slot, 13);

  load_fixture(fixture);
  MmxSaberFrameReset();
  check(start_saber_dash_right(0) != 0,
        "saber-damage dash setup reaches the native dash");
  MmxSaberAttackSnapshot dash_snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < 12 && dash_snapshot.phase != SABER_PHASE_ACTIVE;
       ++i) {
    frame(SNES_PAD_A | SNES_PAD_RIGHT | (i == 0 ? SNES_PAD_Y : 0));
    dash_snapshot = MmxSaberAttackSnapshotGet();
  }
  slot = saber_active_slot();
  enemy = empty_enemy_slot();
  check(slot != 0 && enemy != 0 &&
            dash_snapshot.kind == SABER_KIND_DASH &&
            dash_snapshot.phase == SABER_PHASE_ACTIVE,
        "saber-damage dash setup publishes a live active slot");
  g_ram[enemy + 0x27] = 32;
  saber_damage_callback_probe("dash slash", enemy, slot, 17);

  saber_damage_set_option("x3_finisher_damage", "20");
  saber_damage_set_option("wave_damage", "4");
  saber_damage_reactivate();
  saber_wave_damage_checks(fixture);

  char penguin_path[4096];
  written = snprintf(penguin_path, sizeof(penguin_path), "%s/%s", fixture_dir,
                     "penguin-fight.sav");
  check(written >= 0 && written < (int)sizeof(penguin_path),
        "saber-damage boss fixture path fits");
  saber_damage_set_option("slash1_damage", "3");
  saber_damage_set_option("boss_slash1_damage", "0");
  saber_damage_reactivate();
  saber_damage_boss_probe(penguin_path, 3, 3);
  saber_damage_set_option("boss_slash1_damage", "7");
  saber_damage_reactivate();
  saber_damage_boss_probe(penguin_path, 7, 3);

  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(!MmxSaberEnabled(), "saber-damage disables Saber for the upstream check");
  saber_damage_disabled_finisher_probe();
  zero_legacy_slash_request_checks(fixture);
  puts("ok: saber-damage");
}

static void zero_hook_parity_checks(const char *fixture) {
  check(!MmxSaberEnabled(), "zero-hook-parity runs with Saber disabled");
  zero_extension_checks(fixture);
  zero_legacy_slash_request_checks(fixture);
  puts("ok: zero-hook-parity");
}

static void saber_assets_checks(const char *x1_rom, const char *x3_rom,
                                const char *fixture, const char *assets) {
  const char *empty_cache = getenv("MMX_SABER_EMPTY_CACHE");
  const char *cache = getenv("MMX_SABER_TEST_CACHE");
  char sfx_path[4096];
  static const MmxSaberSfxAttackCue kCues[] = {
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_1,
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_2,
    MMX_SABER_SFX_ATTACK_GROUND_SLASH_3,
    MMX_SABER_SFX_ATTACK_X3_FINISHER,
    MMX_SABER_SFX_ATTACK_AIR,
    MMX_SABER_SFX_ATTACK_WALL,
    MMX_SABER_SFX_ATTACK_DASH
  };
  static const unsigned kExpectedClips[] = {
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_2,
    MMX_SABER_SFX_CLIP_SABER_3,
    MMX_SABER_SFX_CLIP_SABER_3,
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_1,
    MMX_SABER_SFX_CLIP_SABER_2
  };
  check(empty_cache && empty_cache[0], "runner supplies an empty Saber cache");
  check(cache && cache[0] &&
            snprintf(sfx_path, sizeof(sfx_path), "%s/mmx-source/%s", cache,
                     "saber-sfx-v2.bin") < (int)sizeof(sfx_path),
        "runner supplies an isolated Saber SFX cache path");

  activate_zero(x1_rom, x3_rom, assets, true, true);
  check(MmxSaberAssetsLoaded() && MmxSaberRideAssetsLoaded() &&
            MmxSaberWaveLoaded() && MmxSaberEnabled(),
        "Saber asset activation enables sprite and wave caches");
  check(MmxSaberSfxLoaded(), "Saber SFX sidecar loads at activation");
  check(MmxSaberSfxVolume() == 50,
        "Saber SFX package option defaults to 50 percent");
  for (unsigned i = 0; i < sizeof(kCues) / sizeof(kCues[0]); ++i) {
    MmxSaberSfxPlayForAttack(kCues[i]);
    check(MmxSaberSfxLastClip() == kExpectedClips[i],
          "Saber attack cue maps to the expected clip");
  }
  check(MmxSaberSfxRegisteredClipCount() == MMX_SABER_SFX_CLIP_COUNT,
        "Saber SFX clips register lazily on first cue");

  check(remove(sfx_path) == 0, "temporary SFX cache can be removed");
  snes_mod_runtime_activate_plugins_c();
  check(MmxSaberEnabled() && MmxSaberAssetsLoaded() &&
            MmxSaberRideAssetsLoaded() && MmxSaberWaveLoaded(),
        "missing SFX cache does not disable Saber");
  check(!MmxSaberSfxLoaded(), "missing SFX cache leaves SFX unavailable only");

  check(set_test_env("MMX_SABER_TEST_CACHE", empty_cache) == 0,
        "Saber test redirects to the empty cache");
  check(set_test_env("MMX_SABER_TEST_CACHE_ONLY", "1") == 0,
        "Saber missing-cache run disables message boxes");
  snes_mod_runtime_activate_plugins_c();
  check(MmxZeroEnabled() && MmxZeroActive() && !MmxZeroModern(),
        "X3 Zero remains active when Saber caches are missing");
  check(!MmxSaberEnabled() && !MmxSaberAssetsLoaded() &&
            !MmxSaberRideAssetsLoaded() && !MmxSaberWaveLoaded(),
        "missing Saber caches leave Saber disabled");
  x3_plain_checks(fixture, SNES_PAD_Y);
  puts("ok: saber-assets");
}

static bool saber_window_empty(const uint8_t *window) {
  for (unsigned i = 0; i < 40; ++i)
    if (window[i] != 0xff) return false;
  return true;
}

static bool saber_wave_record_empty(const uint8_t *record) {
  for (unsigned i = 0; i < MMX_SABER_WAVE_COLLISION_RECORD_BYTES; ++i)
    if (record[i] != 0xff) return false;
  return true;
}

static void saber_lifecycle_load_checks(const char *x1_rom,
                                        const char *x3_rom,
                                        const char *fixture,
                                        const char *assets) {
  MmxSaberAttackSnapshot snapshot;
  const size_t snapshot_capacity = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *airborne_snapshot = malloc(snapshot_capacity);

  check(snapshot_capacity != 0 && airborne_snapshot != NULL,
        "lifecycle test allocates an in-memory snapshot buffer");

  /* D1: SetState enters the reset seam once for its reset and once again
   * after the replacement state has been installed. Keep this assertion
   * separate from Saber behavior so either generic call cannot disappear. */
  MmxZeroExtension probe = { .state_reset = zero_state_reset_probe };
  const MmxZeroState zero_state = MmxZeroGetState();
  zero_state_reset_calls = 0;
  MmxZeroSetExtension(&probe);
  MmxZeroSetState(zero_state);
  MmxZeroSetExtension(MmxSaberFrameExtension());
  check(zero_state_reset_calls == 2,
        "state replacement calls the generic reset seam at both boundaries");

  /* D4a: replace a live Saber attack with the standing save in the same
   * process. The callback must retire the guest slot before the next frame. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < OLD_SABER_GROUND1_ACTIVE; ++i) frame(0);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_ACTIVE && tagged_projectiles() == 1,
        "hot-load probe reaches ACTIVE with a live Saber-tagged slot");
  check(RtlLoadSnapshot(fixture),
        "hot-load probe replaces the running state with save0.sav");
  check(saber_lifecycle_idle(),
        "state replacement immediately returns Saber idle and retires its slot");
  const unsigned cues_after_load = MmxSaberAttackCueCount();
  idle(OLD_SABER_GROUND1_TOTAL);
  check(MmxSaberAttackCueCount() == cues_after_load,
        "state replacement emits no delayed Saber cue");
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.kind == SABER_KIND_GROUND1 &&
            snapshot.phase == SABER_PHASE_STARTUP && snapshot.tick == 0,
        "the next Y after a hot load starts ground slash 1 normally");

  /* D4b: save an actually airborne guest state through the harness API,
   * replace an active Saber state with it, and inspect the first frames. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  bool airborne = false;
  for (unsigned i = 0; i < 60; ++i) {
    frame(SNES_PAD_B);
    if (!(g_ram[0xbd3] & 4)) {
      airborne = true;
      break;
    }
  }
  check(airborne, "lifecycle harness reaches an airborne state");
  const size_t airborne_size =
      RtlSaveSnapshotToMemory(airborne_snapshot, snapshot_capacity);
  check(airborne_size != 0, "airborne state saves through the harness API");

  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < OLD_SABER_GROUND1_ACTIVE; ++i) frame(0);
  check(MmxSaberAttackSnapshotGet().phase == SABER_PHASE_ACTIVE,
        "airborne-load probe starts from a live Saber attack");
  check(RtlLoadSnapshotFromMemory(airborne_snapshot, airborne_size),
        "airborne load replaces the running Saber state");
  bool fake_landing = false;
  for (unsigned i = 0; i < 3; ++i) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
    fake_landing |= snapshot.kind == SABER_KIND_SABER_LAND;
  }
  check(!fake_landing,
        "loading airborne RAM does not synthesize SaberLand before a landing edge");

  /* D4c: release X during an attack, then replace the state. A fresh X edge
   * must remain a fresh edge; a stale CR1 release latch would suppress it. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  hold_charge_button(10, SNES_PAD_X);
  frame(SNES_PAD_X | SNES_PAD_Y);
  for (unsigned i = 0; i < OLD_SABER_GROUND1_ACTIVE; ++i)
    frame(SNES_PAD_X);
  frame(0);
  check(MmxSaberAttackSnapshotGet().phase != SABER_PHASE_IDLE,
        "CR1 probe releases X while the Saber attack is still live");
  check(RtlLoadSnapshot(fixture),
        "CR1 probe replaces the running state before attack completion");
  /* The fixture's upstream save is allowed to carry an ordinary charge; clear
   * that upstream state so the shot assertion isolates the stale Saber latch. */
  MmxZeroCancel(g_ram);
  unsigned char native_before[8];
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned d = 0x1228 + i * 64;
    native_before[i] = (unsigned char)(g_ram[d] && !saber_tagged_projectile(d));
  }
  /* Feed only the Saber pre-player seam with a fresh X edge. This exposes the
   * mapped press without allowing native buster code to create an ordinary
   * uncharged shot that would obscure the stale-latch assertion. */
  g_ram[0x00a7] = MMX_SABER_NATIVE_FIRE_BIT;
  g_ram[0x00a9] = 0;
  g_ram[0x00ac] = 0;
  MmxZeroExtPrePlayer(g_ram);
  check((g_ram[0x0be3] & MMX_SABER_NATIVE_FIRE_BIT) != 0,
        "state replacement clears the CR1 latch before the next X edge");
  frame(0);
  bool native_birth = false;
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned d = 0x1228 + i * 64;
    native_birth |= g_ram[d] && !saber_tagged_projectile(d) && !native_before[i];
  }
  check(!native_birth,
        "state replacement fires no stale charged shot");

  /* D4d: use the same provider activation/deactivation route as the asset
   * lifecycle checks, then verify both ownership cleanup and reinstallation. */
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(0);
  check(!memcmp(g_snes->cart->rom + 0x37fd8, kSaberGroundBounds, 40) &&
            !memcmp(g_snes->cart->rom + 0x37f40, kSaberAirBounds, 40),
        "disable probe starts with both Saber collision windows installed");
  activate_zero(x1_rom, x3_rom, assets, false, false);
  check(saber_window_empty(g_snes->cart->rom + 0x37fd8) &&
            saber_window_empty(g_snes->cart->rom + 0x37f40),
        "disabling Saber restores only its collision windows to $FF");
  activate_zero(x1_rom, x3_rom, assets, true, true);
  load_fixture(fixture);
  frame(0);
  check(!memcmp(g_snes->cart->rom + 0x37fd8, kSaberGroundBounds, 40) &&
            !memcmp(g_snes->cart->rom + 0x37f40, kSaberAirBounds, 40),
        "re-enabling Saber reinstalls both collision windows");

  free(airborne_snapshot);
  puts("ok: saber-lifecycle-load");
}

static bool saber_context_cyan_box_present(void) {
  MmxRenderDebugRect rects[64];
  const unsigned count = MmxSaberHitboxDebugProvide(rects,
                                                     sizeof(rects) /
                                                         sizeof(rects[0]));
  for (unsigned i = 0; i < count; ++i)
    if (rects[i].rgb555 == MMX_SABER_HITBOX_CYAN) return true;
  return false;
}

static bool saber_context_clean(unsigned cue_count) {
  const MmxZeroState zero = MmxZeroGetState();
  MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
  MmxRendererBeginFrame(g_ram);
  return snapshot.phase == SABER_PHASE_IDLE &&
      snapshot.kind == SABER_KIND_NONE && snapshot.anim_id == 0 &&
      tagged_projectiles() == 0 && MmxSaberAttackHitSlots() == 0 &&
      !saber_context_cyan_box_present() && !zero.slash && !zero.burst &&
      !zero.combo && !zero.projectile && !zero.hit_slots &&
      !MmxSaberWaveRuntimeActive(g_ram) &&
      !saber_finisher_wave_slot() && !MmxSaberComboReservedSlot() &&
      !MmxSaberComboWindowTicks() && !MmxSaberComboFinisherCueCount() &&
      MmxSaberAttackCueCount() == cue_count &&
      !MmxRendererPlayerOverlaySnapshot().active;
}

static void saber_context_start_ground(const char *fixture) {
  MmxSaberAttackSnapshot snapshot;
  load_fixture(fixture);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  snapshot = MmxSaberAttackSnapshotGet();
  for (unsigned i = 0; i < OLD_SABER_GROUND1_ACTIVE + 2 &&
       snapshot.phase != SABER_PHASE_ACTIVE; ++i) {
    frame(0);
    snapshot = MmxSaberAttackSnapshotGet();
  }
  check(snapshot.kind == SABER_KIND_GROUND1 &&
            snapshot.phase == SABER_PHASE_ACTIVE && tagged_projectiles() == 1,
        "context ground route reaches an active ground slash with one tagged slot");
}

static void saber_context_start_air(const char *fixture) {
  begin_air_landing_probe(fixture);
  advance_air_landing_probe(6);
  check(MmxSaberAttackSnapshotGet().kind == SABER_KIND_AIR &&
            MmxSaberAttackSnapshotGet().phase == SABER_PHASE_ACTIVE &&
            tagged_projectiles() == 1,
        "context air route reaches an active air slash with one tagged slot");
}

static bool saber_pause_open(void) {
  return (g_ram[0x1f10] == 6 || g_ram[0x1f10] == 8) &&
      (g_ram[0x00c3] & 0x80);
}

static void saber_context_death_checks(const char *fixture) {
  unsigned cues;

  saber_context_start_ground(fixture);
  cues = MmxSaberAttackCueCount();
  g_ram[0x0bcf] = 0;
  frame(0);
  check(saber_context_clean(cues),
        "death on an active ground slash clears every Saber owner on the death frame");
  for (unsigned i = 0; i < 12; ++i) {
    g_ram[0x0bcf] = 0;
    g_ram[0x0baa] = 0x0c;
    frame(SNES_PAD_Y);
  }
  check(saber_context_clean(cues),
        "death input cannot restart Saber during the death sequence");
  puts("ok: saber-contexts/death");
}

static void saber_context_pause_checks(const char *fixture) {
  MmxSaberAttackSnapshot before;
  MmxSaberAttackSnapshot after;
  unsigned cues;
  bool opened = false;
  bool no_menu_start = true;

  saber_context_start_ground(fixture);
  before = MmxSaberAttackSnapshotGet();
  cues = MmxSaberAttackCueCount();
  frame(SNES_PAD_START);
  for (unsigned i = 0; i < 75; ++i) frame(0);
  opened |= saber_pause_open();
  check(opened, "save0 Highway Start input opens the native weapon menu");

  for (unsigned i = 0; i < 4; ++i) {
    frame(SNES_PAD_Y);
    after = MmxSaberAttackSnapshotGet();
    if (MmxSaberAttackCueCount() != cues ||
        (after.phase == SABER_PHASE_STARTUP && after.tick == 0))
      no_menu_start = false;
  }
  check(no_menu_start,
        "Y pressed in the native pause menu does not start or cue Saber");

  frame(SNES_PAD_START);
  for (unsigned i = 0; i < 75; ++i) frame(0);
  bool closed = !saber_pause_open();
  check(closed, "native weapon menu closes through a second Start input");
  after = MmxSaberAttackSnapshotGet();
  check(MmxSaberAttackCueCount() == cues &&
            (after.phase == SABER_PHASE_IDLE || after.tick >= before.tick),
        "pause resume cleans up or continues the same slash without replay");
  if (after.phase != SABER_PHASE_IDLE)
    idle(OLD_SABER_GROUND1_TOTAL + 2);
  check(saber_context_clean(cues),
        "pause route has no delayed Saber cue, hitbox, or overlay");
  puts("ok: saber-contexts/pause");
}

static void saber_context_scene_checks(const char *fixture_dir) {
  char path[4096];
  MmxSaberAttackSnapshot snapshot;
  unsigned cues;
  int written = snprintf(path, sizeof(path), "%s/%s", fixture_dir,
                         "penguin-fight.sav");
  check(written >= 0 && written < (int)sizeof(path),
        "scripted-scene fixture path fits");
  check(readable_file(path), "penguin-fight.sav exists for scripted control");
  check(RtlLoadSnapshot(path), "penguin-fight.sav loads for scripted control");
  check(MmxZeroActive() && !MmxZeroModern() && g_ram[0x1f7a] == 8,
        "penguin-fight.sav starts in the Chill Penguin stage");
  MmxZeroCancel(g_ram);
  MmxSaberFrameReset();
  frame(SNES_PAD_Y);
  for (unsigned i = 0; i < OLD_SABER_GROUND1_ACTIVE + 2; ++i) {
    snapshot = MmxSaberAttackSnapshotGet();
    if (snapshot.phase == SABER_PHASE_ACTIVE) break;
    frame(0);
  }
  snapshot = MmxSaberAttackSnapshotGet();
  check(snapshot.phase == SABER_PHASE_ACTIVE,
        "penguin-fight route starts a ground slash before scripted control");
  cues = MmxSaberAttackCueCount();
  /* $1F0C is the native scripted-player owner flag used by the existing
   * zero_frame_context gate. Exercise the post-pre-player ownership change,
   * then keep it asserted while Y is pressed. */
  MmxZeroExtPrePlayer(g_ram);
  g_ram[0x1f0c] = 1;
  MmxZeroExtPlayerEnd(g_ram);
  check(saber_context_clean(cues),
        "scripted ownership change at player_end clears the Saber owner");
  bool scene_detected = false;
  for (unsigned i = 0; i < 4; ++i) {
    g_ram[0x1f0c] = 1;
    scene_detected |= g_ram[0x1f0c] != 0;
    frame(SNES_PAD_Y);
  }
  check(scene_detected, "penguin-fight scripted control is detected by $1F0C");
  check(saber_context_clean(cues),
        "scripted control clears the active slash and all Saber ownership");
  for (unsigned i = 0; i < 8; ++i) {
    g_ram[0x1f0c] = 1;
    frame(SNES_PAD_Y);
  }
  check(saber_context_clean(cues),
        "Y cannot start Saber while scripted control remains active");
  puts("ok: saber-contexts/scripted-scene");
}

static void saber_context_load_checks(const char *fixture) {
  const size_t capacity = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *saved = malloc(capacity);

  check(capacity != 0 && saved != NULL,
        "context load route allocates an in-memory save-state buffer");
  saber_context_start_air(fixture);
  const size_t size = RtlSaveSnapshotToMemory(saved, capacity);
  check(size != 0 && tagged_projectiles() == 1,
        "air slash save-state route captures a live Saber slot");
  frame(0);
  check(RtlLoadSnapshotFromMemory(saved, size),
        "air slash save-state route restores through the normal loader");
  MmxRendererBeginFrame(g_ram);
  const bool load_overlay_clear =
      !MmxRendererPlayerOverlaySnapshot().active;
  check(load_overlay_clear && saber_context_clean(MmxSaberAttackCueCount()) &&
            MmxSaberPriorityCurrentFrame() == 0,
        "save-state load drops air Saber, hitbox, overlay, combo, and priority state");
  const unsigned loaded_cues = MmxSaberAttackCueCount();
  idle(OLD_SABER_AIR_TOTAL + 2);
  check(MmxSaberAttackCueCount() == loaded_cues,
        "air slash save-state load emits no delayed Saber cue");
  free(saved);

  saber_context_start_air(fixture);
  check(set_test_env("SNESRECOMP_REWIND", "1") == 0,
        "context rewind route enables the rewind harness");
  snes_rewind_shutdown();
  snes_rewind_set_defaults(1, 4, 1);
  snes_rewind_configure();
  for (unsigned i = 0; i < 5; ++i) {
    frame(0);
    snes_rewind_note_frame();
  }
  check(snes_rewind_open(), "context rewind route opens its real snapshot ring");
  snes_rewind_step(-1);
  snes_rewind_commit();
  MmxRendererBeginFrame(g_ram);
  const bool rewind_overlay_clear =
      !MmxRendererPlayerOverlaySnapshot().active;
  check(rewind_overlay_clear && saber_context_clean(MmxSaberAttackCueCount()) &&
            MmxSaberPriorityCurrentFrame() == 0,
        "rewind drops air Saber, hitbox, overlay, combo, and priority state");
  const unsigned rewound_cues = MmxSaberAttackCueCount();
  idle(OLD_SABER_AIR_TOTAL + 2);
  check(MmxSaberAttackCueCount() == rewound_cues,
        "rewind emits no delayed Saber cue");
  snes_rewind_shutdown();
  puts("ok: saber-contexts/load-rewind");
}

static void saber_context_checks(const char *fixture, const char *fixture_dir) {
  saber_context_death_checks(fixture);
  saber_context_pause_checks(fixture);
  saber_context_scene_checks(fixture_dir);
  saber_context_load_checks(fixture);
  puts("ok: saber-contexts");
}

extern const char *g_last_recomp_func;

typedef struct SaberBossDeathTraceFrame {
  unsigned number;
  unsigned input;
  unsigned before_live;
  unsigned before_hp;
  unsigned before_state;
  unsigned before_substate;
  unsigned before_action;
  unsigned before_kind;
  unsigned before_row;
  unsigned before_protection;
  unsigned before_flags;
  unsigned before_hard_skip;
  unsigned after_live;
  unsigned after_hp;
  unsigned after_state;
  unsigned after_substate;
  unsigned after_action;
  unsigned after_kind;
  unsigned after_row;
  unsigned after_protection;
  unsigned after_flags;
  unsigned after_hard_skip;
  unsigned response_calls;
  unsigned response_original;
  unsigned response_value;
  unsigned response_class;
  unsigned damage_calls;
  unsigned damage_original;
  unsigned damage_value;
  unsigned damage_class;
  unsigned damage_hp;
  unsigned damage_protection;
  unsigned damage_state;
  unsigned damage_substate;
  unsigned damage_action;
  unsigned damage_row;
  unsigned damage_hard_skip;
} SaberBossDeathTraceFrame;

static const MmxZeroExtension *saber_boss_trace_base;
static MmxZeroExtension saber_boss_trace_extension;
static SaberBossDeathTraceFrame *saber_boss_trace_current;
static unsigned saber_boss_trace_target;
static bool saber_boss_trace_quiet;

static unsigned saber_boss_trace_byte(unsigned offset) {
  return saber_boss_trace_target ? g_ram[saber_boss_trace_target + offset] : 0;
}

static unsigned saber_boss_trace_response(uint8_t *ram, unsigned enemy,
                                          unsigned projectile,
                                          unsigned original) {
  unsigned value;
  if (saber_boss_trace_current && enemy == saber_boss_trace_target) {
    MmxSaberPriorityClassification classification;
    ++saber_boss_trace_current->response_calls;
    saber_boss_trace_current->response_original = original;
    saber_boss_trace_current->response_class =
        MmxSaberPriorityClassify(ram, projectile, &classification) ?
        classification.priority_class : MMX_SABER_PRIORITY_CLASS_NONE;
  }
  value = saber_boss_trace_base && saber_boss_trace_base->response ?
      saber_boss_trace_base->response(ram, enemy, projectile, original) : original;
  if (saber_boss_trace_current && enemy == saber_boss_trace_target)
    saber_boss_trace_current->response_value = value;
  return value;
}

static unsigned saber_boss_trace_damage(uint8_t *ram, unsigned enemy,
                                        unsigned projectile, unsigned original) {
  unsigned value;
  if (saber_boss_trace_current && enemy == saber_boss_trace_target) {
    MmxSaberPriorityClassification classification;
    ++saber_boss_trace_current->damage_calls;
    saber_boss_trace_current->damage_original = original;
    saber_boss_trace_current->damage_hp = ram[enemy + 0x27];
    saber_boss_trace_current->damage_state = ram[enemy + 0x01];
    saber_boss_trace_current->damage_substate = ram[enemy + 0x02];
    saber_boss_trace_current->damage_action = ram[enemy + 0x03];
    saber_boss_trace_current->damage_row = ram[enemy + 0x28];
    saber_boss_trace_current->damage_hard_skip = ram[enemy + 0x30];
    saber_boss_trace_current->damage_class =
        MmxSaberPriorityClassify(ram, projectile, &classification) ?
        classification.priority_class : MMX_SABER_PRIORITY_CLASS_NONE;
  }
  value = saber_boss_trace_base && saber_boss_trace_base->damage ?
      saber_boss_trace_base->damage(ram, enemy, projectile, original) : original;
  if (saber_boss_trace_current && enemy == saber_boss_trace_target) {
    saber_boss_trace_current->damage_value = value;
    saber_boss_trace_current->damage_protection = ram[enemy + 0x35];
  }
  return value;
}

static void saber_boss_trace_install(unsigned target) {
  saber_boss_trace_target = target;
  saber_boss_trace_base = MmxSaberFrameExtension();
  check(saber_boss_trace_base != NULL,
        "boss-death trace receives the Saber extension");
  saber_boss_trace_extension = *saber_boss_trace_base;
  saber_boss_trace_extension.response = saber_boss_trace_response;
  saber_boss_trace_extension.damage = saber_boss_trace_damage;
  MmxZeroSetExtension(&saber_boss_trace_extension);
}

static void saber_boss_trace_restore(void) {
  MmxZeroSetExtension(MmxSaberFrameExtension());
  saber_boss_trace_current = NULL;
  saber_boss_trace_base = NULL;
  saber_boss_trace_target = 0;
}

static void saber_boss_trace_sample_before(SaberBossDeathTraceFrame *trace) {
  trace->before_live = saber_boss_trace_byte(0);
  trace->before_hp = saber_boss_trace_byte(0x27);
  trace->before_state = saber_boss_trace_byte(1);
  trace->before_substate = saber_boss_trace_byte(2);
  trace->before_action = saber_boss_trace_byte(3);
  trace->before_kind = saber_boss_trace_byte(0x0a);
  trace->before_row = saber_boss_trace_byte(0x28);
  trace->before_protection = saber_boss_trace_byte(0x35);
  trace->before_flags = saber_boss_trace_byte(0x30);
  trace->before_hard_skip = saber_boss_trace_byte(0x38);
}

static void saber_boss_trace_sample_after(SaberBossDeathTraceFrame *trace) {
  trace->after_live = saber_boss_trace_byte(0);
  trace->after_hp = saber_boss_trace_byte(0x27);
  trace->after_state = saber_boss_trace_byte(1);
  trace->after_substate = saber_boss_trace_byte(2);
  trace->after_action = saber_boss_trace_byte(3);
  trace->after_kind = saber_boss_trace_byte(0x0a);
  trace->after_row = saber_boss_trace_byte(0x28);
  trace->after_protection = saber_boss_trace_byte(0x35);
  trace->after_flags = saber_boss_trace_byte(0x30);
  trace->after_hard_skip = saber_boss_trace_byte(0x38);
}

typedef struct SaberBossDeathResult {
  bool first_hit;
  bool bypass_hit;
  bool lethal;
  bool defeat_started;
  unsigned first_class;
  unsigned bypass_class;
  unsigned lethal_class;
  unsigned first_hit_frame;
  unsigned lethal_frame;
  unsigned defeat_frame;
  unsigned final_live;
  unsigned final_hp;
  unsigned final_state;
  unsigned final_substate;
  unsigned final_action;
  unsigned final_protection;
  unsigned lethal_before_state;
  unsigned lethal_before_substate;
  unsigned lethal_before_action;
  unsigned lethal_before_protection;
  unsigned lethal_before_row;
  unsigned lethal_response_original;
  unsigned lethal_response_value;
  unsigned lethal_damage_original;
  unsigned lethal_damage_value;
  unsigned lethal_damage_state;
  unsigned lethal_damage_substate;
  unsigned lethal_damage_action;
  unsigned lethal_damage_protection;
  unsigned lethal_damage_row;
  unsigned lethal_damage_hard_skip;
} SaberBossDeathResult;

enum { SABER_BOSS_DEFEAT_N = 1 };

static SaberBossDeathTraceFrame saber_boss_trace_frame(unsigned number,
                                                       unsigned input,
                                                       bool print_line) {
  SaberBossDeathTraceFrame trace = {0};
  trace.number = number;
  trace.input = input;
  saber_boss_trace_sample_before(&trace);
  saber_boss_trace_current = &trace;
  frame(input);
  saber_boss_trace_current = NULL;
  saber_boss_trace_sample_after(&trace);
  if (!saber_boss_trace_quiet &&
      (print_line || trace.response_calls || trace.damage_calls ||
      trace.before_hp != trace.after_hp || trace.before_state != trace.after_state ||
      trace.before_substate != trace.after_substate || trace.before_action != trace.after_action ||
      trace.before_live != trace.after_live)) {
    printf("boss-trace f=%u input=0x%X live=%u/%u hp=%u/%u state=%02X/%02X sub=%02X/%02X action=%02X/%02X "
           "kind=%02X/%02X row=%02X/%02X prot=%02X/%02X flags=%02X/%02X hard=%02X/%02X "
           "response=%u:%u->%u class=%u damage=%u:%u->%u class=%u "
           "damage_hp=%02X damage_prot=%02X damage_state=%02X/%02X/%02X "
           "damage_row=%02X damage_hard=%02X last=%s\n",
           trace.number, trace.input, trace.before_live, trace.after_live,
           trace.before_hp, trace.after_hp, trace.before_state, trace.after_state,
           trace.before_substate, trace.after_substate, trace.before_action,
           trace.after_action, trace.before_kind, trace.after_kind,
           trace.before_row, trace.after_row, trace.before_protection,
           trace.after_protection, trace.before_flags, trace.after_flags,
           trace.before_hard_skip, trace.after_hard_skip,
           trace.response_calls, trace.response_original, trace.response_value,
           trace.response_class, trace.damage_calls, trace.damage_original,
           trace.damage_value, trace.damage_class,
           trace.damage_hp, trace.damage_protection,
           trace.damage_state, trace.damage_substate, trace.damage_action,
           trace.damage_row, trace.damage_hard_skip,
           g_last_recomp_func ? g_last_recomp_func : "?");
  }
  return trace;
}

static void saber_boss_death_result_update(SaberBossDeathResult *result,
                                           const SaberBossDeathTraceFrame *trace) {
  const unsigned before_hp = trace->before_hp & 127;
  const unsigned after_hp = trace->after_hp & 127;
  result->final_live = trace->after_live;
  result->final_hp = trace->after_hp;
  result->final_state = trace->after_state;
  result->final_substate = trace->after_substate;
  result->final_action = trace->after_action;
  result->final_protection = trace->after_protection;
  if (!result->first_hit && trace->damage_calls && trace->damage_value &&
      before_hp > after_hp && after_hp) {
    result->first_hit = true;
    result->first_class = trace->damage_class;
    result->first_hit_frame = trace->number;
  }
  if (!result->bypass_hit && trace->response_calls && trace->response_original == 0 &&
      trace->response_value == 1 && trace->damage_calls &&
      trace->damage_original == 0 && trace->damage_value) {
    result->bypass_hit = true;
    result->bypass_class = trace->damage_class;
  }
  if (!result->lethal && trace->damage_calls && trace->damage_value &&
      before_hp && !after_hp) {
    result->lethal = true;
    result->lethal_class = trace->damage_class;
    result->lethal_frame = trace->number;
    result->lethal_before_state = trace->before_state;
    result->lethal_before_substate = trace->before_substate;
    result->lethal_before_action = trace->before_action;
    result->lethal_before_protection = trace->before_protection;
    result->lethal_before_row = trace->before_row;
    result->lethal_response_original = trace->response_original;
    result->lethal_response_value = trace->response_value;
    result->lethal_damage_original = trace->damage_original;
    result->lethal_damage_value = trace->damage_value;
    result->lethal_damage_state = trace->damage_state;
    result->lethal_damage_substate = trace->damage_substate;
    result->lethal_damage_action = trace->damage_action;
    result->lethal_damage_protection = trace->damage_protection;
    result->lethal_damage_row = trace->damage_row;
    result->lethal_damage_hard_skip = trace->damage_hard_skip;
  }
  if (!result->defeat_started && result->lethal && !after_hp &&
      trace->after_state == 6) {
    result->defeat_started = true;
    result->defeat_frame = trace->number;
  }
}

static unsigned saber_boss_near_player(unsigned expected_kind) {
  const unsigned zero_x = read_ram_word(g_ram, 0x0bad);
  const unsigned zero_y = read_ram_word(g_ram, 0x0bb0);
  unsigned best = 0;
  unsigned best_distance = 0xffff;
  for (unsigned d = 0xe68; d < 0x1228; d += 64) {
    unsigned enemy_x, enemy_y, distance;
    if (!g_ram[d] || !g_ram[d + 14] || !(g_ram[d + 0x27] & 127) ||
        g_ram[d + 0x30] || !MmxWidePolicy_IsBossEncounter(g_ram[d + 0x0a]) ||
        (expected_kind && g_ram[d + 0x0a] != expected_kind))
      continue;
    enemy_x = read_ram_word(g_ram, d + 5);
    enemy_y = read_ram_word(g_ram, d + 8);
    distance = abs_difference(enemy_x, zero_x) + abs_difference(enemy_y, zero_y);
    if (distance < best_distance) {
      best = d;
      best_distance = distance;
    }
  }
  return best;
}

static unsigned saber_boss_walk_to(const char *fixture, unsigned expected_kind,
                                    unsigned *walk_frames) {
  unsigned target = 0;
  load_response_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < 360 && !target; ++i) {
    target = saber_boss_near_player(expected_kind);
    if (target) {
      const int dx = (int)read_ram_word(g_ram, target + 5) -
          (int)read_ram_word(g_ram, 0x0bad);
      const int dy = (int)read_ram_word(g_ram, target + 8) -
          (int)read_ram_word(g_ram, 0x0bb0);
      if (abs(dx) <= 24 && abs(dy) <= 24) {
        if (walk_frames) *walk_frames = i;
        return target;
      }
      target = 0;
      frame(dx >= 0 ? SNES_PAD_RIGHT : SNES_PAD_LEFT);
    } else {
      frame(0);
    }
  }
  if (walk_frames) *walk_frames = 360;
  return target;
}

static void saber_boss_trace_prepare(const char *fixture, unsigned kind,
                                     unsigned hp, unsigned *target) {
  unsigned walk_frames = 0;
  *target = saber_boss_walk_to(fixture, kind, &walk_frames);
  check(*target != 0, "boss-death walk reaches the requested boss");
  if (!saber_boss_trace_quiet)
    printf("boss-trace setup kind=0x%02X target=0x%X walk=%u Zero=(%u,%u) Boss=(%u,%u) "
           "hp=%02X state=%02X/%02X/%02X row=%02X prot=%02X flags=%02X\n",
           g_ram[*target + 0x0a], *target, walk_frames,
           read_ram_word(g_ram, 0x0bad), read_ram_word(g_ram, 0x0bb0),
           read_ram_word(g_ram, *target + 5), read_ram_word(g_ram, *target + 8),
           g_ram[*target + 0x27], g_ram[*target + 1], g_ram[*target + 2],
           g_ram[*target + 3], g_ram[*target + 0x28], g_ram[*target + 0x35],
           g_ram[*target + 0x30]);
  MmxZeroCancel(g_ram);
  MmxSaberFrameReset();
  g_ram[*target + 0x27] = (uint8_t)hp;
  g_ram[*target + 0x35] = 0;
  saber_boss_trace_install(*target);
}

static void saber_boss_lock_to_zero(unsigned target) {
  if (!target) return;
  write_ram_word(g_ram, target + 5, read_ram_word(g_ram, 0x0bad));
  write_ram_word(g_ram, target + 8, read_ram_word(g_ram, 0x0bb0));
}

static void saber_boss_trace_finish(void) {
  saber_boss_trace_restore();
}

static SaberBossDeathResult saber_boss_trace_plain(const char *fixture,
                                                   unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  saber_boss_trace_prepare(fixture, kind, 1, &target);
  printf("boss-trace case=plain target=0x%X\n", target);
  SaberBossDeathTraceFrame trace = saber_boss_trace_frame(0, SNES_PAD_Y, true);
  saber_boss_death_result_update(&result, &trace);
  for (unsigned i = 1; i < 60; ++i) {
    trace = saber_boss_trace_frame(i, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && i >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_bypass(const char *fixture,
                                                    unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  const unsigned initial_hp = 2;
  saber_boss_trace_prepare(fixture, kind, initial_hp, &target);
  printf("boss-trace case=bypass target=0x%X\n", target);
  SaberBossDeathTraceFrame trace = saber_boss_trace_frame(0, SNES_PAD_Y, true);
  saber_boss_death_result_update(&result, &trace);
  unsigned followup = 0;
  for (unsigned i = 1; i < 60; ++i) {
    trace = saber_boss_trace_frame(i, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.first_hit) {
      followup = i + 1;
      break;
    }
  }
  check(followup != 0, "boss-death first Penguin/Mammoth slash lands");
  trace = saber_boss_trace_frame(followup, SNES_PAD_Y, true);
  saber_boss_death_result_update(&result, &trace);
  for (unsigned i = followup + 1; i < followup + 40; ++i) {
    trace = saber_boss_trace_frame(i, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && i >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
    if (result.lethal && !result.defeat_started && i >= result.lethal_frame + 12)
      break;
  }
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_ground_combo(const char *fixture,
                                                          unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;
  unsigned hits = 0;

  saber_boss_trace_prepare(fixture, kind, 4, &target);
  printf("boss-trace case=ground-combo target=0x%X\n", target);
  saber_boss_lock_to_zero(target);
  SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number++, SNES_PAD_Y,
                                                          true);
  saber_boss_death_result_update(&result, &trace);
  for (; number < 80 && hits < 1; ++number) {
    saber_boss_lock_to_zero(target);
    trace = saber_boss_trace_frame(number, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (trace.damage_calls && trace.damage_value &&
        (trace.before_hp & 127) > (trace.after_hp & 127))
      ++hits;
  }
  if (!hits) {
    saber_boss_trace_finish();
    return result;
  }
  saber_boss_lock_to_zero(target);
  trace = saber_boss_trace_frame(number++, SNES_PAD_Y, true);
  saber_boss_death_result_update(&result, &trace);
  for (; number < 100 && hits < 2; ++number) {
    saber_boss_lock_to_zero(target);
    trace = saber_boss_trace_frame(number, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (trace.damage_calls && trace.damage_value &&
        (trace.before_hp & 127) > (trace.after_hp & 127))
      ++hits;
  }
  if (hits >= 2 && !result.lethal) {
    saber_boss_lock_to_zero(target);
    trace = saber_boss_trace_frame(number++, SNES_PAD_Y, true);
    saber_boss_death_result_update(&result, &trace);
  }
  for (; number < 150; ++number) {
    saber_boss_lock_to_zero(target);
    trace = saber_boss_trace_frame(number, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  printf("boss-trace ground-combo hits=%u\n", hits);
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_air(const char *fixture,
                                                 unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  saber_boss_trace_prepare(fixture, kind, 1, &target);
  printf("boss-trace case=air target=0x%X\n", target);
  for (unsigned number = 0; number < 100; ++number) {
    saber_boss_lock_to_zero(target);
    unsigned input = number < 30 ? SNES_PAD_B : 0;
    if (number == 2) input |= SNES_PAD_Y;
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, input,
                                                            number == 2);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_dash(const char *fixture,
                                                  unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;
  bool started = false;

  saber_boss_trace_prepare(fixture, kind, 1, &target);
  printf("boss-trace case=dash target=0x%X\n", target);
  write_ram_word(g_ram, target + 5, read_ram_word(g_ram, 0x0bad) + 120);
  write_ram_word(g_ram, target + 8, read_ram_word(g_ram, 0x0bb0));
  frame(0);
  for (; number < 60 && !started; ++number) {
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(
        number, SNES_PAD_A | SNES_PAD_RIGHT, false);
    saber_boss_death_result_update(&result, &trace);
    started = g_ram[0xbaa] == 0x14 && saber_test_grounded();
  }
  if (!started) {
    saber_boss_trace_finish();
    return result;
  }
  bool attack_started = false;
  for (; number < 130; ++number) {
    saber_boss_lock_to_zero(target);
    unsigned input = SNES_PAD_A | SNES_PAD_RIGHT;
    if (!attack_started) {
      input |= SNES_PAD_Y;
      attack_started = true;
    }
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, input,
                                                            input & SNES_PAD_Y);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_charged_buster(
    const char *fixture, unsigned kind, unsigned charge_frames) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;

  saber_boss_trace_prepare(fixture, kind, 1, &target);
  if (charge_frames >= 90)
    g_ram[0x1f99] |= 2; /* X1 arm upgrade: use the Saber-mode charge chain. */
  printf("boss-trace case=charged-buster charge=%u target=0x%X\n",
         charge_frames, target);
  for (; number < charge_frames; ++number) {
    saber_boss_lock_to_zero(target);
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, SNES_PAD_X,
                                                            false);
    saber_boss_death_result_update(&result, &trace);
  }
  for (; number < charge_frames + 220; ++number) {
    saber_boss_lock_to_zero(target);
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(
        number, number == charge_frames ? 0 : 0, number == charge_frames);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  return result;
}

typedef struct SaberBossSweepStats {
  unsigned cases;
  unsigned contacts;
  unsigned lethal;
  unsigned defeats;
  unsigned failures;
  unsigned positive_non_bypass;
  unsigned bypass;
} SaberBossSweepStats;

static bool saber_boss_result_ok(const SaberBossDeathResult *result) {
  return result->lethal && result->defeat_started &&
      result->defeat_frame <= result->lethal_frame + SABER_BOSS_DEFEAT_N &&
      (result->final_hp & 127) == 0 && result->final_state == 6;
}

static SaberBossDeathResult saber_boss_trace_plain_offset(
    const char *fixture, unsigned kind, unsigned offset) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;

  saber_boss_trace_prepare(fixture, kind, 1, &target);
  for (; number < offset; ++number)
    (void)saber_boss_trace_frame(number, 0, false);
  for (; number < offset + 120 && !result.lethal; ++number) {
    saber_boss_lock_to_zero(target);
    const MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
    const unsigned input = number == offset ||
        (snapshot.phase == SABER_PHASE_IDLE && number > offset + 6) ?
        SNES_PAD_Y : 0;
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, input,
                                                            false);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started &&
        number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  result.final_live = target ? g_ram[target] : 0;
  result.final_hp = target ? g_ram[target + 0x27] : 0;
  result.final_state = target ? g_ram[target + 1] : 0;
  result.final_substate = target ? g_ram[target + 2] : 0;
  result.final_action = target ? g_ram[target + 3] : 0;
  result.final_protection = target ? g_ram[target + 0x35] : 0;
  saber_boss_trace_finish();
  return result;
}

static SaberBossDeathResult saber_boss_trace_priority_offset(
    const char *fixture, unsigned kind, unsigned offset, unsigned delay) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;

  saber_boss_trace_prepare(fixture, kind, 2, &target);
  for (; number < offset; ++number) {
    (void)saber_boss_trace_frame(number, 0, false);
  }
  SaberBossDeathTraceFrame trace;
  for (; number < offset + 120 && !result.first_hit; ++number) {
    saber_boss_lock_to_zero(target);
    const MmxSaberAttackSnapshot snapshot = MmxSaberAttackSnapshotGet();
    const unsigned input = number == offset ||
        (snapshot.phase == SABER_PHASE_IDLE && number > offset + 6) ?
        SNES_PAD_Y : 0;
    trace = saber_boss_trace_frame(number, input, false);
    saber_boss_death_result_update(&result, &trace);
  }
  if (result.first_hit) {
    g_ram[target + 0x27] = 1;
    for (unsigned i = 0; i < delay; ++i, ++number) {
      saber_boss_lock_to_zero(target);
      trace = saber_boss_trace_frame(number, 0, false);
      saber_boss_death_result_update(&result, &trace);
    }
    saber_boss_lock_to_zero(target);
    trace = saber_boss_trace_frame(number++, SNES_PAD_Y, false);
    saber_boss_death_result_update(&result, &trace);
    for (; number < offset + 180; ++number) {
      saber_boss_lock_to_zero(target);
      trace = saber_boss_trace_frame(number, 0, false);
      saber_boss_death_result_update(&result, &trace);
      if (result.defeat_started &&
          number >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
        break;
      if (result.lethal && !result.defeat_started &&
          number >= result.lethal_frame + 12)
        break;
    }
  }
  result.final_live = target ? g_ram[target] : 0;
  result.final_hp = target ? g_ram[target + 0x27] : 0;
  result.final_state = target ? g_ram[target + 1] : 0;
  result.final_substate = target ? g_ram[target + 2] : 0;
  result.final_action = target ? g_ram[target + 3] : 0;
  result.final_protection = target ? g_ram[target + 0x35] : 0;
  saber_boss_trace_finish();
  return result;
}

static void saber_boss_sweep_report(const char *label, unsigned offset,
                                    unsigned delay,
                                    const SaberBossDeathResult *result,
                                    SaberBossSweepStats *stats) {
  const bool failure = result->lethal && !saber_boss_result_ok(result);
  const bool contact = result->first_hit || result->lethal;
  ++stats->cases;
  if (contact) ++stats->contacts;
  if (result->lethal) ++stats->lethal;
  if (result->defeat_started) ++stats->defeats;
  if (failure) ++stats->failures;
  if (result->lethal && result->lethal_response_original > 0 &&
      result->lethal_response_value == result->lethal_response_original)
    ++stats->positive_non_bypass;
  if (result->lethal && result->lethal_response_original == 0 &&
      result->lethal_response_value != 0)
    ++stats->bypass;
  printf("boss-sweep case=%s offset=%u delay=%u contact=%u lethal=%u defeat=%u "
         "failure=%u lethal-state=%02X/%02X/%02X prot=%02X row=%02X "
         "callback=%02X/%02X/%02X prot=%02X response=%u->%u damage=%u->%u "
         "final=%u/%02X/%02X/%02X\n",
         label, offset, delay, contact, result->lethal,
         result->defeat_started, failure, result->lethal_before_state,
         result->lethal_before_substate, result->lethal_before_action,
         result->lethal_before_protection, result->lethal_before_row,
         result->lethal_damage_state, result->lethal_damage_substate,
         result->lethal_damage_action, result->lethal_damage_protection,
         result->lethal_response_original, result->lethal_response_value,
         result->lethal_damage_original, result->lethal_damage_value,
         result->final_live, result->final_hp, result->final_state,
         result->final_substate);
}

static void saber_boss_print_result(const char *label,
                                    const SaberBossDeathResult *result);

static SaberBossDeathResult saber_boss_trace_buster_bypass(const char *fixture,
                                                           unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target;
  unsigned number = 0;

  /* A full Saber-mode charge emits maximum shot 1, then maximum shot 2 after
   * the first burst retires. Charge before walking back to the boss: the
   * native max-shot release has a real flight time, so the fixture's live
   * boss must be near Zero at the release edge. */
  saber_boss_trace_prepare(fixture, kind, 32, &target);
  g_ram[0x1f99] |= 2; /* X1 arm upgrade: retain the Saber-mode charge chain. */
  printf("boss-trace case=buster-bypass target=0x%X\n", target);
  for (unsigned i = 0; i < 360 && MmxZeroGetState().charge < 200;
       ++i, ++number) {
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, SNES_PAD_X,
                                                            false);
    saber_boss_death_result_update(&result, &trace);
  }
  check(MmxZeroGetState().charge >= 200,
        "boss-death buster bypass reaches a full live Saber-mode charge");

  /* Keep the charged state live while the real fixture walk closes the final
   * gap. The last direction also makes the native burst face the boss. */
  for (unsigned i = 0; i < 360; ++i, ++number) {
    const int dx = (int)read_ram_word(g_ram, target + 5) -
        (int)read_ram_word(g_ram, 0x0bad);
    const int dy = (int)read_ram_word(g_ram, target + 8) -
        (int)read_ram_word(g_ram, 0x0bb0);
    unsigned input = SNES_PAD_X;
    if (abs(dx) > 20) input |= dx >= 0 ? SNES_PAD_RIGHT : SNES_PAD_LEFT;
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, input,
                                                            false);
    saber_boss_death_result_update(&result, &trace);
    if (abs(dx) <= 24 && abs(dy) <= 24 && g_ram[target + 1] == 4 &&
        g_ram[target + 2] == 0)
      break;
  }
  check(abs((int)read_ram_word(g_ram, target + 5) -
            (int)read_ram_word(g_ram, 0x0bad)) <= 24 &&
        abs((int)read_ram_word(g_ram, target + 8) -
            (int)read_ram_word(g_ram, 0x0bb0)) <= 24,
        "boss-death buster bypass walks the charged Zero onto Penguin");

  /* The first low-priority X shot above is deliberately nonlethal. Reset the
   * fixture HP at the real max-shot release edge: max shot 1 then max shot 2
   * must be the two visible damage events in this asserted kill. */
  result = (SaberBossDeathResult){0};
  g_ram[target + 0x27] = 6;
  g_ram[target + 0x35] = 0;
  printf("boss-trace buster-bypass-release target=0x%X Zero=(%u,%u) "
         "Boss=(%u,%u) hp=%02X\n", target, read_ram_word(g_ram, 0x0bad),
         read_ram_word(g_ram, 0x0bb0), read_ram_word(g_ram, target + 5),
         read_ram_word(g_ram, target + 8), g_ram[target + 0x27]);
  {
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number++, 0, true);
    saber_boss_death_result_update(&result, &trace);
  }
  for (unsigned i = 0; i < 17; ++i, ++number) {
    const int side = g_ram[0x0c11] & 0x40 ? 40 : -40;
    write_ram_word(g_ram, target + 5,
                   (unsigned)((int)read_ram_word(g_ram, 0x0bad) + side));
    write_ram_word(g_ram, target + 8, read_ram_word(g_ram, 0x0bb0));
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, 0, false);
    saber_boss_death_result_update(&result, &trace);
  }
  {
    const int side = g_ram[0x0c11] & 0x40 ? 40 : -40;
    write_ram_word(g_ram, target + 5,
                   (unsigned)((int)read_ram_word(g_ram, 0x0bad) + side));
    write_ram_word(g_ram, target + 8, read_ram_word(g_ram, 0x0bb0));
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number++, SNES_PAD_X,
                                                            true);
    saber_boss_death_result_update(&result, &trace);
  }
  for (unsigned i = 0; i < 80; ++i, ++number) {
    const int side = g_ram[0x0c11] & 0x40 ? 40 : -40;
    write_ram_word(g_ram, target + 5,
                   (unsigned)((int)read_ram_word(g_ram, 0x0bad) + side));
    write_ram_word(g_ram, target + 8, read_ram_word(g_ram, 0x0bb0));
    SaberBossDeathTraceFrame trace = saber_boss_trace_frame(number, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.defeat_started && i >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  saber_boss_print_result("penguin-buster-bypass", &result);
  return result;
}

static void saber_boss_trace_finisher(const char *fixture, unsigned kind) {
  SaberBossDeathResult result = {0};
  unsigned target = 0;
  unsigned walk_frames = 0;
  bool opened = false;

  load_response_fixture(fixture);
  MmxSaberFrameReset();
  for (unsigned i = 0; i < 360 && !target; ++i) {
    target = saber_boss_near_player(kind);
    if (target) {
      const int dx = (int)read_ram_word(g_ram, target + 5) -
          (int)read_ram_word(g_ram, 0x0bad);
      const int dy = (int)read_ram_word(g_ram, target + 8) -
          (int)read_ram_word(g_ram, 0x0bb0);
      if (abs(dx) <= 24 && abs(dy) <= 24) {
        walk_frames = i;
        break;
      }
      target = 0;
      frame(dx >= 0 ? SNES_PAD_RIGHT : SNES_PAD_LEFT);
    } else {
      frame(0);
    }
  }
  check(target != 0, "boss-death finisher walk reaches Penguin");
  g_ram[target + 0x27] = 32;
  g_ram[target + 0x35] = 0;
  release_charge_button(150, SNES_PAD_X);
  for (unsigned i = 0; i < 240 &&
       (MmxZeroGetState().burst || MmxZeroGetState().shot_mask || g_ram[0xc25]); ++i)
    frame(0);
  frame(SNES_PAD_X);
  if (MmxSaberComboWindowTicks()) opened = true;
  for (unsigned i = 0; i < 120 && !opened; ++i) {
    frame(0);
    opened = MmxSaberComboWindowTicks() != 0;
  }
  printf("boss-trace finisher-setup target=0x%X walk=%u opened=%u window=%u "
         "combo=%u burst=%u shotmask=%02X c25=%02X hp=%02X "
         "state=%02X/%02X/%02X prot=%02X\n", target, walk_frames, opened,
         MmxSaberComboWindowTicks(), MmxZeroGetState().combo,
         MmxZeroGetState().burst, MmxZeroGetState().shot_mask, g_ram[0xc25],
         g_ram[target + 0x27], g_ram[target + 1], g_ram[target + 2],
         g_ram[target + 3], g_ram[target + 0x35]);
  if (!opened || !g_ram[target]) {
    puts("boss-trace result=penguin-finisher unreachable");
    puts("boss-trace result=penguin-wave unreachable (finisher window not opened)");
    return;
  }
  g_ram[target + 0x27] = 1;
  g_ram[target + 0x35] = 0;
  saber_boss_trace_install(target);
  printf("boss-trace case=finisher target=0x%X\n", target);
  SaberBossDeathTraceFrame trace = saber_boss_trace_frame(0, SNES_PAD_Y, true);
  saber_boss_death_result_update(&result, &trace);
  for (unsigned i = 1; i < 90; ++i) {
    trace = saber_boss_trace_frame(i, 0, false);
    saber_boss_death_result_update(&result, &trace);
    if (result.lethal && result.defeat_started &&
        i >= result.defeat_frame + SABER_BOSS_DEFEAT_N)
      break;
  }
  saber_boss_trace_finish();
  saber_boss_print_result("penguin-finisher", &result);
}

static void saber_boss_print_result(const char *label,
                                    const SaberBossDeathResult *result) {
  printf("boss-trace result=%s first=%u bypass=%u lethal=%u defeat=%u "
         "frames=first:%u lethal:%u defeat:%u final=live:%u hp:%02X "
         "state:%02X/%02X/%02X prot:%02X classes=first:%u bypass:%u lethal:%u\n",
         label, result->first_hit, result->bypass_hit, result->lethal,
         result->defeat_started, result->first_hit_frame, result->lethal_frame,
         result->defeat_frame, result->final_live, result->final_hp,
         result->final_state, result->final_substate, result->final_action,
         result->final_protection, result->first_class, result->bypass_class,
         result->lethal_class);
}

static void saber_boss_death_checks(const char *fixture_dir) {
  char penguin[4096];
  char mammoth[4096];
  char armadillo[4096];
  check(snprintf(penguin, sizeof(penguin), "%s/%s", fixture_dir,
                 "penguin-fight.sav") < (int)sizeof(penguin),
        "boss-death Penguin fixture path fits");
  check(snprintf(mammoth, sizeof(mammoth), "%s/%s", fixture_dir,
                 "mammoth-fight.sav") < (int)sizeof(mammoth),
        "boss-death Mammoth fixture path fits");
  check(snprintf(armadillo, sizeof(armadillo), "%s/%s", fixture_dir,
                 "armadillo-fight.sav") < (int)sizeof(armadillo),
        "boss-death Armadillo fixture path fits");
  check(readable_file(penguin) && readable_file(mammoth) &&
            readable_file(armadillo),
        "boss-death Penguin, Mammoth, and Armadillo fixtures exist");

  const SaberBossDeathResult penguin_plain = saber_boss_trace_plain(penguin, 0x02);
  const SaberBossDeathResult penguin_bypass = saber_boss_trace_bypass(penguin, 0x02);
  const SaberBossDeathResult penguin_combo =
      saber_boss_trace_ground_combo(penguin, 0x02);
  const SaberBossDeathResult penguin_air = saber_boss_trace_air(penguin, 0x02);
  const SaberBossDeathResult penguin_dash = saber_boss_trace_dash(penguin, 0x02);
  const SaberBossDeathResult penguin_small_buster =
      saber_boss_trace_charged_buster(penguin, 0x02, 30);
  const SaberBossDeathResult penguin_full_buster =
      saber_boss_trace_charged_buster(penguin, 0x02, 90);
  const SaberBossDeathResult mammoth_bypass = saber_boss_trace_bypass(mammoth, 0);
  const SaberBossDeathResult penguin_buster_bypass =
      saber_boss_trace_buster_bypass(penguin, 0x02);
  saber_boss_trace_finisher(penguin, 0x02);
  saber_boss_print_result("penguin-plain", &penguin_plain);
  saber_boss_print_result("penguin-bypass", &penguin_bypass);
  saber_boss_print_result("penguin-ground-combo", &penguin_combo);
  saber_boss_print_result("penguin-air", &penguin_air);
  saber_boss_print_result("penguin-dash", &penguin_dash);
  saber_boss_print_result("penguin-small-buster", &penguin_small_buster);
  saber_boss_print_result("penguin-full-buster", &penguin_full_buster);
  saber_boss_print_result("mammoth-bypass", &mammoth_bypass);
  check(penguin_plain.lethal && penguin_plain.defeat_started &&
      penguin_plain.defeat_frame <= penguin_plain.lethal_frame + SABER_BOSS_DEFEAT_N &&
      (penguin_plain.final_hp & 127) == 0 && penguin_plain.final_state == 6,
      "boss-death Penguin plain slash starts native defeat and stops alive state");
  check(penguin_bypass.first_hit && penguin_bypass.bypass_hit && penguin_bypass.lethal &&
      penguin_bypass.defeat_started &&
      penguin_bypass.defeat_frame <= penguin_bypass.lethal_frame + SABER_BOSS_DEFEAT_N &&
      (penguin_bypass.final_hp & 127) == 0 && penguin_bypass.final_state == 6,
      "boss-death Penguin priority-bypass kill starts native defeat and stops alive state");
  check(saber_boss_result_ok(&penguin_combo),
      "boss-death Penguin ground 1/2/3 combo reaches native defeat");
  check(saber_boss_result_ok(&penguin_air) &&
            penguin_air.lethal_class == MMX_SABER_PRIORITY_CLASS_AIR,
      "boss-death Penguin air slash reaches native defeat");
  check(saber_boss_result_ok(&penguin_small_buster) &&
            penguin_small_buster.lethal_class ==
                MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL,
      "boss-death Penguin small buster reaches native defeat");
  check(saber_boss_result_ok(&penguin_full_buster),
      "boss-death Penguin full buster reaches native defeat");
  check(mammoth_bypass.first_hit && mammoth_bypass.bypass_hit && mammoth_bypass.lethal &&
      mammoth_bypass.defeat_started &&
      mammoth_bypass.defeat_frame <= mammoth_bypass.lethal_frame + SABER_BOSS_DEFEAT_N &&
      (mammoth_bypass.final_hp & 127) == 0 && mammoth_bypass.final_state == 6,
      "boss-death Mammoth priority-bypass control starts native defeat");
  check(penguin_buster_bypass.first_hit && penguin_buster_bypass.bypass_hit &&
      penguin_buster_bypass.first_class == MMX_SABER_PRIORITY_CLASS_MAX_SHOT1 &&
      penguin_buster_bypass.bypass_class == MMX_SABER_PRIORITY_CLASS_MAX_SHOT2 &&
      penguin_buster_bypass.lethal && penguin_buster_bypass.defeat_started &&
      penguin_buster_bypass.defeat_frame <=
          penguin_buster_bypass.lethal_frame + SABER_BOSS_DEFEAT_N &&
      (penguin_buster_bypass.final_hp & 127) == 0 &&
      penguin_buster_bypass.final_state == 6,
      "boss-death Penguin max-shot buster bypass starts native defeat");

  /* The first pass varies the native attack-pattern offset over 960 frames
   * (two complete 480-frame pattern windows observed in the trace), then
   * varies the follow-up timing around the known protection lifetime. Every
   * trial starts at HP 2, takes a real first ground hit, changes HP to 1, and
   * lands the second ground hit. It is intentionally quiet at frame level so
   * the one-line rows below remain a permanent failure table. */
  static const unsigned pattern_offsets[] = {0, 240, 480, 720, 960};
  static const unsigned followup_delays[] = {0, 10, 20};
  SaberBossSweepStats penguin_sweep = {0};
  saber_boss_trace_quiet = true;
  for (unsigned i = 0;
       i < sizeof(pattern_offsets) / sizeof(pattern_offsets[0]); ++i) {
    const SaberBossDeathResult result = saber_boss_trace_plain_offset(
        penguin, 0x02, pattern_offsets[i]);
    saber_boss_sweep_report("penguin-ground-plain", pattern_offsets[i], 0,
                            &result, &penguin_sweep);
  }
  for (unsigned i = 0;
       i < sizeof(pattern_offsets) / sizeof(pattern_offsets[0]); ++i) {
    const unsigned delay = i == 0 ? 0 : 20;
    const SaberBossDeathResult result = saber_boss_trace_priority_offset(
        penguin, 0x02, pattern_offsets[i], delay);
    saber_boss_sweep_report("penguin-ground-priority", pattern_offsets[i],
                            delay, &result, &penguin_sweep);
  }
  for (unsigned i = 0;
       i < sizeof(followup_delays) / sizeof(followup_delays[0]); ++i) {
    const SaberBossDeathResult result = saber_boss_trace_priority_offset(
        penguin, 0x02, 0, followup_delays[i]);
    saber_boss_sweep_report("penguin-ground-priority-delay", 0,
                            followup_delays[i], &result, &penguin_sweep);
  }
  saber_boss_trace_quiet = false;
  printf("boss-sweep summary=Penguin cases=%u contacts=%u lethal=%u defeats=%u "
         "failures=%u positive=%u bypass=%u offsets=0..960 delays=0,10,20\n",
         penguin_sweep.cases, penguin_sweep.contacts, penguin_sweep.lethal,
         penguin_sweep.defeats, penguin_sweep.failures,
         penguin_sweep.positive_non_bypass, penguin_sweep.bypass);
  check(penguin_sweep.contacts >= 6 && penguin_sweep.lethal >= 6,
        "boss-death Penguin offset sweep lands lethal Saber follow-ups");
  check(penguin_sweep.failures == 0,
        "boss-death Penguin offset sweep has no HP-zero live failures");
  check(penguin_sweep.positive_non_bypass > 0 && penguin_sweep.bypass > 0,
        "boss-death Penguin sweep covers positive native and bypass Saber kills");

  /* Short controls sample the same frame-offset sweep on two other bosses;
   * Armadillo uses HP 1/plain damage because its priority combo window is
   * not open during the fixture's vulnerable roll. Their native protection
   * paths must remain untouched. */
  static const unsigned control_offsets[] = {80, 160, 240};
  SaberBossSweepStats mammoth_sweep = {0};
  SaberBossSweepStats armadillo_sweep = {0};
  saber_boss_trace_quiet = true;
  for (unsigned i = 0;
       i < sizeof(control_offsets) / sizeof(control_offsets[0]); ++i) {
    SaberBossDeathResult result = saber_boss_trace_priority_offset(
        mammoth, 0, control_offsets[i], 20);
    saber_boss_sweep_report("mammoth-ground-priority", control_offsets[i], 20,
                            &result, &mammoth_sweep);
    result = saber_boss_trace_plain_offset(armadillo, 0x14,
                                           control_offsets[i]);
    saber_boss_sweep_report("armadillo-ground-plain", control_offsets[i], 0,
                            &result, &armadillo_sweep);
  }
  saber_boss_trace_quiet = false;
  printf("boss-sweep summary=Mammoth cases=%u contacts=%u lethal=%u defeats=%u failures=%u\n",
         mammoth_sweep.cases, mammoth_sweep.contacts, mammoth_sweep.lethal,
         mammoth_sweep.defeats, mammoth_sweep.failures);
  printf("boss-sweep summary=Armadillo cases=%u contacts=%u lethal=%u defeats=%u failures=%u\n",
         armadillo_sweep.cases, armadillo_sweep.contacts, armadillo_sweep.lethal,
         armadillo_sweep.defeats, armadillo_sweep.failures);
  check(mammoth_sweep.contacts && mammoth_sweep.lethal &&
            mammoth_sweep.failures == 0,
        "boss-death Mammoth short offset sweep still dies normally");
  check(armadillo_sweep.contacts && armadillo_sweep.lethal &&
            armadillo_sweep.failures == 0,
        "boss-death Armadillo short offset sweep still dies normally");
  puts("ok: saber-boss-death");
}

int main(int argc, char **argv) {
  check(argc == 2, "X1 ROM supplied");
  const char *fixture = getenv("MMX_ZERO_TEST_FIXTURE");
  const char *x3_rom = getenv("MMX_COOP_X3_ROM");
  const char *assets = getenv("MMX_ZERO_TEST_ASSETS");
  const char *only = getenv("MMX_SABER_TEST_ONLY");
  const char *fixture_dir = getenv("MMX_SABER_FIXTURE_DIR");
  check(fixture && fixture[0], "MMX_ZERO_TEST_FIXTURE supplied");
  check(x3_rom && x3_rom[0], "MMX_COOP_X3_ROM supplied");
  check(assets && assets[0], "MMX_ZERO_TEST_ASSETS supplied");
  if (only && (!strcmp(only, "fixtures") || !strcmp(only, "saber-wall") ||
      !strcmp(only, "saber-cancel") || !strcmp(only, "saber-buster-rules") ||
      !strcmp(only, "saber-damage") || !strcmp(only, "saber-priority") ||
      !strcmp(only, "saber-armadillo") ||
      !strcmp(only, "saber-ride-pilot") ||
      !strcmp(only, "saber-contexts") ||
      !strcmp(only, "saber-boss-death")))
    check(fixture_dir && fixture_dir[0], "MMX_SABER_FIXTURE_DIR supplied");

  SDL_SetMainReady();
  check(snesrecomp_sdl_init(SDL_INIT_EVENTS), "SDL initializes");
  g_audio_mutex = SDL_CreateMutex();
  check(g_audio_mutex != NULL, "audio mutex initializes");
  static const SnesDesktopHostGame game = {
    .display_name = "MMX Saber ROM reference",
    .build_version = "saber-rom-reference-1",
    .num_players = 1,
    .before_run_frame = MmxBeforeFrame,
    .native_widescreen = 0,
    .state_menu_hotkeys = 1,
    .prepare_frame = MmxPrepareFrame,
    .begin_sim_frame = MmxBeginFrame,
    .end_sim_frame = MmxEndFrame,
    .draw_frame = MmxDrawFrame,
  };
  g_game = &game;
  ConfigUseStateMenuDefaults();
  char test_config_path[1024];
  const char *test_tmp = getenv("TMP");
  if (test_tmp && test_tmp[0]) {
    int config_path_length =
        snprintf(test_config_path, sizeof(test_config_path),
                 "%s/saber-test-config.ini", test_tmp);
    check(config_path_length > 0 &&
              (size_t)config_path_length < sizeof(test_config_path),
          "temporary test config path fits");
  } else {
    snprintf(test_config_path, sizeof(test_config_path),
             "saber-test-config.ini");
  }
  FILE *config = fopen(test_config_path, "w");
  check(config != NULL, "temporary test config opens");
  fputs("[Graphics]\nDisplayAspect=8:7\n", config);
  check(fclose(config) == 0, "temporary test config closes");
  ParseConfigFile(test_config_path);
  check(remove(test_config_path) == 0, "temporary test config removes");
  g_config.new_renderer = true;
  g_config.widescreen = false;

  FILE *rom_file = fopen(argv[1], "rb");
  check(rom_file != NULL, "X1 ROM opens");
  check(fseek(rom_file, 0, SEEK_END) == 0, "X1 ROM seeks");
  long rom_size = ftell(rom_file);
  check(rom_size > 0, "X1 ROM has content");
  check(fseek(rom_file, 0, SEEK_SET) == 0, "X1 ROM rewinds");
  uint8 *rom = malloc((size_t)rom_size);
  check(rom != NULL, "X1 ROM allocates");
  check(fread(rom, 1, (size_t)rom_size, rom_file) == (size_t)rom_size,
        "X1 ROM reads");
  check(fclose(rom_file) == 0, "X1 ROM closes");
  if ((rom_size & 0x7fff) == 512) {
    rom_size -= 512;
    memmove(rom, rom + 512, (size_t)rom_size);
  }
  g_mmx_custom_renderer = true;
  MmxRendererSetRom(rom, (size_t)rom_size);
  g_last_drawable_width = 1280;
  g_last_drawable_height = 720;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer;
  RtlRegisterGame(&kMmxGameInfo);
  check(SnesInit(rom, (size_t)rom_size) != NULL, "X1 game initializes");
  g_spc_player = SmwSpcPlayer_Create();
  check(g_spc_player != NULL, "SPC player initializes");
  g_spc_player->initialize(g_spc_player);

  const bool saber_package = only && !strcmp(only, "saber-package");
  const bool saber_finisher = only && !strcmp(only, "saber-finisher");
  const bool saber_tuning = only && !strcmp(only, "saber-tuning");
  const bool saber_hitbox_debug = only && !strcmp(only, "saber-hitbox-debug");
  const bool saber_damage = only && !strcmp(only, "saber-damage");
  const bool zero_extension = only && !strcmp(only, "zero-extension");
  const bool saber_input = only && !strcmp(only, "saber-input");
  const bool saber_ground_1 = only && !strcmp(only, "saber-ground-1");
  const bool saber_ground_combo = only && !strcmp(only, "saber-ground-combo");
  const bool saber_air = only && !strcmp(only, "saber-air");
  const bool saber_wall = only && !strcmp(only, "saber-wall");
  const bool saber_dash = only && !strcmp(only, "saber-dash");
  const bool saber_cancel = only && !strcmp(only, "saber-cancel");
  const bool saber_land = only && !strcmp(only, "saber-land");
  const bool saber_ground_lifecycle = only && !strcmp(only, "saber-ground-lifecycle");
  const bool saber_lifecycle_load = only && !strcmp(only, "saber-lifecycle-load");
  const bool saber_contexts = only && !strcmp(only, "saber-contexts");
  const bool saber_buster_rules = only && !strcmp(only, "saber-buster-rules");
  const bool saber_burst_height = only && !strcmp(only, "saber-burst-height");
  const bool saber_priority_classify =
      only && !strcmp(only, "saber-priority-classify");
  const bool saber_priority = only && !strcmp(only, "saber-priority");
  const bool saber_armadillo = only && !strcmp(only, "saber-armadillo");
  const bool saber_wave_travel = only && !strcmp(only, "saber-wave-travel");
  const bool saber_wave_damage = only && !strcmp(only, "saber-wave-damage");
  const bool saber_wave_lifecycle = only && !strcmp(only, "saber-wave-lifecycle");
  const bool saber_wave_render = only && !strcmp(only, "saber-wave-render");
  const bool saber_ground_hit = only && !strcmp(only, "saber-ground-hit");
  const bool saber_render_snapshot = only && !strcmp(only, "saber-render-snapshot");
  const bool saber_ride_pilot = only && !strcmp(only, "saber-ride-pilot");
  const bool saber_boss_death = only && !strcmp(only, "saber-boss-death");
  const bool x3_zero_specials = only && !strcmp(only, "x3-zero-specials");
  const bool zero_hook_parity = only && !strcmp(only, "zero-hook-parity");
  const bool zero_response_seam = only && !strcmp(only, "zero-response-seam");
  const bool fixtures = only && !strcmp(only, "fixtures");
  const bool saber_enabled_group = saber_package || zero_extension ||
      saber_input || saber_ground_1 || saber_ground_combo ||
      saber_air || saber_wall || saber_dash || saber_land || saber_ground_lifecycle ||
      saber_ground_hit || saber_cancel || saber_lifecycle_load ||
      saber_contexts ||
      saber_render_snapshot || saber_buster_rules || saber_burst_height ||
      saber_finisher || saber_priority_classify ||
      saber_priority || saber_armadillo ||
      saber_tuning || saber_hitbox_debug || saber_damage ||
      saber_wave_travel || saber_wave_damage || saber_wave_lifecycle ||
      saber_wave_render || saber_ride_pilot || saber_boss_death;
  SpecialCounts upstream_specials = {0};
  BurstHeight upstream_burst[2] = {{0}};
  BurstHeight upstream_airborne = {0};
  if (saber_input || saber_buster_rules || saber_burst_height) {
    activate_zero(argv[1], x3_rom, assets, false, false);
    if (saber_input)
      upstream_specials = x3_zero_specials_checks(fixture);
    else if (saber_buster_rules)
      x3_charge_checks(fixture, SNES_PAD_Y);
    else
      upstream_burst_height_checks(fixture, upstream_burst,
                                   &upstream_airborne);
    activate_zero(argv[1], x3_rom, assets, true, true);
  } else if (zero_hook_parity) {
    check(set_test_env("SNESRECOMP_LLE_BOUNCE", "0") == 0,
          "zero-hook-parity forces the interpreted task path");
    activate_zero(argv[1], x3_rom, assets, false, false);
  } else {
    activate_zero(argv[1], x3_rom, assets, saber_enabled_group,
                  saber_enabled_group);
  }
  if (saber_input) {
    check(MmxSaberEnabled(), "saber-input runs with the Saber package enabled");
    saber_input_checks(fixture, &upstream_specials);
  } else if (saber_ground_1) {
    check(MmxSaberEnabled(), "saber-ground-1 runs with the Saber package enabled");
    saber_ground_1_checks(fixture);
  } else if (saber_ground_combo) {
    check(MmxSaberEnabled(),
          "saber-ground-combo runs with the Saber package enabled");
    saber_ground_combo_checks(fixture);
  } else if (saber_air) {
    check(MmxSaberEnabled(), "saber-air runs with the Saber package enabled");
    saber_air_checks(fixture);
  } else if (saber_wall) {
    check(MmxSaberEnabled(), "saber-wall runs with the Saber package enabled");
    saber_wall_checks(fixture_dir);
  } else if (saber_dash) {
    check(MmxSaberEnabled(), "saber-dash runs with the Saber package enabled");
    saber_dash_checks(fixture);
  } else if (saber_cancel) {
    check(MmxSaberEnabled(), "saber-cancel runs with the Saber package enabled");
    saber_cancel_checks(fixture, fixture_dir);
  } else if (saber_land) {
    check(MmxSaberEnabled(), "saber-land runs with the Saber package enabled");
    saber_land_checks(fixture);
  } else if (saber_ground_lifecycle) {
    check(MmxSaberEnabled(),
          "saber-ground-lifecycle runs with the Saber package enabled");
    saber_ground_lifecycle_checks(fixture);
  } else if (saber_lifecycle_load) {
    check(MmxSaberEnabled(),
          "saber-lifecycle-load runs with the Saber package enabled");
    saber_lifecycle_load_checks(argv[1], x3_rom, fixture, assets);
  } else if (saber_contexts) {
    check(MmxSaberEnabled(),
          "saber-contexts runs with the Saber package enabled");
    saber_context_checks(fixture, fixture_dir);
  } else if (saber_buster_rules) {
    check(MmxSaberEnabled(),
          "saber-buster-rules runs with the Saber package enabled");
    saber_buster_rule_checks(fixture, fixture_dir);
  } else if (saber_burst_height) {
    check(MmxSaberEnabled(),
          "saber-burst-height runs with the Saber package enabled");
    saber_burst_height_checks(fixture, upstream_burst, &upstream_airborne);
  } else if (saber_finisher) {
    check(MmxSaberEnabled(),
          "saber-finisher runs with the Saber package enabled");
    saber_finisher_checks(fixture);
  } else if (saber_priority_classify) {
    check(MmxSaberEnabled(),
          "saber-priority-classify runs with the Saber package enabled");
    saber_priority_classify_checks(fixture);
  } else if (saber_priority) {
    check(MmxSaberEnabled(),
          "saber-priority runs with the Saber package enabled");
    saber_priority_checks(argv[1], x3_rom, fixture, assets, fixture_dir);
  } else if (saber_armadillo) {
    check(MmxSaberEnabled(),
          "saber-armadillo runs with the Saber package enabled");
    saber_armadillo_checks(argv[1], x3_rom, assets, fixture_dir);
  } else if (saber_tuning) {
    check(MmxSaberEnabled(),
          "saber-tuning runs with the Saber package enabled");
    saber_tuning_checks();
  } else if (saber_hitbox_debug) {
    check(MmxSaberEnabled(),
          "saber-hitbox-debug runs with the Saber package enabled");
    saber_hitbox_debug_checks(fixture);
    puts("ok: saber-hitbox-debug");
  } else if (saber_damage) {
    check(MmxSaberEnabled(),
          "saber-damage runs with the Saber package enabled");
    saber_damage_checks(argv[1], x3_rom, fixture, assets, fixture_dir);
  } else if (saber_wave_travel) {
    check(MmxSaberEnabled(),
          "saber-wave-travel runs with the Saber package enabled");
    saber_wave_travel_checks(argv[1], x3_rom, fixture, assets);
  } else if (saber_wave_damage) {
    check(MmxSaberEnabled(),
          "saber-wave-damage runs with the Saber package enabled");
    saber_wave_damage_checks(fixture);
  } else if (saber_wave_lifecycle) {
    check(MmxSaberEnabled(),
          "saber-wave-lifecycle runs with the Saber package enabled");
    saber_wave_lifecycle_checks(argv[1], x3_rom, fixture, assets);
  } else if (saber_wave_render) {
    check(MmxSaberEnabled(),
          "saber-wave-render runs with the Saber package enabled");
    saber_wave_render_checks(argv[1], x3_rom, fixture, assets);
  } else if (saber_ground_hit) {
    check(MmxSaberEnabled(),
          "saber-ground-hit runs with the Saber package enabled");
    saber_ground_hit_checks(fixture);
  } else if (saber_render_snapshot) {
    check(MmxSaberEnabled(),
          "saber-render-snapshot runs with the Saber package enabled");
    saber_render_snapshot_checks(fixture);
  } else if (saber_ride_pilot) {
    check(MmxSaberEnabled(),
          "saber-ride-pilot runs with the Saber package enabled");
    saber_ride_pilot_checks(fixture_dir);
  } else if (saber_boss_death) {
    check(MmxSaberEnabled(),
          "saber-boss-death runs with the Saber package enabled");
    saber_boss_death_checks(fixture_dir);
  } else if (zero_hook_parity) {
    zero_hook_parity_checks(fixture);
  } else if (zero_response_seam) {
    zero_response_seam_checks(fixture, fixture_dir);
  } else if (x3_zero_specials) {
    upstream_specials = x3_zero_specials_checks(fixture);
  } else if (fixtures) {
    check(!MmxSaberEnabled(), "fixtures run with Saber disabled");
    fixture_checks(fixture_dir);
  } else if (zero_extension) {
    check(MmxSaberEnabled(), "zero-extension runs with the Saber package enabled");
    zero_extension_checks(fixture);
    x3_plain_checks(fixture, SNES_PAD_Y);
    x3_charge_checks(fixture, SNES_PAD_Y);
    x3_hurt_checks(fixture, SNES_PAD_Y);
    x3_jump_checks(fixture, SNES_PAD_Y);
    x3_post_charge_checks(fixture, SNES_PAD_Y);
    puts("ok: zero-extension");
  } else if (saber_package) {
    check(MmxSaberEnabled(), "saber-package reports the Saber plugin enabled");
    x3_plain_checks(fixture, SNES_PAD_X);
    saber_charge_cap_checks(fixture);
    puts("ok: saber-package");
  } else {
    if (!only || !strcmp(only, "x3-plain")) x3_plain_checks(fixture, SNES_PAD_Y);
    if (!only || !strcmp(only, "x3-charge")) x3_charge_checks(fixture, SNES_PAD_Y);
    if (!only || !strcmp(only, "x3-hurt")) x3_hurt_checks(fixture, SNES_PAD_Y);
    if (!only || !strcmp(only, "x3-jump")) x3_jump_checks(fixture, SNES_PAD_Y);
    if (!only || !strcmp(only, "x3-post-charge")) x3_post_charge_checks(fixture, SNES_PAD_Y);
    if (!only || !strcmp(only, "x1-native")) native_x1_checks(fixture);
    if (!only) {
      upstream_specials = x3_zero_specials_checks(fixture);
      activate_zero(argv[1], x3_rom, assets, true, true);
      check(MmxSaberEnabled(), "default run enables the Saber package group");
      zero_extension_checks(fixture);
      MmxZeroSetExtension(MmxSaberFrameExtension());
      x3_plain_checks(fixture, SNES_PAD_X);
      saber_charge_cap_checks(fixture);
      saber_special_checks(fixture, &upstream_specials);
      saber_ground_1_checks(fixture);
      saber_ground_lifecycle_checks(fixture);
      puts("ok: saber-package");
    }
    if (only && !strcmp(only, "saber-assets")) {
      saber_assets_checks(argv[1], x3_rom, fixture, assets);
    } else if (only && strcmp(only, "x3-plain") && strcmp(only, "x3-charge") &&
        strcmp(only, "x3-hurt") && strcmp(only, "x3-jump") &&
        strcmp(only, "x3-post-charge") && strcmp(only, "x1-native") &&
        strcmp(only, "x3-zero-specials") &&
      strcmp(only, "saber-package") && strcmp(only, "saber-input") &&
      strcmp(only, "saber-finisher") &&
      strcmp(only, "saber-priority-classify") &&
      strcmp(only, "saber-priority") &&
      strcmp(only, "saber-armadillo") &&
      strcmp(only, "saber-tuning") &&
      strcmp(only, "saber-hitbox-debug") &&
      strcmp(only, "saber-damage") &&
      strcmp(only, "saber-ground-1") &&
      strcmp(only, "saber-ground-combo") &&
      strcmp(only, "saber-air") &&
      strcmp(only, "saber-wall") &&
      strcmp(only, "saber-dash") &&
      strcmp(only, "saber-cancel") &&
      strcmp(only, "saber-land") &&
      strcmp(only, "saber-ground-lifecycle") &&
      strcmp(only, "saber-lifecycle-load") &&
      strcmp(only, "saber-contexts") &&
        strcmp(only, "saber-buster-rules") &&
        strcmp(only, "saber-burst-height") &&
        strcmp(only, "saber-wave-travel") &&
        strcmp(only, "saber-wave-damage") &&
        strcmp(only, "saber-wave-lifecycle") &&
        strcmp(only, "saber-wave-render") &&
        strcmp(only, "saber-ground-hit") &&
        strcmp(only, "saber-render-snapshot") &&
        strcmp(only, "saber-ride-pilot") &&
        strcmp(only, "zero-hook-parity") &&
        strcmp(only, "zero-response-seam") &&
        strcmp(only, "zero-extension") &&
        strcmp(only, "saber-assets") &&
        strcmp(only, "fixtures")) {
      fprintf(stderr, "FAIL: unknown MMX_SABER_TEST_ONLY group: %s\n", only);
      return 1;
    }
  }
  puts("SABER ROM CHECKS PASSED");
  return 0;
}
