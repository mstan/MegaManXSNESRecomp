# MMX Widescreen — Tier 1 state, Tier 2 findings & blockers

Status (2026-06-11): **Tier 1 shipped on this branch** — presentation-only
widescreen, gated to player-controlled stages. Opt-in via `Widescreen = 1`
in config.ini (or env `SNESRECOMP_WIDESCREEN=1`); default off = authentic.
Follows the enhancement rules in `recomp-template/ENHANCEMENTS.md`.

## What Tier 1 does

- Renderer (shared snesrecomp runner, pinned cbf7fa7+) draws up to 95 extra
  BG columns per side (9-bit OAM x cap, ~2.0:1 max), dynamically sized from
  the window aspect (`WidescreenUpdateForWindow`, resize-live).
- `RtlDrawPpuFrame` gates per frame: full-width only when
  `$00D1 == 0x02 && !($00C3 & 0x80)`; everything else gets the authentic
  256 view centered with black pillars.
- No game logic is touched. Byte-identical behavior with the option off
  (simulation is identical even with it on — presentation only).

### The discriminator bytes (found empirically, no public doc covers them)

Method: screenshot-labeled page-0 WRAM snapshots diffed across phases
(title / menu / story text / attract-demo stage / live stage / pause) —
tools/record_timeline.py + capture_stage_phases.py + diff_timeline.py,
driven over the TCP debug server (port 4379) while free-running.

| byte | observed values |
|---|---|
| `$00D1` | 0x02 in a player-controlled stage (incl. pause); 0x00 through the whole attract loop — including its stage-engine demo segments — and title/menus/story |
| `$00C3` | bit 7 set during the pause overlay and parts of the title sequence; clear during live play |
| `$003A` | corroborator: 01 player stage, 02 demo stage, 16 title, 00 menus/story |
| `$00D2/$00D3` | 0x04 in player stage, 0x00 otherwise (unused by the gate) |

Open question: the value of `$00D1` on the post-intro **stage select**
screen was not confirmed (the intro-stage bot didn't finish a clean run).
If stage select reads 0x02, its margins would show stale tiles — the fix
is one more byte in the gate; report-and-patch if observed.

## Why margins show stale content in-stage (measured, not guessed)

VRAM write-ring measurement during the attract demo (tools/
measure_demo_stream.py):

- BG1 ($2107=0x51) and BG2 ($2108=0x59) are **64x32 tilemaps** — 512px
  wide, double the view. VRAM for ±95px margins exists (12 cols/side
  needed, 32 spare available).
- The engine streams **exactly one tilemap column at the edge of the
  authentic 256 view** per 8px of camera travel (trailing the scroll
  direction), plus a one-shot ~34-column redraw at room load. Columns
  beyond the seam hold whatever was there before — the "overlapping/
  scrolling" band user-observed on the right while moving right, and the
  leftover Japanese glyphs on the story screen.
- All tilemap writes funnel through `bank_00_82C8` (VRAM upload helper)
  called from NMI: `I_NMI -> NmiHandler -> bank_00_83D9 -> bank_00_83F1 ->
  bank_00_82C8`. NMI only uploads a WRAM staging buffer; the
  **column-decision logic runs in the main loop** and is one hop further
  upstream (unidentified).

## Tier 2 (true filled margins) — what it takes, and the blockers

To make margins genuinely valid the game logic must be widened
(apply_overrides-style injected patches, like SMW's WS-* layer):

1. **Column streaming**: bias the seam column ±12 cols outward and widen
   the room-load redraw. Requires identifying the main-loop routine that
   computes the staging-buffer column index from the camera, then patching
   its column math. Direction reversal leaves the far seam ~24 columns
   stale until caught up — needs a catch-up strategy.
2. **Enemy spawn/cull**: MMX's object system ($1028+ structs, 0x20B each)
   spawns/despawns on camera proximity; every margin enemy means finding
   the MMX equivalents of SMW's spawn-window and SubOffscreen logic.
3. **HUD**: the health bar is OAM-built (the $D6A7 formula); edge-anchoring
   it is a separate, smaller job.

**Blocker: no decomp oracle.** SMW's game-logic widescreen leaned on a
full community decompilation for names, candidate routines, and ground
truth at every step. MMX has no equivalent decomp; community documentation
(TASVideos RAM map etc.) covers gameplay RAM (HP, positions) but not the
scroll/streaming engine. Without real documentation this is blind
reverse-engineering of unannotated generated C — days of guess-and-check,
explicitly out of scope per project owner.

**Decision rule (owner, 2026-06-11): proceed on Tier 2 only if real
documentation (a substantive disassembly or ROM map covering the
scroll/draw engine) is found.**

### Documentation survey (2026-06-11) — nothing qualifies

- [MarkAlarm/MMX1-Disassembly](https://github.com/MarkAlarm/MMX1-Disassembly)
  — full-ROM disassembly, but a raw DiztinGUIsh machine dump: one 24MB
  mmx.asm of auto `CODE_xxxxxx` labels, zero semantic names or comments.
  Equivalent to what the recomp's own decoder already produces; not docs.
- [Data Crystal Mega Man X ROM map](https://datacrystal.tcrf.net/wiki/Mega_Man_X/ROM_map)
  — a 2-entry stub (two player-dash routines). Nothing on the engine.
- [TASVideos MMX RAM map](https://tasvideos.org/GameResources/SNES/MegaManX/RAMMap)
  — gameplay values (timers, title-menu selection, player state); useful
  for cross-checks, silent on $00C3/$00D1 and the scroll/draw engine.
- romhacking.net Mega Man X documents — hacks (debug menu, SA1 port) and
  editor threads; no engine ROM map found.

Conclusion: Tier 2 is **blocked** per the decision rule. The unannotated
disassembly and the recomp's own ring-buffer tooling would support a
from-scratch reverse engineering effort, but that is the explicitly
out-of-scope guess-and-check path.

Fallback idea if Tier 2 stays blocked: a runner-side validity mask — the
runner observes every tilemap write and can black out margin columns whose
content doesn't belong to the current camera span (clean-but-empty leading
edge instead of wrong content). Presentation-only, no game knowledge
required; medium visual payoff.

## Probe/measurement tooling on this branch

All under `tools/` (debug server on port 4379, game free-running —
RULE 0 honored, no pausing):

- `record_timeline.py` / `diff_timeline.py` — record screenshots + WRAM
  every 2s, label phases offline from the screenshots, diff for
  discriminator bytes. (Label phases from screenshots; blind scripted
  phase assumptions mislabeled everything twice.)
- `capture_stage_phases.py` — live capture of stage idle/walk/pause.
- `measure_demo_stream.py` — VRAM-ring streaming measurement (attract
  demo as the reference workload — scripted gameplay, zero input).
- `beat_intro_bot.py` — dumb intro-stage bot (hold right + autofire +
  jumps) for reaching stage select unattended.
- `map_modes.py` / `map_modes2.py` / `probe_combined.py` /
  `find_mode_byte.py` — earlier iterations, kept for reference.
