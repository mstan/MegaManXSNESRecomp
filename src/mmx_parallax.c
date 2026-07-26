#include "mmx_parallax.h"

#include <stdio.h>

#include "config.h"
#include "parallax.h"
#include "snes/ppu.h"

extern Ppu *g_ppu;
extern uint8_t g_ram[0x20000];
extern void WriteConfigFile(const char *filename);
extern const char *MmxParallax_ConfigPath(void);

/* ── Layer stack ──────────────────────────────────────────────────────────
 *
 * Array order IS the painter's draw order and mirrors the Mode-1 priority
 * ranks the PPU composites with (snes/ppu.c PpuDrawBackgrounds' table), so
 * occlusion matches hardware: BG1's priority-1 tiles cover priority-2 sprites,
 * priority-0 sprites hide behind the playfield, and so on. Depth (`z`) carries
 * only parallax and is deliberately SHARED between a layer and its priority
 * band (and across all four sprite bands) — a layer must never
 * parallax-split against itself, which would tear a single tilemap in two.
 *
 * MMX layer semantics (established by the widescreen work, docs/WIDESCREEN.md):
 *   BG1  the playfield — camera-anchored by the committed camera $1E6A/$1E6C.
 *        This is the focal plane the camera frames on.
 *   BG2  the parallax city/scenery behind the playfield, scrolling at a
 *        fraction of BG1. Pushing it back is what sells the effect.
 *   BG3  stage overlay effects (e.g. Launch Octopus's foreground water
 *        filter). Its priority-0 rank sits near the back of the stack, its
 *        priority-1 rank in FRONT of everything, so the two bands get very
 *        different depths.
 *   OBJ  sprites — X, enemies, and the HUD (MMX reserves OAM slots 0-15 for
 *        the HUD). Sprite screen positions already embed the camera, so the
 *        sprite planes sit at the playfield's depth to stay welded to it.
 *
 * KNOWN LIMITATION: because MMX draws its HUD with sprites rather than on a
 * dedicated BG, the life/weapon bars tilt with the OBJ plane instead of
 * staying flat on the glass. ar-recomp hit the same thing on ActRaiser and
 * solved it with a separate anchored flat-HUD pass (its diorama_hud_flat);
 * doing that here needs an OAM-range split of the OBJ capture, which is its
 * own change. Turn the OBJ group off (or parallax off) if it bothers you.
 *
 * KNOWN LIMITATION: BG3's priority-1 rank is 15 (in front of everything) when
 * $2105 bit 3 is set and 3 (near the back) when it is clear. One plane cannot
 * be in both places, so Bg3Hi is placed at the front — the configuration MMX
 * actually uses for its overlay effects. A stage that clears the bit would
 * draw that band too far forward.
 */
static const ParallaxPlaneDesc kMmxPlanes[] = {
  /* plane                    group                   z      shade r,g,b   shadow */
  { kParallaxPlane_Backdrop,  kParallaxGroup_Backdrop, 0.00f, 0.70f, 0.70f, 0.80f, false },
  { kParallaxPlane_Bg3,       kParallaxGroup_Bg3,      0.10f, 0.78f, 0.78f, 0.86f, false },
  { kParallaxPlane_Obj,       kParallaxGroup_Obj,      0.50f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Obj1,      kParallaxGroup_Obj,      0.50f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Bg2,       kParallaxGroup_Bg2,      0.20f, 0.82f, 0.82f, 0.88f, false },
  { kParallaxPlane_Bg1,       kParallaxGroup_Bg1,      0.50f, 0.92f, 0.92f, 0.95f, true  },
  { kParallaxPlane_Obj2,      kParallaxGroup_Obj,      0.51f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Bg2Hi,     kParallaxGroup_Bg2,      0.21f, 0.82f, 0.82f, 0.88f, false },
  { kParallaxPlane_Bg1Hi,     kParallaxGroup_Bg1,      0.51f, 0.92f, 0.92f, 0.95f, true  },
  { kParallaxPlane_Obj3,      kParallaxGroup_Obj,      0.52f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Bg3Hi,     kParallaxGroup_Bg3,      0.62f, 1.00f, 1.00f, 1.00f, true  },
};

static const ParallaxProfile kMmxProfile = {
  .name = "megamanx",
  .planes = kMmxPlanes,
  .plane_count = (int)(sizeof(kMmxPlanes) / sizeof(kMmxPlanes[0])),
  /* BG1, BG2, BG3 and OBJ. BG4 is never rendered in mode 1. */
  .capture_mask = (1u << kPpuOverlaySource_Bg1) |
                  (1u << kPpuOverlaySource_Bg2) |
                  (1u << kPpuOverlaySource_Bg3) |
                  (1u << kPpuOverlaySource_Obj),
};

void MmxParallax_Init(void) {
  Parallax_SetProfile(&kMmxProfile);
  g_parallax.enabled = g_config.parallax;
  if (g_parallax.enabled) {
    char state[256];
    Parallax_DescribeState(state, sizeof state);
    fprintf(stderr, "[parallax] %s\n", state);
  }
}

bool MmxParallax_IsEnabled(void) { return g_config.parallax; }

void MmxParallax_Toggle(void) {
  g_config.parallax = !g_config.parallax;
  g_parallax.enabled = g_config.parallax;
  printf("Parallax = %s\n", g_config.parallax ? "on" : "off");
  WriteConfigFile(MmxParallax_ConfigPath());
}

/* The same stage/gameplay discriminator the widescreen HUD-anchor policy uses:
 * $D1/$D2 are the stable mode pair ($C3 mirrors HDMAEN and moves during
 * effects like Spark Mandrill's light streaks, so it is NOT usable here).
 * Restricting to live stage gameplay keeps title/menu/map/boss-intro frames
 * authentic — those are not worlds, and tilting them just looks broken. */
static bool MmxParallaxSceneIsStage(void) {
  return g_ram[0x00d1] == 0x02 && g_ram[0x00d2] == 0x04;
}

/* Camera motion for the presenter's lean (Parallax_ReportCameraMotion). BG1 is
 * the playfield, so BG1's scroll registers ARE the gameplay camera.
 *
 * Scroll registers are 10-bit and wrap, so a raw frame-to-frame subtraction
 * reads a wrap as a ~1024px lurch; wrap to the shortest signed distance
 * instead. A delta larger than a plausible frame of camera travel is treated as
 * a discontinuity (room change, teleport, respawn) and reported as zero rather
 * than as a huge sweep. */
static void MmxParallaxReportMotion(void) {
  static bool have_prev;
  static int prev_x, prev_y;
  if (!g_ppu) return;
  int x = g_ppu->hScroll[0] & 0x3ff;
  int y = g_ppu->vScroll[0] & 0x3ff;
  if (!have_prev) {
    have_prev = true;
    prev_x = x;
    prev_y = y;
    Parallax_ReportCameraMotion(0.0f, 0.0f);
    return;
  }
  int dx = ((x - prev_x + 512) & 0x3ff) - 512;
  int dy = ((y - prev_y + 512) & 0x3ff) - 512;
  prev_x = x;
  prev_y = y;
  const int kMaxFrameTravel = 24;   /* px; MMX dashing is well inside this */
  if (dx > kMaxFrameTravel || dx < -kMaxFrameTravel) dx = 0;
  if (dy > kMaxFrameTravel || dy < -kMaxFrameTravel) dy = 0;
  Parallax_ReportCameraMotion((float)dx, (float)dy);
}

void MmxParallax_PrepareFrame(int frame_width, int frame_height, int extra) {
  MmxParallaxReportMotion();
  /* Mode 1 is the only mode with BG overlay capture wired up, and it is what
   * stage gameplay runs in; a mode-7 frame (boss intros, the intro stage's
   * effects) would capture nothing and present flat via the composite's own
   * "nothing drew" path, but gate it here so the RemoveFromGame captures are
   * never even declared on such a frame. */
  bool mode1 = g_ppu && PPU_mode(g_ppu) == 1;
  Parallax_PrepareFrame(g_ppu, frame_width, frame_height, extra,
                        mode1 && MmxParallaxSceneIsStage());
}
