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
resets and state loads invalidate host observations. Native OAM remains the
source for the native footprint; recorded pieces supply margin pixels.

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
   Margin CHR currently comes from captured live VRAM; art not loaded by the
   game may need a host graphics cache. Non-stage screens and non-Mode-1 lines
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
