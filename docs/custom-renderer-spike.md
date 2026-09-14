# Mega Man X custom renderer spike

## Current owner playtest checklist (2026-09-14, fourth batch)

- [x] F1: outdoor pillar uses inverted colors before approach.
- [x] F1: Ride Armor briefly flickers to the wrong palette.
- [x] F2: preserve Adaptive width throughout death and respawn.
- [x] F3: show each boss door in the wide view without duplicate columns.
- [x] F4: Chill Penguin must wait until X enters the room through the second door.

## Previous owner playtest checklist (third batch)

- [x] F1: foreground building CHR is scrambled until X moves right.
- [x] F2: foreground building palettes change correct/wrong/correct on approach.
- [x] Reported F4 (save4): parked traffic/foreground palettes change behind X.
- [x] F8 (save7): Vile dialogue repeats into a black area in the left margin.
- [x] F9 (save8): pillarbox the password screen instead of expanding stale maps.
- [x] F7: flying enemy completes its approach/exit before X can engage it.
- [x] New F3: Ride Armor is absent on approach, but appears after death/respawn.

Track each fix and its validation here. Keep source saves unchanged, sprite
capacity opt-in, and all code on `codex/mmx-custom-renderer-spike`.
Checked means implemented and validated against the captured/simulated cases
below; the next owner playtest remains the acceptance check.

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
mate, and retains the column facing the current room while replacing only
its duplicate in host margins. Native scripted doors retain live PPU output.

Gameplay widening remains separate from drawing. Existing enemy, projectile,
traffic and helicopter hooks use the custom view's rounded margin. Spawn
anchors retain 32 pixels of lead beyond that margin. Spark Mandrill's kind-3
mid-boss controller and kinds 0–2 keep their native scan, with independent
cursors. Vile's protected interval starts earlier when necessary to stop the
larger custom lookahead from reaching the allocation-sensitive room first.
Early guest graphics/stage streaming remains disabled in custom mode.
The Highway bee controller now belongs to the native scan because its
initialization starts the arena camera push. Once that boundary is reached,
its vertical descent receives widescreen lead independently of the lock.
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
- The first controlled bee-boundary test (`mmx-render-ks0qm66s`) uses copies of F3
  with the bee put in waiting state and X placed on either side of the native
  distance threshold. At a distance of 160 it stays in state 2; at 96 it enters
  descent state 4, even with a 32:9 view. This is a boundary test, not a claim
  that a complete F4-to-bee route was played through. The simple walk script
  fell into the intervening gap before reaching the encounter.
  This distance-only fix is superseded by the encounter correction below.
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
`MMX_RENDER_OBJECT_TRACE=<csv path>` optionally records Highway bee, Chill
Penguin boss/flyer, and Ride Armor states with player/enemy positions. It logs
transitions and periodic samples while those objects are alive.

## Second playtest: arena push, descent and propellers

The owner's new F1/F2 saves exposed two separate gates. `82:B8E6` saves the
camera limits at object offsets `$3B`/`$31`, then locks both limits to
`objectX-$F0` immediately. Allocating this controller in the margin therefore
started a two-pixel-per-frame camera push early. `82:B964` separately waits
for the player-distance threshold before descending. Restoring only that
second threshold made the bee late without fixing the premature camera push.

Highway kind-3/id-`$22` now initializes in the native scan, like the existing
Thunder Slimer exception. For the first bee at `$0AF4`, the native scan reaches
its 32-pixel column when the camera reaches `$09E0`. After that boundary, the
descent uses the visible margin plus the existing 32-pixel lead. Older spike
saves with a waiting bee and its exact premature lock recover the saved
camera limits until the same native boundary. This recovery does not write
player/camera positions or unwind an encounter that has already started.

The rotor is effect id `$1F`, animation `$36`, with a Bee Blader parent. Its
`82:F486` initialization clears the tile base and OBJ page bit: it uses
permanent page-zero graphics and borrows only resource `$2D`'s palette.
Renderer repair now recognizes that relationship, retains the live rotor
animation/CHR, and supplies the missing palette. Substituting the body's
graphics would be incorrect. Correct current bindings retain live colors.
This repair works with expanded sprite capacity off; that option stays off
by default.

Validation artifacts under `build-custom/validation`:

- `mmx-render-iknia7ot` confirms new F1's saved arena lock is released and F2
  begins descent on the first loaded frame at 32:9. Final rotor verification
  uses `mmx-render-z4kmde74` (capacity off) and `mmx-render-9a6ex5ri` (on),
  with all three F1/F2/old-F3 samples passing the
  raw native pixel oracle and live/replay agreement; expanded queues match
  the observed submissions in order.
- A copied, controlled F2 fixture removes only the already allocated bee,
  its rotor and its own event flag, restores the saved camera limits, and
  places X/camera just before the spawn column. `mmx-render-3fnlazqz` shows
  no bee before entry. `mmx-render-qvv_l50q` and `mmx-render-99u5_xvh` show
  allocation at camera `$09E0` in both 32:9 and 16:9, then lock and descent.
  These are boundary checks, not a complete Highway playthrough.
- F2 walking samples pass at 16:9, 21:9 and Adaptive in
  `mmx-render-6x4patdu`, `mmx-render-pl7kv8ec`, `mmx-render-ul6vxykl`.
  At 16:9 descent starts after 26 pixels of movement from this manually
  positioned save, instead of the previous 105-pixel wait.
- All three CTests pass. Added checks cover the exact camera-lock boundary,
  recovery without moving X, preserving active encounters, and drawing a
  rotor from live CHR with the borrowed palette. Strict C warnings pass.
- The final executable also passes the Chill door snapshots
  (`mmx-render-fbbwthcq`) and mod-off isolation (`mmx-render-qq6l14m5`).

The existing checkpoint is `384a5fe` on `codex/mmx-custom-renderer-spike`.
All follow-up work remains on that branch; no main/master merge is intended.

## Third playtest: seven-item burndown

Background resources now belong to individual world columns, replacing the
earlier per-side palette substitution. Highway kind-2 events `$16`/`$17`
select the private CHR/palette phase. BG1 uses its world position; Highway's
half-speed BG2 projects that position back into the event coordinate system.
Only the groups/tiles owned by those transfers are replaced. The native
256-pixel background continues to use captured PPU data. Other stages retained
their live resources in this batch; Chill's foreground palette ownership is
added in the fourth batch below.

The private resources remain authoritative in the margins even when RAM says
the requested phase is current: RAM changes before DMA finishes. A moving F1
capture exposed that one-frame gap and now stays correct across it.

Stage BG3 is screen-space dialogue and is clipped to the native view. In the
Highway end arena, BG2 columns before `$A00` are authored as empty at the new
vertical scroll. Extending the arena sky edge fills that exposed region.
The password screen is identified by game scene `$D3=$0A`; `$D1/$D2` alone
incorrectly identified it as gameplay. Non-stage scenes use the centered
stock frame with black side bars, including native HUD/menu placement.

The pink flyer (enemy `$36`, `$83:DF71`) previously abandoned its approach
after traveling 160 pixels from spawn. Its custom-renderer leash now adds the
visible margin, allowing it to enter its original attack states. Ride Armor's
dedicated `$83:8948` horizontal lifetime check and `$82:808F` presentation check
now include that margin. The vertical lifetime limit is unchanged. These new
hooks retain their original limits with no custom margin or in Legacy mode.

The provided F3 already contained an armor initialized and erased before its
first animation/physics update. A state-load compatibility fix recognizes
only the untouched Chill Penguin spawn signature (position `$1220,$0390`,
full health, initial frame/collision data, empty slot, X before the spawn).
It resumes the guest initialization once the armor is within both lifetime
axes. Used, moved, animated, or damaged armor does not qualify. Its animation
`$4A` is separately mapped to resource `$49`; the enemy table only maps the
pilot, so the usable armor also needed private art before that resource loads.

Evidence in `build-custom/validation`:

- `mmx-render-ja_um420`: F1/F2/traffic/F8/F9 moving samples at maximum Adaptive
  width. F1 reaches player `$12C3` with the new CHR phase selected; its distant
  towers stay intact. Traffic remains blue behind X. F8 has one dialogue box
  with continuous sky in the left margin; F9 is pillarboxed.
- `mmx-render-gpgc8yab`: F7 normal approach, then waiting. The first flyer
  reaches attack state 4 at frame 269 with a 60-pixel X separation, progresses
  through states 6/8/10, and the second reaches state 4 at frame 400. Neither
  prematurely takes the retreat state 12 before engagement.
- `mmx-render-aku6oz5o`: original new F3, with scripted movement/jumps. Armor
  revives at the expanded lifetime boundary, remains alive through approach,
  and is visible with its green/yellow art after 620 frames. This validates
  appearance and persistence; the script did not mount the armor.
- `mmx-render-2e7_20hc`: F3/F7 with expanded capacity enabled. The option is
  recorded as on, submission prefixes match, and sprites render correctly.
  The other samples use the default off setting.
- `mmx-render-aobkpvx9`: existing Chill door fixtures still pass at maximum
  width. `mmx-render-04v2ovzj` and `mmx-render-m_ssjbe8` confirm Mod Off and
  Legacy retain their respective renderers and surface widths.
- Each custom capture is replayed at 4:3, 16:9, 21:9, 32:9, and maximum
  Adaptive width. All raw native pixel oracles pass; live output equals replay
  at the configured ratio. Visual inspection is separate from that oracle.
- All three CTests and strict C warnings pass. Regression cases cover world
  resource ownership through pending DMA, dialogue/arena sky, password bars,
  both armor cull edges, untouched-save recovery exclusions, and dedicated
  armor art. The injector verifies one flyer hook and one armor lifetime hook.

Source saves are unchanged. All seven reports have fixes and evidence; broader
full-game qualification and owner acceptance remain open before retiring Legacy.

## Fourth playtest: palettes, death/arrival and Chill doors

The five current reports are fixed on the same branch. The owner's updated
F1-F4 saves were copied to `build-custom/fourth-saves` for reproducible runs.

Chill's kind-2 `$17` event at world X `$1021` assigns foreground palettes to
the cave/outdoor sides. The first event's high nibble establishes phase 1 on
the cave side, rather than assuming phase 0. The compositor projects the
owned BG1 palette groups into the margins. BG2 keeps its live palette because
the sky also changes with elevation; Chill's CHR remains live for the same
reason. The distant outdoor pillar now stays cyan before and after approach.

The usable armor's section binding advances before CGRAM receives its new
palette. Its exact old cave palette (resource `$4A`) is recognized in the
captured OBJ colors and replaced with armor resource `$49` during that gap.
Arbitrary live damage/flash palettes are not treated as a pending transfer.

Stage dispatch `$80:997B` keeps the world active in `$D3` states `$00`, `$02`,
`$04`, `$06` and `$08`. Those states now retain the selected width through
level arrival, READY, death and stage clear. Password state `$0A` remains
pillarboxed. During death, an exact saturating RGB transform inferred from
captured CGRAM also applies the white fade to private margin resources.

Door deduplication previously removed both columns of a distant closed pair.
It now keeps the column facing the camera's room, suppressing only the mate.
Opening/closing still follows the guest's live tilemap edits. Chill Penguin's
kind-3/id-`$02` allocation belongs to the native scan so the wider lookahead
cannot start the encounter from the hallway.

Evidence under `build-custom/validation`:

- `mmx-render-gl5p7yv5` captures the corrected distant F1 pillar;
  `mmx-render-foii88zg` captures F1 at frame 170 during the actual pending
  armor palette transfer. The armor remains green/yellow. The input harness
  now accepts button chords such as `right+b` to traverse the cave exit.
- `mmx-render-bsrahrce` captures F3/F4 with both distant doors present as
  single columns and a continuous, correctly colored sky.
- `mmx-render-883h206u` reaches frame 550 of F4's rightward traversal: X is
  still in the hallway and no Penguin object exists. The full 1,000-frame
  run `mmx-render-qhrk63h_` first allocates Penguin at frame 605, player X
  `$1E0C`, after crossing the second-door boundary `$1E00`; its introduction
  advances to state 2 at frame 781. This verifies entry, not boss defeat.
- `mmx-render-upregn_g` verifies the full-width death flash at frame 220.
  `mmx-render-mfll_eq3` and `mmx-render-brf3c59i` sample level setup/black
  transition and the full-width READY scene at frames 330 and 450.
- `mmx-render-j_s_x1p9` confirms F9's password screen stays pillarboxed with
  expanded sprite capacity enabled. The current F1-F4 checks use capacity
  off; that remains the default.
- All custom captures replay at 4:3, 16:9, 21:9, 32:9 and maximum Adaptive
  width with zero raw native pixel differences. Live maximum-width output
  matches replay. All three CTests and strict C warnings pass, including
  new tests for scene ownership, both door-facing directions, palette phases,
  exact pending armor colors, death fades and Penguin event ownership.

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
