# Mega Man X custom renderer spike

Branch: `codex/mmx-custom-renderer-spike`, based on `bbaf743`.
Tracking: central Beads `beads-8wg.1.21`.

The Super Metroid approach works for MMX's Mode-1 stage rendering. This branch
contains a playable prototype, with the old renderer still selectable for
comparison. It is not yet qualified to replace the released renderer.

## Run the prototype

From this worktree on Matthew's Windows machine:

```powershell
.\tools\run_custom_renderer.ps1
```

The launcher opens with a separate `build-custom/playtest` data directory.
On first use, the script enables the widescreen mod with the custom renderer,
Adaptive aspect, and edge-anchored HUD. Choose a ROM in the launcher, or pass
`-DirectRomPath <rom>` to boot directly. Settings, ROM selection and saves in
the original checkout are not used. Existing playtest selections are preserved.
The script supplies the MinGW runtime DLL search path; launching the bare EXE
without those DLLs on PATH may fail.

The widescreen mod offers Custom/Legacy renderer, Adaptive/16:9/21:9/32:9,
and edge/native HUD anchors. Disable the mod for stock rendering. The custom
aspect option controls presentation independently of the legacy Display aspect
setting. Fixed ratios letterbox as needed; Adaptive follows the window.

`Expanded sprite capacity` is a separate, experimental option, **off by
default**. It draws up to 2,048 pieces from the game's submitted draw queues
when the retail OAM writer runs out of slots. It applies only to the custom
renderer. Enemy allocation and behavior still run through the guest game.

Logical widths are 342, 448 and 682 pixels for 16:9, 21:9 and 32:9. Adaptive
is bounded from 4:3 to a 1024-pixel host surface (approximately 5.33:1), with
even-pixel rounding. This is a host allocation limit, not the shared PPU limit.
Gameplay simulation and presentation remain at the existing cadence; this
spike does not add Super Metroid's frame interpolation feature.

## Architecture

`src/mmx_renderer.c` adapts the Mode-1 pixel decoder/compositor design from
SuperMetroidRecomp's `src/sm_renderer.c`. The shared PPU stays at 256 pixels,
with `g_ws_active=false` and zero extra space. No shadow tilemap, periodic-fold
cache, widened PPU buffer, or margin line enhancer participates in custom
composition. The common presentation helper only copies pixels and preserves
frame-dump diagnostics.

The existing raster pass still executes once per simulated frame, retaining
MMX's IRQ/HDMA ordering. Each visible line captures registers, palette, VRAM
and OAM. The host compositor reads those snapshots without running guest code
or writing guest memory. Native terrain samples use captured VRAM; wider BG1
and BG2 samples use MMX's retained level maps and ROM metatile definitions.
BG1 uses the full camera plus raster scroll phase. BG2 uses the streamer's
retained coordinates. Stage edges reflect terrain. The existing Storm Eagle
roof exception and Launch Octopus submarine entrance mask are retained.

The generated D76A hook records actual metasprite drawing data before native
clipping. It neither widens the native OAM emission gate nor alters CPU state.
Signed host coordinates distinguish a sprite at +300 from one at -212; the
512-pixel OAM wrap ambiguity therefore does not constrain the view. Pieces
are latched before the next NMI publishes the corresponding OAM. Stage changes,
resets and state loads invalidate host observations. Recorded pieces now draw
their complete footprints, including the column at native X=255 rejected by
the retail writer. Native OAM supplies HUD and other uncaptured submissions.
Full host Y coordinates also avoid the native eight-bit wrap ambiguity.

`src/mmx_render_assets.c` resolves live animation sets to the ROM's compressed
graphics and section resource tables. For an unambiguous enemy resource whose
live tile or palette binding is stale, the compositor uses private decoded
CHR and the resource's palette. Current bindings retain live VRAM and palette
effects. This covers child animation pieces as well as the parent; it does
not draw a fixed preview icon or advance guest DMA. Unknown or ambiguous
resources keep the existing live-art path.

The optional capacity extension observes D6A7's first object handoff before
OAM exhaustion. It reads the same six priority queues, the three weapon
objects, and X, in D56F's order. The default still uses only actual D76A
observations. Both paths share the existing relational crusher-child tile
repair. Neither path adds guest objects or writes guest OAM.

HUD selection preserves MMX's first-16-slot reserve and signature-checked
boss-health runs. The custom compositor relocates those sprites before layer
composition, leaving their native positions free of duplicates. Door handling
uses the existing structural three-metatile signature, requires an adjacent
mate, and skips the authored pair in host margins while retaining native
scripted doors.

Gameplay widening remains separate from drawing. Existing enemy, projectile,
traffic and helicopter hooks use the custom view's rounded margin. Spawn
anchors retain 32 pixels of lead beyond that margin. Spark Mandrill's kind-3
mid-boss controller and kinds 0–2 keep their native scan, with independent
cursors. Vile's protected interval starts earlier when necessary to stop the
larger custom lookahead from reaching the allocation-sensitive room first.
Early guest graphics/stage streaming remains disabled in custom mode.
The Highway bee's vertical descent keeps its native player-distance trigger;
allocating its controller in the wider scan no longer advances that entrance.
The extra background view projects ROM-authored palette transitions separately
from guest CGRAM, including Highway's half-speed BG2 parallax.

## F1–F4 playtest repairs, 2026-09-14

The owner's four compatible saves are reproduced in the ignored validation
directories; source saves are unchanged. F1's turtle and F3's bee were alive
with stale graphics bindings. F2's turtle retained a palette selection from
an earlier resource allocation. F4 exposed buildings before the camera's
`$0850` palette transition. These were resource and timing problems; the
captured frames did not exhaust the retail submission budget.

- F1 and F3 now draw their live animated enemies using ROM-backed resources.
  F2's turtle uses the correct palette. F4's newly visible buildings use the
  projected palette in both 21:9 and 32:9 captures.
- All four saves pass live/offline agreement at 16:9, 21:9, 32:9 and Adaptive.
  Final runs: `mmx-render-flg1e69q`, `mmx-render-0ppkaq4_`,
  `mmx-render-zckr30vq`, and `mmx-render-p4u4iklh`, respectively.
- `mmx-render-5wytay_g` repeats all four at 32:9 with expanded capacity on.
  Its guest RAM and output images equal the capacity-off run, and all expanded
  queue entries match the observed submissions in order. A synthetic test
  with 200 submitted pieces confirms that the additional 88 pieces appear
  only when the option is enabled; the available 112 gameplay slots remain
  the cutoff when it is disabled.
- A controlled bee-boundary test (`mmx-render-ks0qm66s`) uses copies of F3
  with the bee put in waiting state and X placed on either side of the native
  distance threshold. At a distance of 160 it stays in state 2; at 96 it enters
  descent state 4, even with a 32:9 view. This is a boundary test, not a claim
  that a complete F4-to-bee route was played through. The simple walk script
  fell into the intervening gap before reaching the encounter.
- Expanded-capacity regression samples cover Chill Penguin, Highway, the
  Chill door, and the previously healthy Vile fight (`mmx-render-87xxty6j`,
  `mmx-render-c67u9alu`, `mmx-render-l7pb3jrn`). Mod-off and Legacy still produce
  stock and legacy surface widths even if the new option is stored as on
  (`mmx-render-1sjvrm3w`, `mmx-render-9b3kwxet`).

Capture format 2 records animation/resource identity and optional complete
submissions. The replay tool checks the unmodified compositor against native
PPU pixels first, then reports `repaired_native_pixels` separately. Correcting
an invisible native-area enemy intentionally changes those pixels. Raw oracle
agreement by itself never establishes that a guest graphics binding is valid.
`MMX_RENDER_OBJECT_TRACE=<csv path>` optionally records Highway bee state
transitions with player/enemy positions for timing investigations.

## Validation recorded on 2026-09-13/14

- Windows Release CMake build and all three CTests pass. Assertions remain
  enabled in Release tests. The custom renderer, replay tool and renderer test
  compile with `-Wall -Wextra -Werror`.
- Tests cover geometry through the host capacity, fixed-aspect destination
  fitting, immutable raster input, HUD relocation, opposite-side sprites
  beyond the OAM wrap boundary, reset invalidation, independent spawn cursors,
  record ownership, Vile lookahead protection and door signatures.
- Real snapshots from Highway, Chill Penguin, Spark Mandrill's late door area,
  and Sigma 1/Vile were replayed at 4:3, 16:9, 21:9 and 32:9. Sampled native
  pixels match the PPU oracle exactly, with 224 custom lines and no fallback.
  Wider images were inspected separately. This is sampled-frame evidence,
  not a full-game visual certification.
- Final Highway runs using each mod aspect selection, including Adaptive,
  produced the expected live surface size. The live image equals offline
  replay byte for byte. The 41 visible recorded metasprite pieces match native
  OAM geometry, size and attributes with zero unmatched pieces.
- Mod-off and Legacy launches produce 256- and 342-pixel surfaces respectively,
  and do not produce a custom raster capture.
- A 3,000-frame replay from the clean Vile door checkpoint entered the encounter
  and reached active cutscene/combat rendering at camera `$0A80`. Its final
  capture has zero native differences at all four ratios, including 3,518
  sprite pixels in the 32:9 margins. This does **not** establish successful
  completion of Vile's entire dialogue/recharge/final-fight sequence.

Artifacts are ignored under `build-custom/validation`. Useful runs:

| Run directory | Evidence |
| --- | --- |
| `mmx-render-fanzbzhv` | Final 32:9 Highway capture, live/replay agreement |
| `mmx-render-ewybrv6s` | Final 16:9 mod selection |
| `mmx-render-wz0gszkc` | Final 21:9 mod selection |
| `mmx-render-4mtnt6eh` | Final Adaptive mod selection |
| `mmx-render-612qanel` | Chill Penguin and Highway fixtures |
| `mmx-render-m5_rjzns` | Chill Penguin approach and boss-door fixtures |
| `mmx-render-ndg2vntg` | Spark Mandrill late door area |
| `mmx-render-udm7payt` | Vile door-to-encounter script |
| `mmx-render-a0szjd8s` | Previously healthy Vile final-fight snapshot |
| `mmx-render-43h_loc5` / `mmx-render-7_vkv17q` | Mod-off / Legacy smoke |

The original checkout's 266,643-byte v4 saves were rejected by the current
runtime. Tests use compatible 297,754-byte archived v7 saves, copied into each
test directory. No source save was converted or overwritten.

## Required before removing the legacy renderer

1. Replay Thunder Slimer's actual approach, the later Spark light streaks, and
   the complete Vile sequence at each aspect. The archived Spark save named in
   the old issue now contains the late boss-door area, so it is not evidence
   for Thunder Slimer activation.
2. Add and validate activation coverage for newly exposed columns when the
   window suddenly expands or a narrow-view save is loaded. Current hooks
   scale the normal moving-camera spawn window, but the retail scanner still
   runs at camera-column crossings. This spike does not guarantee that a
   resize cannot reveal a dormant enemy before the next scan.
3. Cover animated backgrounds, all stage-specific resource transitions,
   overlapping sprites and doors through complete opening/closing sequences.
   Background CHR and unresolved sprite resources still come from live VRAM;
   further art transitions may need additional resource ownership handling.
   Validate damage flashes and alternate palettes on repaired enemy families.
   Non-stage screens and non-Mode-1 lines
   intentionally use centered stock output. Exact native pixel agreement does
   not validate unseen margin art or encounter progression.

## Rebuild and reproduce

Use CMake with the checked-in dependency pins and a generated USA `src/gen`.
This worktree was configured with the original checkout's read-only dependency
directories (`snesrecomp` at `8d12911`, `recomp-ui` at `a7a4f30`) and copied
generated sources. CMake reapplies and verifies both the existing gameplay
overrides and the pre-clipping capture hook before building. ROM and generated
banks remain ignored. The experimental sources are wired into CMake; the old
MSVC solution has not been updated for this spike.

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-custom --parallel 6
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build-custom --output-on-failure
```

`tools/test_renderer_captures.py --help` documents isolated fixture runs and
aspect/renderer selection. `mmx_render_capture` replays a capture using the
caller-supplied ROM, compares unanchored native pixels, then writes the selected
HUD layout. Both headered and unheadered ROMs are accepted for replay.

The central Beads issue is saved locally. Its Dolt push currently fails because
the configured remote references a missing `refs/dolt/remotes/origin/dolt/data`
ref; this spike does not repair the issue database's synchronization setup.
