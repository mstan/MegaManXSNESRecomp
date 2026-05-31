# Issues — MegamanXRecomp

> **OPEN:** one rendering issue — a BRIEF sprite/OBJ-layer + HUD dropout
> under heavy sprite load (X, the fish, and the HUD vanish for a moment
> after a fish explosion, then re-appear). Transient, self-recovering;
> the digger-area artifacting is the same class. See "Open" below.
>
> **RESOLVED (2026-05-26) on branch `feat/cpu-s-stack-model`:** the
> heavy-load **scheduler softlock / black-screen** that this issue used
> to manifest as. Root cause was the snesrecomp Option-1 `cpu->S`
> return-frame model leaking the stack pointer down into zero page,
> corrupting the DMA queue tail and spinning the `82C8→B9F3→$BA48`
> walker. Three coordinated fixes landed it (see "Resolution progress
> (Option-1)" below). The build now boots fully and recovers from heavy
> load instead of freezing — the residual is only the transient dropout
> above. Lives on `feat/cpu-s-stack-model` (both repos); `main` is still
> the pre-Option-1 v0.1.1 baseline.
>
> **RESOLVED (2026-05-26), retained for regression:** two freezes
> (Chill Penguin kill, Dr Light capsule). Neither reproduces against the
> current build (MMX v0.1.1, snesrecomp `main`). Their reproduction
> recipes and diagnoses are kept deliberately — NOT to be deleted —
> re-run them after any framework change touching JSR/JSL · RTS/RTL ·
> PLA/PLP/PLD · PEI · m/x flag propagation to confirm they stay fixed.

---

## Open

### Scene-transition reveal desynced from BG load — highway BG "loads in late" + boss-select fade off (filed 2026-05-30) — ✅ FIXED 2026-05-30 (Session-7)

> #### ✅✅ RESOLVED (2026-05-30, Session-7) — highway build-in FIXED + VALIDATED. Root cause = a MISSING cfg task-entry, NOT m/x.
>
> **The highway BG build-in is fixed.** Root cause (full proof in
> `## Session-7` below): the recomp's C-host coop scheduler routes each
> resumed task slot's handler PC through a hand-written switch
> (`mmx_rtl.c mmx_dispatch_task_pc`); task entry PCs must be declared in
> `recomp/bank00.cfg`. The slot-4 **fade-OUT/blank task `$89DB`** (sibling
> of the already-declared reveal task `$89C9`) was **never declared**, so
> the recompiler emitted no `Task_89DB` and the dispatcher silently dropped
> it at `default`. Result: no fade-out → no re-blank → the stage streamed
> its bulk BG onto a lit screen, then the `$89C9` reveal no-op'd (already
> lit). **Fix** = add `func Task_89DB 89db` (bank00.cfg) + `case 0x89DB`
> (mmx_rtl.c) + regen + rebuild. **Validated:** all bulk-BG now uploads
> under forced-blank (`inidisp=0x80`, LIT=0), highway reveals with the
> complete cityscape+HUD (no black chunks, matches hardware), ride cutscene
> plays normally. MMX-only change → SMW/ALttP untouched. **Uncommitted**;
> Production rebuild + boss-select/heavy-load guards still pending.
>
> **CORRECTION of the Session-3 box below:** the "m/x divergence reorders
> fade ahead of load" hypothesis was **wrong** (width is M1X1 throughout —
> re-confirmed Sessions 3c–6). The real defect is the dropped `$89DB`
> scheduler task. Everything from the Session-3 box down is RETAINED for
> history but superseded by Session-5/6/7.

> #### ⭐ DECISIVE UPDATE (2026-05-30, session 3) — NOT a presentation bug; build-in is a recomp m/x divergence, PROVEN against real hardware [SUPERSEDED — see Session-7; it is NOT m/x]
>
> A clean-session re-verification (full investigation in
> `## Session-3 decisive findings` below) **overturns** the
> "authentic? / scheduler faithful" stalemate the earlier sub-sections
> below reached. Summary:
> 1. **PPU presentation path is CORRECT** — renderer honors forced-blank
>    (`ppu.c:652`) and brightness (`ppu.c:102`, scale `b/15`); present
>    runs `I_NMI → MmxSchedulerTick → MmxDrawPpuFrame` and the draw never
>    writes `$2100`, so the once-per-frame ring samples render-time state.
>    At every bad frame **`inidisp == $00B3 == 0x0f`** — no forced-blank,
>    no brightness fade, no stale register. Hyp A/B/C all FALSIFIED.
> 2. **The build-in is NOT authentic.** A snes9x oracle driven from COLD
>    BOOT (now possible — see tooling fixes below) shows the real ROM
>    revealing the highway with the **full city BG + HUD already present**
>    (no black chunks), then READY. The recomp reveals foreground-only and
>    streams the city BG in over ~200 frames before HUD/READY.
> 3. **Root cause localized:** the `$00B3` fade curve is faithful
>    (identical 2-frame/step cadence recomp vs oracle), but load-vs-reveal
>    is **reordered**: hardware = `load → delay → bulk-load → fade-in`;
>    recomp = `load → fade-in → delay → bulk-load`. Only Task0 (`$852C`)
>    + the vblank task run; Task0's yield variant **flips `M0X1`↔`M1X1`**
>    through the window → an **m/x-flag divergence** (same class as the
>    ecosystem ALttP regression) takes a wrong-width branch in Task0's
>    intro path that puts the fade ahead of the bulk load.
> 4. **Fix is recompiler-class** (m/x propagation in Task0's intro path),
>    NOT display-side and NOT stage-specific. Next: from-boot m/x
>    first-divergence trace, recomp vs the now-working oracle.
>
> The sub-sections below this (sessions 1–2) are RETAINED for history but
> their "is it authentic / scheduler is faithful" conclusions are
> **superseded** by the hardware reference above.

**Status: ✅ FIXED 2026-05-30 (Session-7) — dropped scheduler task `$89DB`
re-declared; validated (bulk BG loads under blank, highway reveals complete).
Production rebuild + boss-select/heavy-load regression guards pending.** (The
struck-through "m/x divergence" framing below is SUPERSEDED.) Investigated on the
`mmx-highway-loadin` worktree (Oracle build off v1.0.0 baseline
snesrecomp `926d61e`; debug server port 4379). **User-confirmed: this is
ONE root cause behind TWO reported symptoms** — the highway-intro BG
"loads in slow/late" AND the stage-end → boss-select "black screen longer
than normal + fade-in a bit off."

#### Symptom

- **Highway intro:** entering the opening Highway stage, the distant
  **background** (cityscape / dark cloud tiles) visibly fills in over
  ~1 s *after* the scene is already on screen at full brightness — black
  unloaded tile chunks in the sky that resolve late. The green highway
  *structure* (nearer layer) is fine; it's the far BG that lags. "In the
  normal game it just loads immediately." Matches the user's screenshot.
- **Boss-select:** the black screen between a finished stage and the
  boss-select grid lasts longer than normal and the fade-in is off.
  (Not yet captured under instrumentation — reachable only by clearing a
  stage; deferred. Same mechanism, manifesting in the other direction —
  the reveal/black timing not tracking the load.)

#### Evidence (same-epoch, `snes_frame_counter`; one clean run)

Title → GAME START → black stage-load transition (`~f1033`) → BG uploads
in stages via the **NMI VRAM-DMA-queue walker** `bank_00_82C8`
(stack `I_NMI → NmiHandler → 83D9 → 83F1 → 82C8`):

| VRAM byte region | role | uploaded at frame |
|---|---|---|
| `0x3000–0x6000` | highway-structure char (BG1) | **1030** (one frame, bulk) |
| `0xA000–0xB000` | tilemaps | **1060–1067** |
| `0x8000` | distant cityscape detail | **1136** |

Per-frame `inidisp` ring (always-on FrameRecord): screen reaches **full
brightness (`0x0f`) by ~f1085** — i.e. the reveal completes ~50 frames
BEFORE the cityscape detail finishes uploading (f1136), and the scene is
shown bright with the far BG still streaming in. Dense frame-stamped
screenshots confirm: `f903` = title screen, `f1033` = black transition,
`f1138` = highway with the far BG still showing dark unloaded chunks;
a few seconds later the sky is clean. So the chunks are transient
unloaded tiles, not art.

#### Root cause (class)

A **transition-cadence divergence** between the recomp's NMI / cooperative-
scheduler frame model and hardware. The game's stage-init sequences
"load near layer → reveal → load far layer" across frames; on hardware
the far-BG load lands before/under the reveal (forced-blank), so it's
never seen. In the recomp the reveal (INIDISP ramp, applied immediately
when the main loop writes it) runs *ahead* of the far-BG load task, so
the bright scene is rendered before that DMA batch is queued/flushed.
This is the same *class* as the Option-1 / scheduler-timing work, and is
glue-level (`src/mmx_rtl.c` `MmxRunOneFrameOfGame` runs `I_NMI` →
`MmxSchedulerTick` → `MmxDrawPpuFrame`; brightness takes effect the same
frame, DMA-queue content one NMI later).

#### CORRECTION (2026-05-30, cont.) — it is NOT a fade-timing bug

A precise same-epoch capture (per-frame `inidisp` curve + VRAM write
frames + dense screenshots) revised the above:

- `inidisp` is **`0x0f` (full brightness) for the ENTIRE highway-load
  window** (e.g. frames 1013–1183). There is **no sustained forced-blank
  and no brightness fade** around the highway load. The "black" early
  frames are full-brightness rendering of **empty/unloaded VRAM+CGRAM**,
  not a blank. (The per-NMI `LDA #$80; STA $2100` at gen `mmx_00_v2.c`
  ~L3851 force-blanks only for the duration of the NMI's own DMA each
  frame, then restores brightness from the game's value — invisible to
  the once-per-frame FrameRecord sample. Standard, not the bug.)
- So the earlier "reveal runs ahead of the load" framing is **wrong** —
  there is no reveal/fade to hold. The screen is displayed throughout
  while the background tiles upload into it.
- The real shape: the **foreground** highway-structure tiles
  (`0x3000–0x6000`) upload at ~f1034, but the **distant cityscape** tiles
  (`~0x7000–0x9000`) don't finish until ~f1140 — **~106 frames later** —
  and `vScroll` pans 0→256 at ~f1057 (the opening camera move). The user
  sees the background fill in over those ~106 frames.

**Open question this raises:** is the ~106-frame background-load lag a
recomp-specific scheduler/cadence divergence (background-upload task runs
much later than on hardware), or is it close to authentic (the opening
camera pan legitimately streams the far BG in) and merely more visible
here? **This cannot be answered without a hardware/snes9x reference** for
the same from-boot sequence — distinguishing "recomp bug" from "authentic
but ugly" is exactly what the oracle is for (legitimate from-boot use,
unlike the save-state repros it's disabled for). Deferred pending that
decision; a speculative scheduler change must not ship into the playable
build without knowing the target behavior.

#### Per-frame tracer findings (2026-05-30, cont.) — `loadin_get`

Added an always-on per-frame stage-load tracer (`debug_server.c`:
`debug_server_loadin_tick` + `loadin_get` cmd; BG-write counters in the
VRAM hook, lo=`0x2000-6FFF` foreground / hi=`0x7000-BFFF` cityscape;
ticked from `MmxRunOneFrameOfGame`). It spans the whole load with no
truncation (the generic rings could not). One run (highway at f1023):

| frames | inidisp | activity |
|---|---|---|
| f704 | `0x80` BLANK | bg_hi 2048 (only blanked upload frame) |
| f783–789 | `0x0f` on | bg_lo 512; bg_hi 8192×3 (~24 KB cityscape bulk) |
| f881 | `0x0f` on | **bg_lo 18944 (~foreground bulk)** |
| f898 | `0x0f` on | bg_hi 8192 |
| f904–918 | `0x0f` on | vScroll 0→256 (camera pan); bg_hi 288/frame stream |
| f984–994 | `0x0f` on | bg_hi 1024/frame stream |

So the game blanks briefly (f704) then **lifts the blank (~f783) well
before the bulk fg/bg uploads (f783–898) complete**, and then streams
more background as the camera pans (vScroll→256 at f904). All of f783+ is
at full brightness → the populate is visible. `$0100/$0101` read 0x00
throughout (highway intro is GameMode 0; the **vScroll** is the live
signal, not those bytes).

#### ROOT CAUSE pinned (2026-05-30, cont.) — fade-in completes before the content uploads

Found the brightness/fade shadow and the fade routines via the WRAM-write
ring (`trace_wram 0xB3` + `wram_writes_at 0xB3`), correlated with the
`loadin_get` upload trace in ONE coherent run (highway at f1030):

- **`$00B3` = the fade/brightness shadow** (live fade level; `0x80` =
  forced blank, `0x00..0x0f` = brightness). The NMI writes it to `$2100`
  each frame; `$00:A05B` does `LDA #$80; STA $00B3; STA $2100`.
- **`bank_00_8973` = fade-IN** (steps `$00B3` `0x02→0x0f`);
  **`bank_00_8995` = fade-OUT** (steps `0x0f→0x00`).
- Timeline: intro scene fades … → **highway fade-IN completes at f740**
  (`$00B3 = 0x0f`) → **`$00B3` is NEVER written again through f1030.**
- The highway **content uploads at f792–1004** — cityscape bulk
  (8192×3 @ f792–798), **foreground bulk 18944 @ f890**, then camera-pan
  streaming (288–1024/frame at vScroll=256, f913–1004) — ALL at
  `inidisp = 0x0f`.

**So:** the screen reaches full brightness at **f740**, then the stage
content uploads over the next **~52–264 frames while fully displayed**.
The game **never re-blanks for the highway stage-load** in the recomp —
it rides the intro's fade-in and builds the stage on a lit screen. On
hardware the content is present when the stage appears (user's YouTube
ref shows a complete BG at READY), so the recomp either (a) is missing a
re-blank the game intends for the stage-load, or (b) reveals ~260 frames
too early relative to its (scheduler-paced) upload.

**Fix options (next):**
1. *Glue, low-risk, reversible:* hold the rendered screen blank during
   the stage-content upload — now a well-defined window (bulk BG writes
   resuming AFTER an intro fade-in, settling when BG writes quiesce).
   Approximates hardware's "content present when shown"; judge by eye.
2. *True fix (deeper RE):* find why the highway stage-init doesn't
   re-blank `$00B3` (control-flow / scheduler-cadence divergence vs
   hardware) and restore it. Higher effort, touches the playable build.

(Per-frame `loadin_get` tracer + `$00B3` semantics are the tools; both
kept. Tracer is always-on.)

#### Ruled out

- **DMA throughput / per-DMA rate limit.** Not the cause: `$420B`
  (MDMAEN) does the *entire* transfer synchronously inline
  (`snes.c` `while (dma_cycle(...)) {}`). Each upload batch lands in a
  single frame; there is no per-frame byte budget to "speed up."
- **A missing/dropped forced-blank during the load.** The black
  transition IS a real blank and the near layer DOES upload under it; the
  defect is specifically the *reveal un-syncing from the far-BG load*,
  not an absent blank.
- **"Accelerate the load to beat the fade" as the fix.** Rejected
  (user-aligned): the staggering is the game's own stage-init pacing
  (decompress/queue in steps), which spans real frames on hardware too —
  forcing it faster means reaching into the game's load state machine,
  risking desync of the playable build, and is inauthentic. The
  authentic fix is to hold the reveal until the load is in.

#### Next steps

1. Pin the **exact gating**: what advances the INIDISP brightness ramp
   vs. what schedules the far-BG (cityscape) DMA batch — find why the
   recomp runs the reveal ~50 frames ahead of that batch (boundary-ring /
   per-frame task trace across `f1030–1140`).
2. Implement the **"true" fix** glue-side in `mmx_rtl.c`: hold the
   reveal/fade until the stage-load DMA queue is drained (or correct the
   transition cadence) — visual-timing only, do NOT touch game logic.
3. **Decisive reference (set aside):** a from-boot snes9x-oracle compare
   of reveal-vs-load timing would confirm hardware behavior; the oracle
   is disabled in `mmx.ini` and from-boot input determinism is a risk
   (see [No oracle when save-state repro] note) — but a from-boot
   highway run is the legitimate case if pursued.
4. Re-verify boss-select with the same fix (expected to resolve both).
5. **Guard:** confirm MMX stays fully playable + no SMW/ALttP regression
   class after any scheduler/NMI-cadence change.

Diagnostic env + helpers (`_dbg/_shot/_timeline.ps1`, the always-on
`trace_vram` + FrameRecord rings, `last_vram_write_to`) documented in
the per-user memory `project_mmx_worktree_diag_env`.

#### REVISED MODEL (2026-05-30, session 2) — scheduler is FAITHFUL; the load is Task0's own interleaved sequence

New per-frame instrumentation (committed below) overturns the
"scheduler/NMI-cadence divergence" premise above. **There is no
scheduler stall and no glue-level cadence to correct.**

Instrumentation added (all always-on rings; extend, don't arm):
- `loadin_slots <from> <to>` (debug_server.c): per-frame snapshot of the
  7 cooperative-scheduler task slots (`pc/state/countdown`), the `$00B3`
  fade shadow, AND the slot-0 (Task0) **yield-site call path** captured in
  `mmx_host_yield` (`mmx_rtl.c` `g_slot0_yield_stk[3]`, named via
  `g_last_recomp_func`/`g_recomp_stack`).
- `mmx_get_slot_debug()` (mmx_rtl.c) exposes the C-host slot fiber PCs.

Findings (fresh from-boot capture, highway reveal ≈ f905, one run):
1. **No task waits on a long countdown.** Across the entire load window
   only slot 0 = Task0 (`$852C`, `st2/cd1` — wakes every frame) and slot
   6 = the vblank task (`$B25B`) are active. The ~90-frame gaps between
   upload batches are **Task0 sitting in `bank_00_B2EB`** (a scripted
   intro *delay* routine) yielding one frame at a time — the game's own
   pacing, faithfully resumed 1 host-frame : 1 game-frame.
2. **The load order is intrinsic to Task0's instruction stream.** The
   highway tileset loaders — `bank_00_8A45` (DMA helper) called by
   `bank_00_94E7 / _991B / _9989` — run **after** the blocking fade-in
   `bank_00_8973` (which yields `YieldNFrames(cd2)`, 2 frames/step, ~28
   frames). So cityscape (`bg_hi` 8192×3) + foreground/overpass (`bg_lo`
   18944) necessarily upload onto an **already-lit** screen. Sequence:
   under-blank load (`f649–878`) → fade-in (`8973`) → reveal `f905` →
   post-reveal load (`94E7/991B/9989`, `f1007–1105`) → camera pan
   (`vScroll 0→256`, BG column-streams as it pans).
3. **Frame-relative timing matches hardware.** Fade and delays are
   frame-counted via the yield primitives; the cooperative scheduler
   burns exactly one host frame per yield. The recomp is NOT "running
   faster" in frame terms here. The one genuine recomp/HW difference —
   **synchronous (instant) DMA** — would make the load finish *earlier*,
   not later, so it does not explain the late content.

Visual ground truth (screenshot ring, `_seq_*.png`): at reveal the green
highway *foreground* is complete but the *sky* has large black unloaded
tile chunks that fill in over ~1 s (k170→k176) — matches the report.

**Option 1 / 3 (hold the reveal blank) is self-defeating.** It fixes
"BG builds in late" by *adding* a black hold — which is exactly the
*other* reported symptom ("boss-select black screen too long"). And the
load is interleaved with the **intended camera-pan intro** (content is
*meant* to stream in as the camera pans up), so a blanket hold would
black out animation that should be seen. So a display-side hold is out.

**A-vs-B is genuinely unresolved** (oracle unavailable — desyncs from the
HLE'd boot): (A) authentic intro behavior the recomp reproduces
faithfully (argues: faithful scheduler, frame-deterministic sequence,
fixed `B2EB` delays), vs (B) a recomp control-flow/width divergence that
defers the highway load past the reveal (argues: the black regions are
large and persist ~1 s — more than CRT blur would mask — and the user +
a YouTube ref say hardware shows it complete at reveal). No evidence of a
wrong branch was found, but it wasn't ruled out. The only *true* fix is
(B) — making the load complete before reveal (content arrives earlier; no
added black) — which is recompiler-class, risky to the playable build,
and may find nothing.

#### Targeted cosmetic attempt (2026-05-30, session 2) — implemented, env-gated OFF

User elected to try a targeted display-side reveal-hold anyway. Built it
(`mmx_rtl.c` `mmx_reveal_hold_tick` + an `inidisp`-override at render in
`MmxDrawPpuFrame`; arms on the blank→fade signature gated by a stage-sized
under-blank BG load so it can't fire in gameplay; reads always-on
`g_loadin_bg_*_last`). Instrumented its state into the `loadin_get` ring
(`rh`/`rhload`) and measured it from boot. **Outcome: not a clean win.**

- The highway is revealed, then its BG keeps loading in batches separated
  by long (~90-frame) scripted `B2EB` delays, right up to the camera pan
  (`vScroll→256`). Releasing after the first cityscape batch reveals mid-
  load (build-in just moves later); the only clean "assembled, now it
  animates" boundary is the pan. Holding to the pan = **~3-5 s of black**,
  which recreates the "black screen too long" complaint. There is no
  short-black variant that hides the build-in.
- The arming screens are all the highway itself (verified by screenshot:
  the pre-resolve frames already show the highway with the black-chunk
  sky), so it does not wrongly blank a separate logo/title — but the black
  duration alone makes it a poor trade.

**Left in tree, DISABLED by default**, env-gated `MMX_REVEAL_HOLD=1`, so
the intro can be A/B'd live by eye. Recommendation: treat the build-in as
authentic progressive load and ship with the hold OFF, unless a live A/B
shows the long-black version is preferable. The diagnostic instrumentation
(`loadin_slots`, per-slot scheduler state, Task0 yield call-paths,
`rh`/`rhload`) is the durable value from this session and is kept.

#### Session-3 decisive findings (2026-05-30) — presentation falsified; hardware reference obtained

Clean-session re-verification. Tools: per-frame `loadin_get`/`loadin_slots`
rings (sampled post-NMI/post-scheduler, i.e. render-time state) + a
from-cold-boot **snes9x oracle** (made usable this session — see below).

**1. PPU presentation path is CORRECT (Hyp A/B/C falsified).**
- Renderer honors forced-blank — `PpuDrawWholeLine` (`ppu.c:652`)
  `memset`s the line to black when `PPU_forcedBlank`; and brightness —
  `brightnessMult[i] = ((i<<3)|(i>>2)) * brightness / 15` (`ppu.c:102`),
  so brightness 0 → black. Both correct.
- Frame loop (`mmx_rtl.c`): `MmxRunOneFrameOfGame` = `I_NMI` (flush prev
  frame's DMA queue + restore `$2100` from `$00B3`) → `MmxSchedulerTick`
  (run game tasks, queue next DMA) → host calls `MmxDrawPpuFrame`.
  `MmxDrawPpuFrame` renders 225 scanlines and **never writes `$2100`**, so
  the per-frame ring's `inidisp` IS the render-time value. The NMI's
  per-frame `LDA #$80;STA$2100` force-blank is restored before the draw
  AND before the sample — it does not create a present bug.
- Bad-window frame table (recomp, one cold-boot run; `inidisp`==`$00B3`
  every row → no fade/blank, no stale register):

  | frame | $00B3 | INIDISP | bg_lo | bg_hi | vScroll | oam | kind |
  |---|---|---|---|---|---|---|---|
  | 1002 | 0x0f | 0x0f | 0 | 0 | 0 | 16 | (post-fade, HUD building) |
  | 1003 | 0x0f | 0x0f | 512 | 0 | 0 | 0 | BG_LOADED_NO_HUD |
  | 1004 | 0x0f | 0x0f | 0 | 8192 | 0 | 0 | cityscape on lit screen |
  | 1101 | 0x0f | 0x0f | **18944** | 0 | 0 | 0 | **FG bulk on lit screen** |
  | 1118 | 0x0f | 0x0f | 0 | 8192 | 0 | 0 | cityscape on lit screen |
  | 1124 | 0x0f | 0x0f | 0 | 288 | 256 | 0 | camera-pan stream |

  → the bad frame is full-brightness rendering of genuinely-unloaded VRAM,
  not a missed blank. Presentation is innocent.

**2. The build-in is NOT authentic — hardware reference.** Real ROM via
the snes9x oracle, cold boot (color-corrected screenshots `_on_036..046`):
the opening Highway appears with the **complete distant city skyline + HUD
already present**, then "READY" — no black chunks, no visible build-in.
The recomp instead reveals FG-only, no HUD, then streams the city BG in
(black chunks) over ~200 frames before HUD/READY. So the recomp presents
intermediate load frames hardware never shows.

**3. Root cause localized — m/x divergence reorders fade vs load.** The
`$00B3` fade curve is faithful: recomp and oracle both step `0x02→0x0f` at
exactly **2 frames/step** over ~26 frames, with identical blank→fade→
hold→fadeout cycle structure (recomp `_hw_loadin.json` b3 vs oracle
`_oracle_b3.json`). What diverges is **load-vs-reveal order**:
- Hardware: `partial load (under 0x80) → ~125f delay → BULK load (still
  under 0x80) → fade-in → reveal complete`.
- Recomp: `partial load (under 0x80) → fade-in (reveal) → ~125f delay →
  BULK load (FG 18944 @ ~f1047/1101, cityscape after) on a LIT screen`.

  `loadin_slots` across the window: only **slot 0 = Task0 (`$852C`)** and
  the vblank task (`$B25B`) are ever active; Task0 does the loads itself,
  paced by `YieldOneFrame`/`YieldNFrames`. Task0's yield **variant flips
  `M0X1`↔`M1X1`** frame-to-frame (e.g. f1047 bulk load = `M0X1`, f950/1064
  cityscape = `M1X1`) — the **m-flag is toggling**. An m/x-flag (wrong-
  width) divergence in Task0's intro path takes a different branch than
  hardware, putting the blocking fade-in (`bank_00_8973`) *ahead* of the
  bulk tileset loaders (`bank_00_94E7/_991B/_9989` → `_8A45`). This is the
  **same class as the ecosystem ALttP m/x-dispatch regression**
  (see per-user memory `project_alttp_dispatch_regression`).

This **invalidates the session-1/2 "load order is intrinsic to Task0's
instruction stream / scheduler is faithful" conclusion**: the oracle runs
the SAME ROM/Task0 and loads-before-reveal, so the recomp must be running a
DIFFERENT branch (m/x), not the same stream at a different cadence.

**4. Oracle made usable from cold boot (reusable tooling).** Prior
sessions reported "oracle desyncs to garbage." Cause found + fixed:
- `main.c` `emu_oracle_run_frame` passed the runner's **12-bit** per-player
  input word (B=0x001…START=0x008…R=0x800; see `debug_server.c`
  `k_controller_names`) straight to `snes9x_bridge`, which reads `s_joypad`
  in **SNES 16-bit** order (B=15…START=12…R=4). START never reached the
  oracle → real-ROM boot couldn't be navigated. Added
  `mmx_runner_to_snes_joypad()` remap. The from-boot oracle is the
  **legitimate** case (only the save-state repros are unfollowable);
  `EnableSnes9xOracle` left `false` by default (flip to `true` for a
  reference run).
- `snes9x_bridge.cpp` `retro_video_refresh`/`emu_screenshot` assumed
  XRGB8888 but snes9x emits **RGB565** → garbled/wrong-colour oracle
  screenshots. Now honors the format the core requests via
  `SET_PIXEL_FORMAT` (RGB565/0RGB1555/XRGB8888). Oracle screenshots are
  now correct.

#### Session-3 cont. — m/x-variant hypothesis DISPROVEN at the milestone calls; it is control-flow sequencing

Boundary-ring capture (8M ring, `freeze_at_frame 1130`, regex-parsed —
`boundary_get` emits both `"X"` register and `"x"` flag so `ConvertFrom-Json`
chokes; parse with regex). Recomp milestone ENTRY order, one run:

| frame | fn | variant | note |
|---|---|---|---|
| 795 | `8A45` | M1X1 | under-blank partial load |
| **876** | **`8973`** | M1X1 | **fade-in (reveal)** |
| 985 | `94E7` | M1X1 | bulk loader (post-fade) |
| 990 | `991B` | M1X1 | bulk loader (post-fade) |
| 1083 | `9989` | M1X1 | bulk loader (post-fade) |

**Every milestone runs at the correct M1X1 width.** The earlier
`M0X1`↔`M1X1` flip seen in the `loadin_slots` *yield* variant was just the
yield site varying frame-to-frame, NOT a wrong-width dispatch at the
fade/loaders. So **the "wrong m/x variant / m/x propagation" root cause is
DISPROVEN at these call sites** — do NOT patch m/x dispatch on that basis.

Call chain of the deferred loader (frozen ring, f1064):
`Task0 → bank_00_931F → bank_00_94E7 → bank_00_8A45` (the DMA-upload helper),
**interleaved every frame with the `D56F` object/OAM engine**
(`91AA→9271→D56F→…→D625→bank_04_9086→Yield`). I.e. by the build-in window
Task0 is already running the gameplay object engine AND a per-frame BG
loader (`931F→94E7→8A45`) — all at M1X1.

**Refined open question:** why are the bulk BG uploads (cityscape `bg_hi`
8192×3 and FG `bg_lo` 18944) issued by this `931F→94E7→8A45` path *after*
the fade (`8973`) rather than under the preceding blank? It is a control-
flow / sequencing divergence, NOT a wrong-width decode. Caveat for the next
pass: part of the post-fade `bg_hi` traffic (the `288–1024/frame` stream at
`vScroll=256`, f1124+) is the **authentic camera-pan reveal** and must not be
mistaken for the bug; the bug is specifically the *bulk* `bg_hi 8192×3`
(~f1004–1009) + `bg_lo 18944` (~f1047/1101) landing on a lit screen.

#### Session-3 cont. — ORDER REVERSAL PROVEN against hardware (load-then-fade vs fade-then-load)

Oracle execution-order reference obtained (`_oracle_order.ps1`: incremental
both-bank milestone dumps from the snes9x insn-trace tail, no reset; defeats
1M-ring eviction; **execution is in bank `$80`**). Real-ROM highway window:

| event_index | event | oracle pc | oracle f | oracle P (m/x) | recomp pc | recomp f | recomp P (m/x) | verdict |
|---|---|---|---|---|---|---|---|---|
| — | loader `9989` | `$80:9989` | 1120–1128 | 0xb0/b1 (m1x1) | `bank_00_9989` | 1083 | 0x33 (m1x1) | both M1X1 |
| — | fade `8973` | `$80:8973` | **1147** | 0xb1 (m1x1) | `bank_00_8973` | **876** | 0x33/0x31 (m1x1) | both M1X1 |

**ORACLE order: loader `9989` (f1120–1128) → fade `8973` (f1147) = LOAD-then-
FADE. RECOMP order: fade `8973` (f876) → loaders `94E7/991B/9989` (f985–1083)
= FADE-then-LOAD. The order is REVERSED.** That is the concrete divergent
edge: the recomp's Task0 runs the reveal fade before the stage loaders;
hardware runs the loaders (under blank) first, then the fade. This is why
the recomp shows the BG build in on a lit screen.

**Width is NOT the cause (re-confirmed):** every milestone runs at **m=1,x=1
on both sides**. So this is NOT a wrong-width / wrong-variant dispatch bug.

**Candidate cause (lead, NOT yet proven causal):** the entry-P **N and Z
flags differ** — oracle `0xb0/0xb1` (N=1) vs recomp `0x31/0x33` (N=0). A
wrong N/Z would flip a `BMI/BPL/BNE/BEQ` that sequences the load-vs-fade
state. CAVEAT: entry-P is the caller's flag state and `9989` is reached via
different call sites on each side (oracle = per-frame stream f1120–1128;
recomp = single f1083), so the N/Z delta may be contextual rather than the
branch the order depends on. Must locate the actual ordering branch and show
it reads a diverging flag before claiming this.

#### Session-3 cont. — VRAM-write attribution: it is a multi-path ENQUEUE-cadence divergence, not one branch

`last_vram_write_to` (per-write func+stack attribution; `trace_vram 0 0xffff`
armed) at `freeze_at_frame 1112`:

| VRAM byte | written frame | function (stack) | timing |
|---|---|---|---|
| `0x3000–0x5000` (FG) | **873** | `82C8` (NMI DMA-queue walker) | **under blank ✓** |
| `0x2000` | 1065 | `8B90` ← `953F→9579→991B` | post-fade |
| `0xa000–0xb000` (tilemaps) | 1066 | `82C8` (NMI walker, flushing queue) | post-fade |

Key correction: the **foreground tiles DO upload under the blank** (f873).
The post-fade uploads come from (a) the NMI DMA-queue walker `82C8` flushing
DMA that was **enqueued late**, and (b) the `953F→9579→991B→8B90` path
(`953F` = the task dispatcher implicated in the Option-1 / fish-softlock
m-flip work). So the late BG is produced by the object/DMA-**enqueue** engine
(the `D56F`/`953F` subtree) running post-fade — the **same subtree** as the
Option-1 issues — and flushed by the NMI walker. This is a **broader
enqueue-cadence divergence**, NOT a single fade-vs-load ordering branch, and
every milestone reachable here runs at M1X1 (width is not it).

**Honest status:** behaviorally proven (HW loads-then-reveals; recomp builds
in; not presentation; not m/x width), narrowed to the post-fade enqueue
cadence in the `953F`/`D56F`/NMI-walker interaction — but NOT isolated to one
provable instruction edge, and the evidence points to a cadence divergence
rather than one wrong branch. **Do not patch** until a single edge is proven
(hard rule). Next probe: trace WHO enqueues the late DMA (the `953F→…→991B`
enqueue path) and why it runs post-fade vs under-blank on HW — likely the
same root as the Option-1 `cpu->S`/dispatch family
([[Option-1 fix-attempt notes above]]); a from-boot oracle WRAM/enqueue-order
compare on `953F`'s inputs is the decisive next step. NOT width, NOT
display-side, NOT stage-gated. Guard SMW/ALttP.

- **`find_first_divergence` does NOT work here (trap).** It diffs recomp
  WRAM vs oracle WRAM *at the same input-frame assuming lock-step game
  state* — but the recomp HLE-boots while the oracle real-boots, so at any
  given frame the two are at **different game moments**. The diff would
  report a huge "divergence" that is just content mismatch (exactly the
  failure the `EnableSnes9xOracle=false` rationale warns about). The oracle
  is the GROUND-TRUTH *reference* (BG-loads-before-reveal, proven by
  screenshot), NOT a per-frame lock-step comparator for MMX.
- **Use the recomp-side boundary ring vs the ROM disasm instead.** Build a
  ROM disasm of Task0's highway stage-init (the fade `$8973` lives at the
  `JSR $8570` site; the loaders `$94E7/_991B/_9989→$8A45` are a separate
  GameMode-driven phase). Capture the always-on boundary ring frozen at the
  highway window (`freeze_at_frame <N>` near the under-blank→fade boundary,
  ~f850–1050; set `SNESRECOMP_BOUNDARY_RING_ENTRIES=8388608`), and find the
  function that ENTERS m=1 but EXITS m=0 (the proven D56F-style PLP/PLD
  m-flip technique) OR the conditional whose width-wrong compare takes the
  branch that sequences fade-before-load. Then fix the m/x propagation at
  that emit site.
- Capture scripts (gitignored `_*`): `_cap_highway.ps1` (recomp filmstrip
  + loadin), `_oracle_nav.ps1` (oracle filmstrip + `$00B3` timeline),
  `_cap_slots.ps1` (loadin_slots window).

#### Session-4 (2026-05-30) — late bulk BG is the gameplay OBJECT ENGINE (`953F`) streaming POST-fade; reframed to a Task0 phase-ordering (engine-entry vs reveal-fade), NOT one enqueue branch

Clean from-cold-boot re-capture on the **current build** (no rebuild;
`fix/scene-transition-loadin`). Method honored the ring discipline:
queried always-on rings for the window, did not arm-then-hope.

**`$00A5/$00A6` software DMA queue is the WRONG signal — ruled out.**
Armed `trace_wram a5 a6` from boot (`_cap_enqueue.ps1`); the tail is
touched ONLY by the per-frame NMI OAM/scroll DMA (`B787←B668` enqueue,
walker `82C8` reset) and is **DORMANT f544→f1183** — i.e. across the
entire highway load window. The bulk BG does **not** flow through it;
it is direct (synchronous `$420B`) DMA, attributable only via the
VRAM-write ring. (`$00A5` resumes at f1183 = camera-pan/gameplay start,
confirming it is the gameplay OAM/scroll queue, not the bulk-BG path.)

**Bug window (this run, loadin ring): bulk-on-LIT (`inidisp 0x0f`) at
f996/998/1001 (cityscape `bg_hi 8192×3`) + f1093 (FG `bg_lo 18944`) +
f1110 (cityscape).** Under-blank partial loads preceded each reveal
(f698–711, f901–912). Two reveal-fades: `8973`@f711, `8973`@f912.

**Boundary ring frozen at the bug onset (`freeze_at_frame`, 8M ring,
zero eviction — `boundary_idx`=37519 total; `_cap_bnd3.ps1`). Milestone
ENTRY timeline (regex-parsed; dup `X`/`x` keys):**

| frame | call (depth) | P | m,x |
|---|---|---|---|
| 711 | `8973` fade (d1, Task0 direct) | 0x31 | 1,1 |
| 912 | `8973` fade (d1, Task0 direct) | 0x33 | 1,1 |
| 995 | `931F`→`94E7`→`8B90`→`8A45` (d3–5) | 0x33 | 1,1 |
| 997–1000 | `953F`→`9579`→`991B`→`8B90`→`8A45` (d1–4) | 0x31 | 1,1 |
| 1093 | `953F`→`9989`→`8B90` (d1–4) | 0x33 | 1,1 |
| 1109–1114 | `8A45`/`8B90` | 0x33 | 1,1 |

**Width is M1X1 EVERYWHERE — re-confirmed NOT a width bug** (every
milestone m=1,x=1; matches Session-3).

**NEW MECHANISM — the late bulk BG is the per-frame gameplay OBJECT
ENGINE `953F`, not the intro/stage-init loader.** `953F` is a jump-table
dispatcher (gen `mmx_00_v2.c:224646`) on the **zero-page global `$00D2`**
(`D=0x0000` at all entries, confirmed from the boundary `D` field):
`X = [$7E:00D2]; _idx = X/2`; 6-entry table → case 1 = `9579`(→`991B`,
cityscape) … `9989` = FG; leaf `8A45` does the `$420B` DMA. During the
bug window `$00D2`=2 (X=2 → case 1 streamer). **Crucially `953F` does NOT
run at all before f995** — the gameplay object-engine loop is not entered
until AFTER the f912 reveal fade. Between f912 and f995 Task0 sits in the
scripted `B2EB` delay (~83 frames).

**The reframe (supersedes "one fade-vs-load branch"):** the divergence is
the ORDER of two Task0 phases —
- **Recomp:** reveal-fade `8973`@f912 → ~83f `B2EB` delay → enter object
  engine `953F`@f995 (streams bulk cityscape+FG on a LIT screen) = BUG.
- **Hardware** (Session-3 oracle order proof): engine-subtree loader
  `9989`@f1120 → fade `8973`@f1147 = BG streamed under blank, then reveal.

So hardware enters/runs the gameplay engine (which streams the bulk BG)
**before** the reveal fade; the recomp does the reveal fade first and
enters the engine after. This is a **Task0 phase-ordering / engine-entry
cadence** divergence, consistent with Session-3's "multi-path enqueue
cadence" honest status — now concretely identified as *gameplay-engine
entry happening after the reveal instead of before it.* NOT width, NOT
`$00A5`, NOT display-side.

**Still NOT a proven single instruction edge (hard rule → no patch).**
The open question: what in Task0's intro state machine orders the reveal
fade (`8973`@f912) ahead of the gameplay-engine entry (first `953F`@f995),
when hardware orders them the other way? `$0100/$0101` GameMode stays 0
throughout, so it is NOT GameMode-gated.

**Next probe (decisive):** PC-aligned Task0 trace across ~f850–1000
(recomp block-trace / boundary EXITs) vs the oracle insn-trace (bank
`$80`, incremental tail dump), to find the branch where recomp takes
"fade now" while hardware takes "run engine / keep loading". Candidate
state to trace on BOTH sides: whatever Task0 reads right before the
`8973`@f912 call, and the trigger that first enters `953F` (the
gameplay-loop dispatch). Then prove the exact PC + diverging value.

Capture scripts added (gitignored `_*`): `_cap_enqueue.ps1`
(`$00A5` + loadin), `_cap_bnd3.ps1` (reach highway → freeze 8M boundary
ring → milestone ENTRY dump + loadin). Artifacts: `_enq_a5.json`,
`_enq_loadin.json`, `_bnd3_raw.json`, `_bnd3_window.json`,
`_bnd3_loadin.json`.

##### Session-4 cont. — fade call site is `$91B5` scene-init (fixed load→fade sequence); the late bulk is a SEPARATE engine. Likely NOT a single branch.

Drilled into the recomp's own instruction-block trace (the always-on
`capture()` ring, `trace_get_v2 event=0 frame_lo=.. frame_hi=..`, frozen by
`freeze_at_frame`; the v2 codegen does NOT emit `debug_on_block_enter`, so
the `trace_blocks`/`get_block_trace` ring is dead here — use `trace_get_v2`).

- The reveal fade is called from **`bank_00_91B5`** at PC **`$9269`** (`JSR
  $8973`). `$91B5` is the highway **scene-init**, a FIXED linear sequence:
  `$9261 LDA#$10` → `JSR $89E1` → `$9266 JSR $935D` (load) → `$9269 JSR $8973`
  (fade) → `$926C STA [D+$3b]=$FF; RTS`. **No conditional gates the fade** —
  it is unconditionally the call right after `$935D`.
- **`bank_00_935D`** = a fixed list of `LDY #<fileID>; JSL $82:8011`
  (load-graphics-file via the bank-02 decompress+DMA loader): Y =
  `0x12,0x2e,0x26,…`. These are the **under-blank** loads. So `$91B5` loads
  a fixed file set, then immediately fades — atomically, same function,
  no yield between load and fade.
- The **post-fade bulk** (cityscape `bg_hi 8192×3`, FG `bg_lo 18944`) is a
  **DIFFERENT file set** streamed by the **separate per-frame gameplay
  engine `953F`** (dispatch on `$00D2`), which does not begin until ~90
  frames AFTER `$91B5`'s fade. On hardware (Session-3 order proof) that
  engine's loader `9989` runs BEFORE the fade `8973`.

**So the divergence is a PHASE ORDERING between two independent producers —
Task0's scene-init `$91B5` (load fixed set + reveal) vs Task0's entry into
the gameplay BG-streamer engine `953F` — not a single wrong branch inside
either.** `$91B5` itself is branch-free at the fade; width is M1X1; `$00D2`
routes the engine correctly once it runs. What differs is *when Task0 starts
running the engine relative to running `$91B5`'s fade.* Hardware runs the
engine first; recomp runs `$91B5`/fade first.

**Honest implication for the "prove one instruction edge" bar (hard rule):**
the evidence across Sessions 1–4 now points to a **scheduler / task-ordering
cadence** divergence (which of Task0's phases the cooperative fiber model
runs in which frame), NOT a single branch reading a single diverging
flag/value. There may be no single edge to prove. Candidate true root:
the MMX glue cooperative scheduler (`mmx_rtl.c MmxRunOneFrameOfGame` /
`MmxSchedulerTick`: `I_NMI → scheduler → draw`) sequences Task0's
scene-init vs the gameplay-engine task in a different order/frame than
hardware's real CPU+NMI timing — i.e. a **scheduler-faithfulness bug in the
glue**, which would be a legitimate (non-hack) fix target. This needs an
oracle PC-aligned check of Task0's phase order (does HW interleave the
engine task between scene-init load and the fade?) to confirm before any
change. Decision point flagged for the next session — see handoff.

Block-trace artifacts (gitignored): `_blk_loadin.json`, `_blk_fade3.json`
(fade call-site path). The block-trace freeze edit to `debug_server.c`
`debug_on_block_enter` was reverted (that hook is dead in v2 codegen).

##### Session-4 cont.2 — DISPATCH located: `$91AA` on `[D+$39]` (state 0=`$91B5` init-load+fade, state 2=`$9271` engine). Oracle CONFIRMS hardware streams ~120f then fades once.

Pinned the dispatch that owns load-vs-reveal. The highway object runs
**`bank_00_91AA`**, a 3-entry jump table on the object state byte
**`[D+$0039]`** (gen `mmx_00_v2.c:277608`):
- **state 0 → `$91B5`** = INIT: `JSR $89E1` → `JSR $935D` (load a fixed gfx
  file list via `JSL $82:8011`) → **`JSR $8973` (fade-IN)** → `STA [D+$3b]=$FF`.
- **state 2 → `$9271`** → `D56F` = the per-frame object/stream engine.
- state 4 → next.

Standard actor `init→run`: state 0 inits (load + reveal), then advances to
state 2 (engine streams every frame). So both HW and recomp run `$91B5`
once then `$9271` — it is NOT a wrong-path selection.

**Oracle (real ROM, `_oracle_order.ps1` + the 4 new milestone PCs, window
f1156–2518) — hardware execution order, decisive:**
- `91B5`/`935D`/`89E1` = **0 hits** in-window (the init ran BEFORE f1156).
- engine `953F` = **1160 hits**; loaders `991B`×1, `9989`×6, `8A45`×4
  stream the BG across **f1156–1261** (~120 frames).
- fade `8973` = **exactly 1 hit @ f1280** — i.e. AFTER all the streaming,
  and NOT reached via `$91B5` (which never ran in-window).

##### Session-4 cont.3 (2026-05-30) — CORRECTION: "instant-DMA-collapse" hypothesis REFUTED. Late element is the `$953F`/`$00D2` bulk-stream phase, not a collapsed init.

A finer recomp capture (`_cap_bnd3` with `91AA/91B5/935D/9271` added,
frozen at the highway; loadin correlated) **refutes** the prior
"synchronous DMA collapses `$91B5`'s load so it falls straight into the
fade" hypothesis. Do NOT treat that as the theory.

Corrected facts (recomp, one clean run, frozen f1204):
- **`$91B5` scene-init does NOT collapse.** It enters @f753, its
  under-blank load runs f785–855, its fade follows — it spans ~100+
  frames, paced normally. Synchronous DMA is NOT collapsing it. (The
  earlier "f877 all one frame" observation was only the *tail* blocks
  `$9261→$9266→$9269` executing together, not the whole init.)
- **The object/engine flow runs BEFORE the reveal.** `$91AA` dispatches
  on `[$0039]` correctly: state 0 → `$91B5` (init, @f753), then state 2
  (X=`0x0002`) → `$9271` object/`D56F` engine, which runs @f894–965.
- **Reveal fade completes @f891** (inidisp/`$00B3` → `0x0f`).
- **The actual LATE element is the bulk-BG stream phase** = the separate
  per-frame dispatcher **`$953F`** (on **`[$00D2]`**) → `$9579`→`$991B`
  (cityscape) / `$9989` (foreground) → `$8A45` (the `$420B` DMA). It does
  not start streaming the bulk until **~f967**, i.e. AFTER the f891
  reveal. cityscape `bg_hi 8192` @f966/968/971, FG `bg_lo 18944` @f1063,
  more @f1080 — all at inidisp `0x0f`.

**Order, corrected:**
- **Recomp:** reveal fade `$8973` reaches visible/full-bright (f891) →
  **then** `$953F`/`$991B`/`$9989` bulk BG streams onto the LIT screen
  (f966–1080).
- **Hardware/oracle:** `$953F`/`$991B`/`$9989` bulk BG stream happens
  **before** reveal fade `$8973` (oracle: stream f1156–1261, fade f1280).

**Current target (NOT yet pinned):** find what gates/advances `[$00D2]`
or otherwise triggers the `$953F` bulk-stream phase such that it runs
AFTER `$8973` in recomp but BEFORE `$8973` on hardware. It is a nested
state-machine ordering (`$91AA`/`[$0039]` scene-vs-engine; `$953F`/`[$00D2]`
when-to-stream), NOT a collapsed init and NOT (on current evidence) an
m/x width bug. **Do NOT conclude "implicit DMA/vblank timing" until the
`$00D2`/`$953F` gating is fully traced** — the direct DMA can be
synchronous and still correct; the more likely cause is the stream-phase
gate/counter advancing late.

**Next probe (in progress):** trace every read/write of `$00D2`, the
branches in/just-before `$953F` that depend on it, and transitions into
`$953F`/`$9579`/`$991B`/`$9989`/`$8A45`/`$8973` — recomp vs oracle,
aligned by PC/milestone (NOT absolute frame). Find the first divergent
edge (exact PC, input value/flags, branch result, first bad state write)
that delays the stream phase past the fade.

Captures: `_oracle_order.json` (HW milestone order), `_bnd3_raw.json` /
`_bnd3_loadin.json` (recomp milestone + brightness/bulk timeline).
`mmx.ini EnableSnes9xOracle` toggled true only for reference runs, left
**false**.

##### Session-4 cont.4 (2026-05-30) — stream-gate traced to `$953F`/`$9550`/`$00D2`; `$1F9B` is a RED HERRING. Ordering divergence confirmed; exact single edge still a multi-var handshake.

Traced `$00D2` (the `$953F` bulk-stream dispatch state) write timeline on
the recomp (`trace_wram` from boot, frozen at highway):
- `$953F` dispatches on **`[$00D2]`**: 0 → `$9550` (wait state), 2 →
  `$9579`→`$991B` (cityscape stream), 4 → next chunk. `$991B`/`$9989` are
  only reachable with `$00D2`≥2, so they ENCODE the gate state.
- `$00D2` flips 0→2 (stream start) at **f1068** (one run) / **f1011**
  (another). The reveal (`$00B3`→`0x0f`) was at **f1009** / **f919**
  respectively. **So `$00D2`→2 (stream start) lands ~60–90 frames AFTER
  the reveal.**
- `$9550` sets `$00D2:=2` at block `$9567`, reached only if block `$9562`'s
  check `[$1F9B]==0` passes. **`$1F9B` is a RED HERRING — it is 0 the
  entire run**, so that check never blocks. The `$9550` top-gate is
  `[D+$00d3]` (`if d3!=0 goto $9576` wait path), and the proximate trigger
  observed is **`$1F7A`**: set `0x23` @f529, **cleared to 0 by loader
  `$94E7`←`$931F` @f1065**, immediately followed by `$00D2`→2 @f1068. So
  the stream gate opens when the loader `$94E7` finishes its batch — and
  that finish is AFTER the reveal.

**Ordering divergence — CONFIRMED both sides (via insn-milestone order, the
reliable oracle signal; per-variable `emu_wram_timeseries` returned only
f1 samples — unreliable, do not use it for this):**

| event | recomp | hardware (oracle) |
|---|---|---|
| reveal fade `$8973` → full bright | f1009 (one run) | f1280 (single, LAST) |
| stream gate `$00D2`→2 (⇒ `$991B`/`$9989` run) | **f1068 (AFTER reveal)** | ≤f1161 (`$991B`@1161) — **BEFORE reveal** |
| bulk BG stream `$991B`/`$9989` | f1068+ (on lit screen) | f1156–1261 (under blank) |

So: **hardware advances the stream gate and streams the bulk BEFORE the
reveal fade; the recomp reveals first, then the loader/`$94E7` finishes and
the gate opens (`$00D2`→2) after.** Equivalent statements: "stream gate
opens late" ≡ "reveal fires early relative to the load." Which side is
*the* bug (gate-too-late vs reveal-too-early) is not yet decided — both are
the same ordering flip.

**Honest status:** the gate is a **multi-variable handshake**
(`$00D2`/`$00D3`/`$1F7A`/`$1F99`, plus whatever triggers the `$8973` reveal
independently), NOT yet reduced to one provable branch + diverging value.
The recomp-side static + ring analysis has bottomed out here; the exact
"why does the reveal fire before the loader finishes" needs either (a) an
oracle insn-trace of the **reveal trigger** path (what calls the final
`$8973` on HW and what it waits for) aligned by PC, or (b) tracing `$1F7A`
/ the `$94E7` loader-batch progress on HW. The oracle `emu_wram_timeseries`
tool is currently unreliable (f1-only); the working oracle signal is
`emu_get_insn_trace` milestone order (incremental tail dumps).

Captures (gitignored): `_d2_writes.json` (`$00D2`), `_d2_1f7a.json`,
`_d2_1f9b.json` (red herring), `_d2_loadin.json`; `_cap_d2.ps1`,
`_cap_ogate.ps1`.

#### Session-5 (2026-05-30) — DECIDED: **REVEAL FIRES EARLY** (conclusion 2). Reliable oracle watch tooling stood up; reveal caller pinned both sides; gate is NOT late.

The session-4 "gate-too-late vs reveal-too-early" question is **resolved: the
reveal fires early.** Decided with reliable, hook-based oracle observability
(NOT the f1-only `emu_wram_timeseries`, which stays unreliable).

**Tooling (Task A — validated, mostly already existed).** The decisive
observers are the always-on hook tools, queried (not armed-then-hoped):
- `emu_block_watch_arm <pc24> <ramoffs> [maxhits]` / `emu_block_watch_get`
  — captures regs (A/X/Y/S/D/DB/PB/P) **+ up to 8 WRAM bytes on every entry**
  to a bank-`$80` PC. This *is* the per-milestone watch with state.
- `emu_wram_writes_at <addr>` — per-write `(frame,pc,before,after)`, reliable.
- **NEW (bounded, runner-only, no regen):** added a **caller return address**
  (`"ret"`) to the oracle block-watch hit — read off the stack at entry
  (`snes9x_bridge.cpp` `emu_block_watch_hit.r_ret` +
  `snes9x_bridge_block_watch_get_ret`; emitted by
  `emu_oracle_cmds.c h_emu_block_watch_get`). Gives the reveal caller directly.
- Recomp side already mirrors this (`block_watch_*`, `wram_writes_at` with
  `func`/`parent`, `boundary_get` with `depth`/`PB`/`S`).
- `EnableSnes9xOracle=true` for the from-boot reference; left **false** after.
  Capture scripts (gitignored): `_owatch.ps1`, `_owatch2.ps1`, `_owatch3.ps1`
  (definitive same-run), `_rwatch.ps1`. Build: `Oracle|x64`, runner-only.

**Oracle order (one clean run; the load is bracketed by BLANK):**
`$91B5` highway-init runs **while still bright** (b3=`0x0f`, `$1F7A`=`0x23`) →
screen fades-OUT then **forced-blank** (b3→`0x80` via `$8089ba`) → under blank:
`$94E7` clears `$1F7A` → gate **`$00D2`→2** (`$809571`) → cityscape `$991B` →
FG `$9989` (`$00D2`→4) → **reveal fade-IN at `$80898e`, dispatched from `$89C9`
via the task JMP-table at `$80:80E6`, only AFTER the stream (`$00D2`=`0x04`).**
So hardware = **load/stream UNDER BLANK, then reveal.** `$8973` is the fade
*engine*; the highway reveal enters it from `$89C9` (a deep, PB=`$80` dispatch),
NOT from `$91B5`.

**Recomp order (SAME run, internally consistent):** the highway **scene-init
`$8973` fade-IN fires EARLY** — `bank_00_8973` at f865 (b3 `0x80`→`0x0f`),
called **directly from Task0 at depth 1, PB=`$00`** (the `$91B5` init path) —
*before* the gate/stream. b3 reaches `0x0f` at f891 and **is never written
again** (no re-blank). Only *then*: `$94E7` clears `$1F7A` (f976), gate
`$00D2`→2 (f979), cityscape `$991B` (f981), FG `$9989` (f1074–1198) — all on a
**lit** screen → the visible build-in. **The recomp DOES reach the correct deep
reveal — `Task_89C9`→`$8973` at f1199 (depth 3, PB=`$80`) — but it is a NO-OP**
because b3 is already `0x0f`.

**Why "reveal early," not "gate late" (the clincher):** the recomp's
gate/stream/`$89C9` sequence is intact and correctly ordered relative to the
load (`$94E7`→`$00D2`→2→`$991B`/`$9989`→`$89C9`); the gate even opens *earlier*
in the recomp's own timeline (f979) than on hardware (f1148). Nothing is late.
The one displaced event is the **scene-init fade-IN** (Task0→`$8973`, f865),
which lights the screen before the load and pre-empts the proper post-stream
`$89C9` reveal (which then no-ops). It is an **order reversal with ~equal
spacing** (recomp reveal→gate ≈ +114f; oracle gate→reveal ≈ −121f), i.e. a
clean flip — and the side that is mis-placed is the **reveal**.

| event | oracle order | oracle pc | oracle state @event | oracle reason | recomp order | recomp pc | recomp state @event | recomp reason | concl |
|---|---|---|---|---|---|---|---|---|---|
| highway scene-init `$91B5` | 1 (f988) | `$80:91B5` | b3=0f, d2=0, 1f7a=23 | runs while bright; does NOT reveal highway | 1 (f753→fade f865) | `bank_00_91B5`→`$8973` | b3 0x80→0x0f | **its fade-IN lights screen (reveal)** | — |
| forced blank | 2 (f1137) | `$80:8089ba` | b3→0x80 | blank held for load | — | (absent after f891) | b3 stays 0x0f | **no re-blank** | div |
| `$94E7` clears `$1F7A` | 3 (f1148) | `$80:94E7` | b3=80, 1f7a 23→00 | loader, under blank | 3 (f976) | `bank_00_94E7` | b3=0f, 1f7a 23→00 | loader, on lit screen | same logic |
| gate `$00D2`→2 | 4 (f1148) | `$80:9571` | b3=80 | stream gate opens (blank) | 4 (f979) | (gate path) | b3=0f | gate opens (lit) — **not late** | — |
| bulk stream `$991B`/`$9989` | 5 (f1150–1171) | `$80:991B`/`9989` | b3=80, d2→04 | **streams UNDER BLANK** | 5 (f981–1198) | `bank_00_991B`/`9989` | b3=0f | **streams on LIT screen** | div=visible |
| reveal `$8973` (deep) | 6 (f1269) | `$89C9`→`$8973` | b3 0x80→0f, d2=04 | **reveal AFTER stream** | 6 (f1199) | `Task_89C9`→`$8973` | b3 already 0f | **NO-OP (already revealed)** | **reveal early** |

**Conclusion: #2 — reveal fires early.** Root hunt (next) = the **reveal
trigger/caller path**: why the recomp runs the `$91B5`/Task0 scene-init
`$8973` fade-IN (lighting the screen) at f865 while b3 is blank, instead of
holding the blank and deferring the reveal to the `$89C9` post-stream dispatch
(`$00D2`=`0x04`) as hardware does. Equivalent framing: the recomp does not hold
forced-blank through the stage-load; find the branch/sequence in Task0's
highway-object init that emits the fade-in ahead of the load, vs hardware's
keep-blank-until-`$89C9`. NOT width (M1X1 throughout), NOT the gate, NOT
display-side. **Do NOT patch** until that single sequencing edge is shown.

Captures (gitignored `_o*`/`_rw*`): `_o3_obw.json` (oracle block-watch+ret),
`_o3_o_*.json` (oracle writes), `_o3_rbnd.json`/`_o3_rwin.json` (recomp
boundary), `_o3_r_*.json` (recomp writes).

#### Session-6 (2026-05-30) — REFINED: not "reveal early" at the shallow fade — it is a **MISSING RE-BLANK** before the bulk load (answer C). The shallow `$91B5` fade is correct on BOTH sides.

Drilled the shallow vs deep `$8973` callers and the blank path. **The Session-5
"reveal early" framing is sharpened, not overturned:** the displaced event is
NOT the shallow fade (both sides do it and it legitimately reveals) — it is the
**forced-blank that hardware re-applies before the bulk highway load and the
recomp does not.**

**`$8973` census (both sides, same runs):** `$8973` is the fade ENGINE, entered
from two sites. (1) SHALLOW: `$91B5`→`$8973` at `$80:9269` — the highway
**object-init** fade; it is reached on BOTH (oracle f1023, recomp f853) **from
b3=`0x80` (blank)** and on BOTH it ramps b3→`0x0f` (reveals). Identical → NOT the
bug. (2) DEEP: the `$89C9` dispatch (via the `$80:80E6` scheduler JMP-table) →
`$8973` — hardware's post-stream reveal; the recomp reaches it too (`Task_89C9`
f1170) but it **no-ops** (b3 already `0x0f`).

**The real divergence — the per-frame scene fade-controller (`$8C5x`) and its
forced-blank:**
- Oracle b3 cycle for the highway: shallow reveal (`$91B5` f1023) → **fade-OUT
  `$8995` (f1089)** → **forced-blank `$8089b8/ba` (f1119, b3→`0x80`)** → bulk load
  UNDER BLANK (gate `$00D2`→2, `$991B`/`$9989`) → **deep reveal `$89C9` (f1232+)**.
- Recomp: shallow reveal (`$91B5` f853→`0x0f`@879) → **b3 NEVER written again**
  (no fade-out, no blank) → bulk load on the LIT screen → deep `$89C9` no-op.
- **Execution census (`emu_block_watch`/`block_watch`, same run `_owatch6`):** the
  scene fade-controller `$8C58`/`$8C5B`/`$8CB3` runs on the oracle **every frame**
  across f1014–1088 & f1169–1228 (136 hits); fade-OUT `$8995` fires f1089/f1197;
  blank `$8089ba` f1119/f1227. On the **recomp**: `$8C5B` runs **once @f645**,
  `$8995` **once @f614** — **NEITHER in the highway-load window.** So the recomp's
  per-frame fade-controller is **dormant** during the load.
- **Oracle blank dispatch path (insn-trace into f1119):** the `$80:8080`
  cooperative-scheduler dispatcher (`$80AB LDA slot,X; CMP; JMP/branch`) resumes a
  fade task into `$8089b4`→`$80899b`, where `LDA $..; AND #; BEQ $89b6` selects
  the set-blank branch (`LDA #$80; STA $00B3`). So the re-blank is produced by a
  **per-frame fade task driven by the `$8080` scheduler** — the same scheduler
  family as the Option-1 work.
- **Recomp scheduler slots in the window (`loadin_slots`):** only Task0 (`$852C`)
  + vblank (`$B25B`) (+transient `$E6B1`) are active; **no fade-controller slot**.
  The recomp CAN fade/blank (it does so for the intro cutscenes, f27–879, and a
  pre-highway cutscene in `_owatch7` loads-under-blank-then-reveals correctly) —
  so the capability exists; what is missing is running the scene fade-controller
  task that holds blank through the **highway bulk-load** phase.

| event | oracle order | oracle pc / caller | oracle b3 | oracle d2 | recomp order | recomp pc / caller | recomp b3 | recomp d2 | conclusion |
|---|---|---|---|---|---|---|---|---|---|
| shallow fade (object-init) | f1023 | `$8973`←`$91B5`/`$9269` | 0x80→0f | 00 | f853 | `$8973`←`$91B5` | 0x80→0f | 00 | SAME (not bug) |
| fade-controller per-frame | f1014–1228 | `$8C58`/`$8C5B`/`$8CB3` (136×) | — | — | — | (1× @f645 only) | — | — | **recomp dormant** |
| fade-OUT | f1089 | `$8995` | 0f→00 | — | (none in window) | `$8995` @f614 only | — | — | **divergence** |
| forced re-blank | f1119 | `$8089b8`←`$8080` sched | 00→0x80 | — | (none) | — | stays 0f | — | **MISSING (root)** |
| gate `$00D2`→2 | f1148 | `$9571` | 0x80 | 0→2 | f979 | gate path | 0f | 0→2 | gate not late |
| bulk stream | f1150–1171 | `$991B`/`$9989` | 0x80 | →4 | f981–1198 | `$991B`/`$9989` | 0f | →4 | lit vs blank |
| deep reveal | f1232 | `$89C9`→`$8973` | 0x80→0f | 04 | f1170 | `Task_89C9`→`$8973` | 0f (no-op) | 04 | reveal wasted |

**Answer: C** — the shallow `$91B5`→`$8973` fade is acceptable (both sides reveal
there); the recomp **misses the forced-blank** that hardware re-applies before the
highway bulk-load. The first divergent edge is localized to the **per-frame scene
fade-controller task (`$8C5x` → `$8995` fade-OUT → `$8089b8` blank), driven by the
`$80:8080` cooperative scheduler on hardware, NOT being run by the recomp's
scheduler during the highway-load window** (only Task0+vblank run; the fade-
controller slot is absent/dormant). This is the **scheduler-faithfulness /
task-scheduling** family (same root class as Option-1 `cpu->S`/dispatch), **not** a
single WRAM-state branch (so not D) and **not** the gate (`$00D2`/`$953F` open on
time). **Still NOT reduced to one provable instruction edge** — the open root is
*why the recomp's cooperative scheduler does not run the scene fade-controller
task in this window* (which slot it should occupy, and the install/resume/
countdown branch that drops it). **Do NOT patch** until that is shown.

CAVEAT (frame variance): absolute game-frames vary run-to-run (HLE boot + wall-
clock-paced START presses), and `cgadsub=0xbd` can match more than one transition;
all cross-side claims above use **same-run** oracle-vs-recomp captures
(`_owatch3`/`_owatch6`). `_owatch7` reached only a pre-highway cutscene (Task0+
vblank only, no `$91xx`/`$953F`) so it is off-target for the bulk-load bug.

Captures (gitignored): `_o4_*`/`_o6_*`/`_o7_*` (census, blank dispatch path,
slot table). Bounded tool add (uncommitted, runner-only): oracle block-watch
caller `ret` (see Session-5).

#### Session-7 (2026-05-30) — ROOT CAUSE PROVEN + FIXED: the fade-OUT/blank task `$89DB` was a MISSING cfg task-entry → scheduler dropped it (answer D). One-line config fix + dispatch case.

The "scheduler doesn't run the fade-controller" (Session-6) is now reduced to a
single proven edge and fixed.

**Scheduler model.** The recomp replaces the asm cooperative scheduler
(`$00:8099`/`$80E6 JMP ($0032,X)`) with a C-host fiber scheduler
(`mmx_rtl.c MmxSchedulerTick`). It reads the **WRAM slot table**
(`$0030+slot<<4`: state; `$0032+x`: handler PC; `$0036+x`: saved S; 7 slots,
bases `$013F/$017F/$01BF/$01FF/$023F/$027F/$02BF`). Game code installs tasks via
`$00:813B` (at entry **A=handler PC**, **X=slot offset**; writes `$32+X`=handler,
`$30+X`=1). When the scheduler resumes a slot it routes the handler PC through a
**hand-written switch `mmx_dispatch_task_pc` (mmx_rtl.c:46)** to the recompiled
`Task_xxxx` body. Task entry PCs are **not auto-discovered** (they are
`LDA #imm` operands feeding `$813B`); they must be declared in `recomp/bank00.cfg`
as `func Task_xxxx <pc>` (the cfg comment says so explicitly).

**Install census (block_watch on `$813B`, A=handler/X=slot, same run, bug
reproduced — bulk FG `bg_lo=18944` on a LIT screen @f1230):**
- The highway fade-controller is **slot 4** (offset `$40`, state `$0070`).
- Oracle installs slot4 = **`$89DB`** (caller `$80:8C58`, the per-frame scene
  controller) = fade-OUT→blank, then slot4 = **`$89C9`** (caller `$80:94DB`, the
  loader) = reveal. Recomp installs the SAME: slot4 `$89DB`@f1131 + `$89C9`@f1144
  (D=0, so written exactly where the scheduler reads). **Install is NOT missing.**
- BUT recomp `$0089DB`/`$00899D` (fade-task body) = **0 hits** — the installed
  `$89DB` task **never executes**; `$0089C9` reveal ran (no-op, b3 already `0x0f`).

**Exact edge.** `Task_89C9_M1X1` is emitted (`$89C9`: `JSR $8973` fade-IN → die)
and is a `case` in `mmx_dispatch_task_pc`. Its **sibling `$89DB`** (the fade-OUT
half: `JSR $8995` → forced-blank `$8089b8` `b3:=0x80` → die) was **never declared
in `recomp/bank00.cfg`**, so the recompiler emitted **no `Task_89DB`** (no
`L_89DB` label, not in `mmx_dispatch_v2.c`), and `mmx_dispatch_task_pc` has **no
`case 0x89DB`** → resuming slot 4 with handler `$89DB` falls to `default` →
`RECOMP_RETURN_NORMAL` (silent drop). No fade-out → no re-blank → bulk BG paints a
lit screen. The reveal `$89C9` then no-ops because the shallow `$91B5` fade already
lit the screen.

Fade-task logic confirmed (gen `$899D`): `A=$00B3; if ((A & 0x0F)==0) goto $89B6
(b3:=0x80 BLANK) else ramp toward target Y`. So the blank only happens once the
fade-OUT (`$89DB`→`$8995`) ramps brightness to 0 — exactly the task that was
dropped.

| event | oracle: slot/state/handler/caller | recomp: slot/state/handler/exec | verdict |
|---|---|---|---|
| install fade-OUT task | slot4 state=1 handler=`$89DB` ← `$8C58` @f1108 | slot4 state=1 handler=`$89DB` ← (Task0) @f1131, D=0 | **same install** |
| run fade-OUT task | `$89DB`/`$899D` execute → `$8995` fade-out | `$0089DB`/`$00899D` = **0 hits (dropped at dispatch default)** | **DIVERGED (root)** |
| forced-blank | `$8089b8` b3→0x80 (under blank load) | never (b3 stays 0x0f) | diverged |
| install + run reveal | slot4 `$89C9` ← `$94DB`, reveals after load | slot4 `$89C9` runs but **no-op** (b3 already 0f) | reveal wasted |

**Conclusion: D (scheduler skip)** — the fade-OUT/blank task `$89DB` is installed
in slot 4 but `mmx_dispatch_task_pc` has no case for it (and the recompiler never
emitted a body), so the dispatcher silently drops it. Slot that should contain the
fade-controller = **slot 4**; reason it doesn't run = **undeclared task entry
`$89DB`**.

**Fix (smallest, config + glue; matches the cfg's documented mechanism):**
1. `recomp/bank00.cfg`: add `func Task_89DB 89db` (next to `Task_89C9`).
2. `src/mmx_rtl.c`: add `extern RecompReturn Task_89DB_M1X1(...)` + `case 0x89DB:
   return Task_89DB_M1X1(cpu);` to `mmx_dispatch_task_pc`.
3. Regen (emits `Task_89DB_M1X1` + dispatch entry) + rebuild Oracle.
NOT Highway-specific, NOT display-side, NOT M/X, NOT generated-code edit, NOT an
interpreter fallback. **Status: APPLIED + VALIDATED (2026-05-30).**

**Validation (recomp, post-fix, `_val89db.ps1`/`_val_highway.png`/`_val_play.png`):**
- Regen emitted `Task_89DB_M1X1` (entry `$0089DB`, `L_89DB`) + dispatch entry
  `{0x0089DBu,…}`; stub-lint = the same **242** known-v1.0.0 stubs (no new stubs);
  built `Oracle|x64` clean.
- **All 15 bulk-BG frames now upload UNDER BLANK (`ini=0x80`), LIT=0** — incl. the
  FG bulk `bg_lo=18944`@f1266 now blank (was LIT@f1230 pre-fix). `$0089DB` now
  executes (1 hit, was 0); blank step `$0089B6` runs in the load window (3 hits);
  deep `$89C9` reveal now meaningful (screen was blanked → reveals).
- **Visual:** highway reveals with the **complete distant cityscape + HUD already
  present, no black-chunk build-in** (matches the hardware/oracle reference). The
  opening ride cutscene plays normally afterward (not frozen, not over-blanked).
- Scope: change is **MMX-only** (`recomp/bank00.cfg` + `src/mmx_rtl.c` glue; regen
  re-emits only MMX `src/gen`). The shared recompiler and other games (SMW/ALttP)
  are **untouched** → no ecosystem regression risk from this change.
- Not yet exercised (broader regression matrix, recommended before ship): boss-
  select transition; heavy-load risky areas (Launch Octopus/fish, Chill Penguin,
  Dr. Light) — reachable only via longer play/save-states.

Captures: `_o9_*`/`_o10_*`/`_o11_*` (install census, fade-task exec, bug confirm).

### Music-rate ticking + occasional off-tune audio — dual-producer APU sample drop (filed 2026-05-30)

**Status: FIXED 2026-05-30 (ear-verified by user) — framework-side ring
buffer.** User report: a subtle **ticking sound in the music** plus
**occasional** (~5%) **notes that sound off** (not a constant pitch
error — a single note occasionally wrong), audible on the Highway after
the music starts. User flagged this as the same *class* as a fixed
PokemonStadiumRecomp bug ("the ticks ended up being some type of
truncation"). Confirmed: same class. After the fix the user reports the
"off-packet squelches" are gone.

#### Cross-reference (the analog)

`F:\Projects\n64recomp\PokemonStadiumRecomp` ISSUES.md "Music-rate
periodic tick — FIXED 2026-05-28": an N64 `AI_LEN_REG` that read 0
killed the game's audio-pacing feedback loop → audio **over-production**
→ a host sample-**decimation** valve dropped samples mid-waveform →
~4 clicks/s. Lesson: a producer/consumer imbalance in the audio path,
resolved by a sample **drop/truncation**, surfaces as a periodic click.

#### Root cause (MMX, code-confirmed)

The DSP output sample buffer is **fixed at 534 samples** and **silently
drops** anything beyond it:

```c
// runner/src/snes/dsp.c  (dsp_cycle)
if (dsp->sampleOffset < 534) {            // <-- HARD CAP
  dsp->sampleBuffer[sampleOffset*2]   = totalL;
  dsp->sampleBuffer[sampleOffset*2+1] = totalR;
  dsp->sampleOffset++;                    // samples past 534 are DISCARDED
}
```

That buffer has **two concurrent producers** (serialized by `RtlApuLock`):

1. **Audio thread** — `RtlRenderAudio` (`common_rtl.c`) cycles the APU
   `while (sampleOffset < 534)` then `dsp_getSamples` copies 534 and
   resets `sampleOffset = 0`. One drain per SDL audio callback.
2. **CPU thread** — `snes_catchupApu` (`snes.c`) loops `apu_cycle` on
   every APU-port touch (`RtlApuWrite`/`snes_readBBus`), to keep the
   SPC700 advanced for `$2140-$2143` port timing. `apu_cycle` runs
   `dsp_cycle` every 32 cycles → it **also produces output samples** into
   the same buffer.

When the game drives heavy APU-port traffic (feeding the music
sequencer / SFX), the CPU-thread catchup fills `sampleBuffer` to 534
**between** audio callbacks; further produced samples hit the cap and are
**dropped**. Each dropped run = a gap in the waveform = a **tick**; the
lost-sample timing jitter also shifts note phase → **occasional
off-tune**. Tick rate scales with port traffic, so it's more audible in
busy music — matching "subtly in the music," "hear it on highway." The
`if (sampleOffset < 534)` guard is the "truncation" the user recalled.

#### Ruled out

- **Output resampling / `g_frames_per_block` truncation.**
  `main.c` opens SDL with `allowed_changes = 0`, so `have.freq == 32000`
  (SDL resamples to the device internally). Then
  `g_frames_per_block = 534*32000/32000 = 534` and `dsp_getSamples`'
  `adder = 534/534 = 1.0` → an exact 1:1 copy, no per-block phase reset
  error. So the integer-truncation-on-non-32000-rate path is NOT the
  cause at the shipped config. (Secondary watch item: if a host is ever
  forced to a non-clean rate, `dsp_getSamples` is point/nearest-neighbor
  with a per-block `location = 0.0` reset — verify `have.freq` if
  symptoms change.)
- **ADPCM predictor / voice reuse.** (The PokemonStadium twin ruled this
  out via `adpcm_decode_recent`; the MMX path is the snes9x DSP and the
  defect is the buffer cap, upstream of decode.)

#### Fix (landed in `snesrecomp/runner/src` — framework-side)

Replaced the fixed 534-sample buffer with a power-of-two **ring** so the
CPU-thread catch-up can write ahead without dropping; the audio thread
consumes the oldest 534 (FIFO, continuous phase) per block. This
**self-balances**: `RtlRenderAudio` only produces the shortfall the
catch-up hasn't already supplied (`while (avail < 534)`), so total SPC
advance stays at the consumption rate and bursty production is *buffered*
rather than dropped. Eliminates the squelch (no drop) and the occasional
off-note (no mid-note discontinuity), and smooths the bursty SPC advance
into a steady output rate.

- `snes/dsp.h`: `sampleBuffer[534*2]` + `uint16 sampleOffset` →
  `sampleBuffer[DSP_SAMPLE_RING*2]` (`DSP_SAMPLE_RING = 8192`, ~256 ms)
  + monotonic `uint32 sampleWrite/sampleRead` (mask-indexed, wrap-safe).
- `snes/dsp.c`: `dsp_cycle` writes to the ring, drops ONLY on true
  overflow (audio thread stalled — never under normal pacing);
  `dsp_getSamples` consumes the oldest 534 by absolute read index;
  `dsp_reset` zeroes both counters.
- `common_rtl.c` `RtlRenderAudio`: gates on `sampleWrite-sampleRead` ≥ 534
  instead of `sampleOffset`.

**Verification:** ear-confirmed by user on the Highway — "not hearing the
off-packet squelches." No crash/regression to boot or gameplay; SPC
output ports active, frame advancing.

**Watch item:** if catch-up ever sustains over-production the ring fills
→ added latency, then drops resume (rare). Not observed. A drop /
high-water counter would make this an objective regression guard
(deferred).

**Ship path:** this is shared `snesrecomp/runner/src/snes/` runtime — the
edits currently live (uncommitted) in the `_mmx_snesrecomp` worktree
(snesrecomp `926d61e`, detached). To ship: commit framework-side (benefits
SMW/ALttP/all SNES titles), then rebuild MMX Production. Guard the other
games' audio for no regression. Same "fix belongs in the runtime fork"
conclusion as the PokemonStadium twin.

### Health-capsule consume softlock — fill-health routine never terminates (filed 2026-05-27)

**Status: RESOLVED 2026-05-28 — could not reproduce.** Re-tested on a
fresh boot of the v1.0.0 Production build: the fill-health routine
terminates normally. The original report was a stale-savestate artifact
(MMX repro requires a full relaunch from boot, never a loadstate — F1
loads desync). Originally observed on the `main` build (`c95930a`,
Option-1 + OAM-wipe fix). Picking up / consuming
a **health (life-energy) capsule** softlocks the game: the "refill health"
routine runs **forever** — the per-tick health-fill loop never stops, the
capsule never empties/decrements, so the game is stuck in the fill
animation indefinitely. Classic non-terminating-loop softlock.

Likely a loop whose exit condition (capsule amount reaches 0, or health
reaches max) never trips — i.e. the decrement/compare that should end the
fill isn't happening (a counter that doesn't change, or a compare reading
the wrong byte). Could be Option-1-related (a mis-pop perturbing the
loop's counter/flag) or an independent decoder/loop bug — **undiagnosed**.
NOT yet reproduced under instrumentation. To do next time: catch the
spinning loop's PC via the boundary/block ring, find the health-fill
counter var and why its terminating compare never fires.

### Sprite / OBJ-layer dropout + tile artifacting under heavy sprite load (filed 2026-05-26)

**Status: OPEN (much reduced) on `feat/cpu-s-stack-model`.** The
heavy-load **scheduler softlock / black-screen is RESOLVED** — the build
boots fully and the game RECOVERS from heavy load (X dies/respawns, no
freeze). What remains is a **brief, self-recovering sprite/OBJ-layer +
HUD dropout**: after destroying a fish and it exploding, X + the fish +
the HUD vanish for a moment, then re-appear (the HUD even partially
re-appears while X is inside a fish). This is the milder original
"artifacting/dropout" variant — same class as the armored-armadillo
digger area. Repro is unchanged (the `_fastfire.ps1 -loadslot 1` recipe
below). See "Resolution progress (Option-1)" immediately below for what
was fixed and what remains.

#### Resolution progress (Option-1 cpu->S model) — 2026-05-26

Root cause of the softlock/blackout: snesrecomp Option-1 models JSR/JSL
return frames on `cpu->S`. Three places leaked `cpu->S`; each leak
accumulated until S drifted into zero page, where a stack push / the
`D56F` subtree's D-relative writes (D=$0098) landed on the DMA queue tail
`$00A5/A6` (= a code-address-looking value like `$D665`), so the NMI
walker `82C8 → JMP $B9F3 → $BA48` (the same `BCS`-self trap as the Dr
Light freeze) spun on an unwalkable tail → watchdog → softlock/blackout.

Three coordinated fixes (all on `feat/cpu-s-stack-model`):

1. **Interrupt-frame ABI** — `cpu_push_interrupt_frame()` (snesrecomp
   `runner/src/cpu_state.h`), called before `I_NMI`/`I_IRQ` in MMX
   `src/mmx_rtl.c`. Models the hardware interrupt-entry push so the
   handler's `RTI` pop balances. Without it `cpu->S` drifted to page 2.
2. **Cooperative-yield frame pops** — the yield HLEs
   (`HleMmxYieldOneFrame`/`NFrames`/`Vblank` in MMX `src/gen_stubs.c`)
   now pop the 2-byte Option-1 JSR return frame (`cpu->S += 2`) that the
   coroutine RESUME's RTS pops on hardware. Fixed the **boot wedge**
   (`cpu->S` was leaking 2B/yield into zero page during boot).
3. **Dispatch miss-restore** *(the heavy-load fix)* —
   `cpu_dispatch_pc_from`'s lookup-miss path restored `cpu->S = _entry_s`
   (bare), but the RTS/RTL had already popped its return frame, so this
   **under-popped by `frame_size`**, leaking the caller's frame on EVERY
   miss. An `host_return_valid=0` callee dispatches on every RTS; under
   heavy load the boundary `dispatch_log` showed **205 misses/burst**
   (vs 3 at boot) in the `D56F/D5DF/9A6A/9976/9E68` enqueue subtree →
   2B/frame `cpu->S` drift → the softlock. Fix: `_emit_return` now passes
   `_entry_s + frame_size` (codegen.py) so a miss restores the
   post-return-pop S. This is what reduced the softlock/blackout to the
   transient dropout.

**Remaining (this OPEN issue):** the brief OBJ-layer + HUD dropout after
a fish explosion. cpu->S no longer net-drifts to zero page, so it is no
longer the walker-spin softlock. Likely a narrower per-frame OAM/DMA
throughput or a smaller transient stack/state perturbation under the
explosion's heavy load — to be pinned next (the `s0drift` diagnostic in
`mmx_rtl.c`, gated on `MMX_RTL_DIAG`, shows `cpu->S` now oscillates and
recovers rather than monotonically drifting). The historical
softlock-era analysis below is retained for context.

#### Root-cause investigation — 2026-05-27 (fish-explosion OAM/HUD wipe)

Pinned the transient OAM/HUD blank to a **shared-tail RTS return-target
divergence** in the Option-1 model. Evidence captured with two focused,
always-on, tripwire-frozen traces (see "Observability added" below).

**CONFIRMED (data-backed, from the RTS-decision + WRAM-write rings):**

- The blank is `D56F`'s OAM park loop (`$D5C1`) clearing slots
  `[$E4, $E3)` to `Y=$E0`. At the wipe frame the park reads start
  `$E4 = 0`, end `$E3 = 0x80` → it parks **all 128 OAM slots** (every
  sprite + HUD).
- `$E4` (the per-frame OAM-slot count) **legitimately reaches `0x80`
  (128 = OAM full)** under the explosion load. Normal frames peak
  `~0x7c`. So over-allocation is NOT the bug — do **not** cap `$E4`.
- When the build fills OAM, `D6A7` takes its **overflow exit**
  (`$D74E CMP X,#$1F ; BCS $D765 ; JMP $D5CC`) into the shared finalize
  `$D5CC` (`$E3 = $E4 = 0x80 ; $E4 = 0`).
- The `$D5CC` finalize is **duplicated** into both the `D56F` and `D6A7`
  generated host functions. The `$D5CC` RTS (`$D5DE`) reached via the
  `D6A7` overflow path executes **in `D6A7`'s copy**: `entry_s = 0x132`
  (D6A7's frame) but the live stack `ret_s = 0x138` → `s_eq = 0` → it
  cannot host-return. It dispatches on the popped PC `$9AA6`; `$9AA6` is
  a host-return continuation (not a dispatch entry, `found = 0`) → it
  classifies as **`MISS_UNWIND`** (restore S, return `RECOMP_RETURN_NORMAL`).
- The Option-1 miss-unwind returns `NORMAL` **one host-C frame** (to
  `D6A7`'s caller). The popped PC `$9AA6` actually lives **several**
  frames up — it is the continuation after `JSR $D56F` at `$9AA3`. So
  `D625`/`D56F` wrongly resume and run `D56F`'s park (`$D5C1`) → wipe.
  (Confirmed by the trace showing `D56F`'s own `$D5DE` finalize running
  *after* the `D6A7` miss-unwind, i.e. `D56F`'s body executed post-reset,
  and by the earlier WRAM ring showing `D56F` parent-`9A6A` writing `$E0`.)

Decision table (wipe build frame; `src=$D5DE` is the `$D5CC` RTS):

| func copy | entry_s | ret_s | s_eq | popped | found | decision | E3 | E4 |
|---|---|---|---|---|---|---|---|---|
| `D6A7_M1X0` (overflow) | 0132 | 0138 | 0 | `$9AA6` | 0 | **MISS_UNWIND** | 80 | 00 |
| `D56F_M1X1` (normal)   | 0138 | 0138 | 1 | `$9AA6` | 0 | HOST_RETURN | 00 | 00 |

Normal (non-wipe) frame: a single `$D5DE` RTS in `D56F_M1X1`,
`entry_s==ret_s==0x138`, HOST_RETURN, `E3=0x7c`. No `D6A7`-context
finalize.

**THEORY (not oracle-confirmed — snes9x oracle is disabled for
save-state repros):** on hardware the overflow `JMP $D5CC ; RTS` pops
`$9AA6` and returns *out of the entire OAM routine* (back to `9A6A`),
**skipping the park** — OAM retains its 128 built sprites and renders
full (consistent with the user's report that the original *lagged* here
but never blanked the HUD). Our recomp diverges because "return to
`$9AA6`" is a multi-level non-local return that the one-frame miss-unwind
does not perform.

**Root-cause class (THEORY for the general fix):** a `JMP` into a shared
tail block ending in `RTS`, where the RTS's popped return targets a
**far-ancestor** continuation (multi-level non-local return). The
Option-1 RTS-dispatch-miss unwinds exactly one host-C frame, so
intermediate frames execute code hardware never reaches. Any routine
with a "bail to shared epilogue then RTS out several levels" idiom is
exposed to this class.

**Ruled out this session:** (1) declaring the `$9AA6`/`$D6A6`/`$D3EC`
dispatch-miss targets as cfg `func` entries — those misses are the
Option-1 *unwind mechanism*, not graph failures; declaring them did not
change the wipe and **regressed** into a sustained black screen
(reverted). PRINCIPLES §13a "add miss targets to cfg" does NOT apply to
host-return-continuation misses in this calling model. (2) `$E4`
over-count (it legitimately reaches `0x80`).

**Observability added (`feat/cpu-s-stack-model`, runner + RTS lowering,
nothing in `src/gen/`):**

- **RTS/return-decision trace** — `dbg_rts_trace()` emitted once per
  RTS/RTL by `_emit_return` (codegen.py), recorded in `debug_server.c`.
  Classifies each RTS as `HOST_RETURN` / `DISPATCH` / `MISS_UNWIND` with
  `entry_s`/`ret_s`/`s_after`/popped PC/`hrv`/`found`/`$E3`/`$E4`.
  Commands: `rtstrace_range <lo> <hi>`, `get_rtstrace [from to limit]`.
  Uses `cpu_dispatch_has_entry()` (cpu_state.c) for a side-effect-free
  dispatch-table probe.
- **PC-range block-path trace** — `dbg_oam_block_trace()` hooked in
  `cpu_trace_block`. Per-block `pc`/`func`/`parent`/`depth` + full regs
  (A,X,Y,S,D,DB,PB,P,mx) + `$E3/$E4/$E6/$E8/$EA/$EB`. Commands:
  `oamblk_range <lo> <hi>`, `get_oamblk [from to limit]`.
- Both are PC-range-filtered and frozen by the same `g_boundary_frozen`
  tripwire the `[oamdrop]` detector trips (also unified the WRAM-write
  ring onto that freeze this session).

**Proposed fix (THEORY — not yet implemented):** make the shared-tail
RTS resolve the popped PC against the *dynamic* stack rather than the
enclosing host function's `entry_s`. Candidate approaches under
evaluation — see the fix proposal appended at the end of this file.

#### Summary (TL;DR)

Under heavy sprite load the game **softlocks**, but it *looks* like a render
glitch because NMI keeps running (frame counter advances, background still
scrolls) while the game-logic foreground is frozen. Chain:

1. The foreground runs the DMA-queue-enqueue subtree
   `bank_00_D56F → D625 → D6A7 → D76A` (the same code as the resolved Dr
   Light freeze).
2. The recomp puts **JSR/JSL return frames on the host C stack, not
   `cpu->S`**, while `PHP/PLP/PEI` operate on `cpu->S`. A callee in that
   subtree touches the *absent* return frame, writing into the slot where the
   balanced `D56F` saved its P with `PHP`.
3. `D56F`'s exit `PLP` reads that clobbered byte → **m flips 1→0**.
4. The wrong m=0 propagates to the task dispatcher `bank_00_953F`, which then
   runs the **wrong `M0X0` task variant** (a do-nothing path) every frame
   forever → object engine never runs → OAM frozen → sprites + HUD vanish.

**One root cause, three symptoms:** this fish softlock, the (resolved-as-
symptom) Chill Penguin softlock, and the (resolved-as-symptom) Dr Light
freeze are all the same `cpu->S`-vs-host-C-stack divergence in this subtree.
**Fix:** snesrecomp `IMPROVEMENTS.md` **Option 1** — model JSR/JSL return
frames on `cpu->S`, RTS/RTL as pop-and-dispatch (attempted on branch
`feat/cpu-s-stack-model` — see "Fix attempt #1" below). Point patches per site
are stubs and were disproven by Dr Light's `force_variant_at` experiment.

#### Fix attempt #1 — Option-1 cpu->S model (2026-05-26) — NOT landed, parked on branch

Implemented the full Option-1 `cpu->S` JSR/JSL return-frame ABI (+ ChatGPT's
`host_return_valid` refinement) on snesrecomp branch `feat/cpu-s-stack-model`.
**Full implementation detail + the staged diagnosis is in snesrecomp
`IMPROVEMENTS.md` on that branch** (committed). Outcome:

- **Compiles + runs**, but **regresses boot**: wedges by ~frame 448–456 in the
  NMI DMA-queue walker `bank_00_82C8` (watchdog trips every frame → black
  screen). Multi-layer, not a clean fix.
- **Layers fixed:** (1) `cpu->S` page-2 drift; (2) interrupt-frame ABI — `RTI`
  pops the native/emu frame the NMI/IRQ entry pushes, AND `mmx_rtl.c` enforces
  the cpu->S-balanced invariant around `I_NMI`/`I_IRQ` (save S before, restore
  after). After these, `cpu->S` stays in page 1 (`$01ff`).
- **Still broken:** the DMA-queue tail `$00A5/A6` is corrupted to `$B30E` — a
  code-address-looking value (stray return-frame write), which `82C8` can't
  walk → spin. NOT from cpu->S drift (S is page 1 now), so a *different*
  Option-1 stack-correctness failure mode writes the bad value there. The
  `$00A5` writer was not pinned (the armed WRAM-watch produced no hit in the
  captured stderr; next session should `wram_writes_at 0xa5` with `trace_wram`
  armed on `$00A5`).
- **Remaining failure modes (boundary-ring per-function S-deltas):** IRQ chain
  (`I_IRQ`/`IrqHandler` −14, `84C3` +13 — path-dependent: early-exit vs full
  handler); NMI DMA-processing chain's cross-function stack protocol
  (`83D9` dispatcher `JMP ($83EB,X)` → `83F1` / `8428`, where `8428` ends in
  `BRA $83FC` back into `83F1`'s shared tail → `81E3`/`82C8`).
- **How to resume (for whoever picks this up):** `git checkout
  feat/cpu-s-stack-model` in snesrecomp; read `IMPROVEMENTS.md`. The
  empirical loop is regen (~7min) → build Oracle (~5min) → boot → boundary-ring
  S-delta (socket script: pair entry/exit by `entry_seq`, flag non-`+2`/`+3`).
  Find the `$00A5=$B30E` writer first — that's the actual wedge cause now that
  cpu->S no longer drifts.

**Safe state (2026-05-26):** snesrecomp reverted to `main`, MMX `src/mmx_rtl.c`
reverted, `src/gen` regenerated from `main`, Oracle rebuilt. Working game =
Production v0.1.1 (boss-freeze fix; untouched throughout). Option-1 lives only
on `feat/cpu-s-stack-model`.

#### Symptom

Two observed forms, same trigger — high simultaneous sprite count (many
enemies / projectiles on screen). **The frame counter keeps advancing (NMI
still fires), so it superficially looks like a rendering glitch — but the
game-logic foreground is actually wedged (a scheduler softlock); see the
deeper findings below.**

1. **OBJ-layer dropout** (reproduced 2026-05-26, underwater Launch Octopus
   area). X, the remaining enemies, AND the HUD (health + weapon/ammo bars)
   all vanish simultaneously. Screenshots:
   `issue-screenshots/repro_fish_obj_dropout.png` (dropout) vs
   `issue-screenshots/repro_fish_slot1_clean.png` (clean, same scene).
2. **Sprite tile artifacting.** Sprites show wrong / stale tile pixels
   (X garbles while being swallowed; the tunnel-digger area shows it too).
   Screenshots: `issue-screenshots/mmx_iWNgPciVA4.png` (underwater fish),
   `issue-screenshots/mmx_B8BSw5KDfX.png` (tunnel-digger, HUD still present
   — captured before the drop).

These are probably two faces of one underlying problem (the sprite engine
mishandling heavy load): "hide everything" (dropout) vs "show objects whose
tiles never loaded" (artifacting). Not yet confirmed they share a cause.

#### Working repro (2026-05-26) — underwater fish area

Save state **slot 1** (`saves/save1.sav`, repo-root cwd — the runner
`SwitchDirectory`'s to the repo root where `mmx.ini` lives, so saves resolve
there, NOT `build/bin-x64-Oracle/saves/`) lands in Launch Octopus's
underwater area with the fish enemies.

1. Launch Oracle, drive boot → Highway intro stage (Start-spam, as in the
   resolved repros). The loadstate must graft onto an already-initialised
   in-game scheduler.
2. Fire the coupled loadstate + rapid-fire burst with the new persistent-
   connection helper (NOT `_dbg.ps1`, which reconnects + sleeps 300ms per
   call → ~370ms/shot, far too slow; X dies before the burst lands):

   ```powershell
   F:\Projects\snesrecomp\MegamanXRecomp\_fastfire.ps1 -loadslot 1 -count 45 -button y
   ```

   `_fastfire.ps1` opens ONE connection, sends `loadstate 1`, then streams
   ~45 `set_controller p1=y` / `clear_controller` pulses (~22ms each ≈
   22 shots/sec). X is swallowed by a fish on load; the burst kills it fast,
   X is swallowed again, that fish dies too.
3. Screenshot. **NOTE:** for THIS repro, reloading the save on the same
   running instance is safe (no full relaunch) *as long as it hasn't fully
   frozen/softlocked/crashed* — unlike the resolved freeze repros.

#### Is it "OAM overflow"? — what it is NOT

"OAM overflow" has a specific hardware meaning, and the evidence rules it
out:

- **NOT a hardware per-scanline sprite/tile overflow** (the PPU's 32-sprite
  / 34-tile-per-scanline cap). Two reasons: (1) `NoSpriteLimits = 1` in
  `mmx.ini` disables that cap in the runner's PPU; (2) decisively, a
  hardware overflow drops sprites at *render* time without altering OAM —
  but here the sprites are parked at **Y = 0xE0 inside OAM**, i.e. the game
  *software* wrote them off-screen. They aren't on-screen in OAM for the PPU
  to overflow on.
- So this is a **software** suppression — the game's own sprite manager
  hid them. Candidate game-managed resources that can "overflow":
  - **(a) OAM-slot allocation pool** — the engine hands out OAM slots per
    frame; once exhausted, leftover objects are parked at Y=0xE0.
  - **(b) dynamic VRAM sprite-tile cache** — objects whose tiles can't be
    cached are hidden to avoid showing garbage (this is also the natural
    explanation for the *artifacting* variant, for objects where hiding
    fails to fire).
  - **(c) corruption** — a clobbered counter/pointer feeds Y=0xE0 into the
    OAM build loop (cf. the DMA-queue / D-relative corruption family from
    the resolved Dr Light freeze). Not an "overflow" at all, just looks
    like one.
  Which of (a)/(b)/(c) it is — unknown.

#### What we KNOW

- **PPU is innocent.** `get_ppu_state` at the dropout frame: `inidisp=0x0f`
  (full brightness, forced-blank OFF), `screenEnabled` TM=`0x17` (OBJ bit4
  set — sprites enabled on the main screen), `obsel=0x03`. No blanking,
  no window suppressing OBJ.
- **The defect is in OAM contents.** `dump_oam` shows a full OAM table where
  ~every entry has **Y = 0xE0 (224)** — the conventional off-screen "hidden"
  park value. Only sprite 0 is on-screen. The game's per-frame OAM build
  left almost everything parked off-screen → nothing renders.
- **It is global to the OBJ layer**, not per-object: X, enemies, and HUD all
  go at once → whatever hides them acts on the whole OAM build pass.
- **Not a freeze.** Scheduler alive, frame counter advances.
- **Trigger is heavy simultaneous sprite load.**
- The OAM shadow buffer is at WRAM **`$0700`** (low table $0700–$08FF, high
  table $0900–$091F). Confirmed two ways: content-match of the PPU OAM dump
  against a WRAM dump, and the OAM DMA's post-transfer A-bus pointer `$0920`
  = `$0700 + 0x220` (544 bytes). The earlier `read_ram 0x920` mismatch was
  just reading one byte past the buffer.

#### Deeper finding (2026-05-26, cont.) — it is a SCHEDULER SOFTLOCK; OAM-freeze is downstream

Activated the always-on WRAM write-trace ring scoped to the OAM buffer
(`trace_wram 0x700 0x920`) and ran ~80 frames:

- **Zero writes to `$0700–$0920`.** The OAM buffer is NOT being re-hidden each
  frame — it is **completely frozen**, stuck at whatever it held during the
  overload moment (mostly Y=0xE0 + sprite 0). NMI still DMA-copies this stale
  buffer to PPU OAM every frame, which is why the sprites/HUD stay gone.
- Ring **validated** against `$0B9D` (NMI handshake): that DOES log a write
  every frame (`NmiHandler_M1X1` ← `I_NMI_M1X1`), so the empty OAM result is
  real, not a tooling blind spot.

So the Y=0xE0 OAM contents are a **downstream symptom**. The real failure is
that the **per-frame OAM build / object engine stopped running.** The
foreground is wedged:

- `call_stack` pinned at `["YieldOneFrame_M0X0"]` (depth 1) across repeated
  samples; `get_cpu_state`: A=X=Y=0, PC=0, DP=0, **mf=false xf=false**.
- Scheduler task-state bytes (`$0103` slot-0 state, `$1f80` sched scratch) get
  **zero writes** — the cooperative scheduler's foreground is not advancing
  any task. Only NMI runs, ticking the frame counter (which is why it *looks*
  like "running but mis-rendering" rather than a hard freeze).

**This is a scheduler softlock**, presenting with the *exact same terminus*
as the RESOLVED Chill Penguin kill freeze (`YieldOneFrame_M0X0`, m=0/x=0).
Since Chill Penguin's boss-kill trigger is confirmed fixed by v0.1.1, this is
most likely a **sibling reached by a different trigger** (heavy sprite load in
the fish area), not a regression of that fix — `YieldOneFrame_M0X0` is the
generic "foreground can't make progress" terminus, reachable by multiple root
causes. To be confirmed.

#### Root-cause CLASS identified (2026-05-26, cont.) — stack-balance leaks in the JSR/JSL frame family

Captured the boundary ring across the wedge (8M-entry ring,
`SNESRECOMP_BOUNDARY_RING_ENTRIES=8388608`). The foreground is NOT idle — it
runs ~258 function events at depth 10 **every frame**, and the call set + stack
pointers are **byte-identical frame to frame** (frames 9818–9831):

- `bank_00_D1ED_M1X1` enters at S=$13e every frame; `bank_01_812E_M1X1` at
  S=$13b; `bank_01_9ACC_M1X1` at S=$138. **Zero cross-frame drift.**
- So it is a **deterministic per-frame loop**: the same task chain reprocesses
  the same (frozen) game state every frame, never advancing it — which is why
  OAM (`$0700`) and task state never change. `call_stack` reads `YieldOneFrame`
  only because the query lands during the inter-frame yield.

Per-function stack (S) deltas (S_out − S_in, paired entry↔exit) show genuine
imbalances in the loop body:

| Function | S-delta | note |
|---|---|---|
| `bank_04_916A_M0X0` | **−4** (else 0) | **data-dependent** — leaks 4 bytes (two PEIs) on ~⅓ of calls, balanced otherwise. The Dr Light "path-B" signature. |
| `bank_01_9ACC_M1X1` | −3 | every call |
| `bank_01_812E_M1X1` | −3 | every call |

`$01:812E` disassembles to the exact idiom: `PHP; PHD; SEP #$30; PEA $0BA8;
PLD` (relocates DP to `$0BA8`), then `JSR $9ACC` / `JSR $9B2D` / `JSR $9FE1`,
then `JSR ($819F,X)` (absolute-indirect dispatch). This is the
**JSR/JSL ↔ RTS/RTL frame-size mismatch + PEI-trampoline + indirect-dispatch**
family documented in snesrecomp `IMPROVEMENTS.md` — the SAME systemic
divergence (recomp models JSR/JSL via the host C call stack, not `cpu->S`) that
produced the resolved Dr Light freeze.

The leaks do **not** accumulate across frames (the HLE per-slot fiber
save/restore resets `cpu->S` each frame — hence the stable S above). Instead,
within a single frame a leaked stack makes a `PLD`/`PLP`/`RTS` read displaced
bytes → wrong D/P/return → a control-flow decision that fails to advance the
game state. Inputs are identical every frame → identical wrong result → stable
infinite loop. The "sprite/HUD dropout" is just the frozen OAM that results.

**Confidence:** HIGH that this is a stack-balance modeling divergence in the
JSR/JSL/RTS/RTL/PEI family (the data-dependent −4 on `916A` is near-certainly a
recomp bug, not intentional ROM behaviour). NOT yet instruction-level proven
WHICH leaked byte corrupts WHICH branch to freeze progression — that needs
single-frame instruction tracing inside one loop iteration.

**Fix direction:** the complete fix is snesrecomp `IMPROVEMENTS.md` **Option 1
— model JSR/JSL pushes on `cpu->S` and emit RTS/RTL as pop-and-jump-via
`cpu_dispatch_pc`.** That hardens the entire family at once (this softlock +
the Dr Light class + every PEI-trampoline site). It is a major codegen/runtime
refactor (previously attempted and rolled back — see `IMPROVEMENTS.md`
"Abandoned path: full cpu->S model"), so it needs its own scoped effort. A
point patch per leaking site (916A/9ACC/812E) would be a stub and miss the
next site.

#### Transition captured + EXACT mechanism (2026-05-26, cont.)

Caught the precise trigger→wedge transition (user-triggered repro, full M0X0
variant) by walking the 8M boundary ring back to the onset frame. Transition is
frame **44523 → 44524**:

- **Frame 44523** (last normal frame, 744 real events): the foreground runs the
  full object engine, ending deep in the DMA-queue-**enqueue** subtree
  `bank_00_D56F_M1X1 → D625 → D6A7 → D76A` (X iterating a sprite table, ~16
  entries under heavy load). **This is the same D56F/D5DF/D6A7/D76A subtree as
  the resolved Dr Light freeze.**
- The unwind out of that subtree shows the corruption:

  ```
  EXT bank_00_D625_M0X0   m=1  P=0x21     (m still 1)
  EXT bank_00_D56F_M1X1   m=0  P=0x00     <-- m CLEARED here
  EXT bank_00_9A6A_M1X1   m=0
  EXT bank_00_9976_M1X1   m=0
  EXT bank_00_953F_M1X1   m=0             (task dispatcher exits at m=0)
  ENT YieldOneFrame_M0X0  m=0
  ```

  `D56F` is the **M1X1** variant (entered at m=1) but **exits at m=0**: its
  epilogue `PLP` restored a P byte with the m-bit clear (P `0x21`→`0x00`). The
  wrong m=0 propagates up through 9A6A/9976/953F unchanged.
- **Frame 44524**: the task dispatcher `953F`, now at m=0, runs
  `bank_00_953F_M0X0` (the WRONG variant — does nothing, X `0x0002`→`0x0404`,
  immediate exit) then `YieldOneFrame_M0X0`. The object engine never runs again;
  only NMI's DMA walker (82C8/83D9/83F1) executes → OAM frozen → sprites/HUD
  gone.

**Exact mechanism:** heavy sprite load drives many iterations of the DMA-enqueue
subtree (`D56F…`); somewhere in it the recomp's stack-modeling divergence
displaces the stack so `D56F`'s exit `PLP` reads the wrong byte → **m silently
goes 1→0**. That m=0 reaches the task dispatcher `953F`, which then dispatches
the **M0X0 variant** of the task body — a do-nothing/misdecoded path — every
frame forever. The "sprite/HUD dropout" is just the frozen OAM that results.

**This unifies all three issues into ONE root cause:**

- **Fish softlock (this issue)** — m=0 leaks out of the D56F subtree →
  wrong-variant (`M0X0`) dispatch at `953F` → foreground does nothing.
- **Chill Penguin softlock** (Resolved-as-symptom) — identical terminus
  (`YieldOneFrame_M0X0`; its open question was literally "why are m/x both 0 at
  the dispatch site"). Same mechanism.
- **Dr Light freeze** (Resolved-as-symptom) — same `D56F/D5DF/D6A7/D76A`
  subtree; there the displacement corrupted the DMA queue tail (`$A5/$A6`) → NMI
  walker hard-spin instead of a wrong-variant dispatch.

All three are the recomp modeling JSR/JSL/RTS/RTL/PEI via the host C stack
rather than `cpu->S`, so under the right load a `PLP`/`PLD`/`RTS` in this
subtree reads a displaced byte. The v0.1.1 point fixes resolved specific
*symptoms*; the underlying divergence is still live and keeps producing new ones
(this fish softlock being the latest). The complete fix remains snesrecomp
`IMPROVEMENTS.md` **Option 1 — model JSR/JSL on `cpu->S`**.

**Still not proven:** the exact instruction inside the `D56F` subtree that
displaces the stack (which leaking `PEI`/`JSL`/dispatch). That is the only
remaining unknown; it needs a single-frame instruction trace through frame
44523's `D56F` call, and it does not change the fix.

#### Fix-class CONFIRMED via generated C (2026-05-26, cont.)

Read the generated C for `bank_00_D56F_M1X1` (`src/gen/mmx_00_v2.c:17110`) and
its ROM disasm:

- ROM `D56F` is balanced/faithful: `PHP; PHB` at `$D56F`, the DMA-enqueue
  subtree (`JSR $D5DF/$D66B/$D69C/$D625`), `REP #$20` for a 16-bit STZ block,
  then `PLB; PLP; RTS` at `$D5DC`. On hardware the `PLP` restores the entry P
  (m=1).
- Generated C models `PHP`/`PLP` on **`cpu->S`** (`cpu_write8(cpu,0,cpu->S,
  cpu->P); cpu->S--`). But it emits every `JSR` as a **plain C call**
  (`switch(m/x){ bank_00_D7CE_M?X?(cpu); }`) that does **NOT** push a return
  address onto `cpu->S`.

That is the fundamental divergence: on real hardware return addresses and
pushed data share one stack and interleave; the recomp splits them
(returns→host C stack, data→`cpu->S`). So inside D56F's subtree the `cpu->S`
layout no longer matches hardware, and D56F's exit `PLP` reads a clobbered
byte → m flips 1→0.

**This is `IMPROVEMENTS.md` Option-1 territory (bucket b), NOT a targetable
M/X-propagation or decoder bug.** There is no honest point fix: D56F itself is
correct, and pinning/forcing m=1 (or the variant) is a stub that the Dr Light
`force_variant_at` experiment already disproved. The only correct fix is to
model JSR/JSL return-address pushes on `cpu->S` (and RTS/RTL as
pop-and-jump), routing **every** function-invoke path through a uniform push
— which is exactly the coverage gap that sank the previous Option-1 attempt
("several emit paths invoke callees without going through `_emit_call`").

#### What we DON'T know

- **Why the foreground wedges at `YieldOneFrame_M0X0`** — the central open
  question. Is it waiting on an NMI handshake flag that never arrives, or did
  a task corrupt the scheduler's resume state so nothing is runnable?
- **The trigger→wedge transition.** We caught the steady wedged state, not the
  frame it happened on. The boundary ring overwrites fast, so the transition
  must be captured right after the trigger (see Next step).
- **Whether it shares a root cause with the resolved Chill Penguin softlock.**
  Same terminus (`YieldOneFrame_M0X0`, m=0/x=0); Chill Penguin's boss-kill
  trigger is fixed, so this is likely a sibling — but unconfirmed.
- **Whether the artifacting variant (#2 in Symptom) is the same bug** as this
  full wedge, or a separate, milder rendering divergence.
- **Whether real hardware / an emulator wedges in the SAME scenario.** Hard to
  check: the snes9x oracle is disabled for save-state repros (see Resolved
  section); confirming parity needs an input-record/replay path or a from-boot
  route to the scene. (Almost certainly a recomp bug — real MMX does not
  softlock the underwater area under heavy fire.)

#### Next step — find why the foreground wedges

The OAM/render layer is understood. The open question is why the cooperative
scheduler parks at `YieldOneFrame_M0X0` and never resumes the object engine.
This needs the **transition** captured (the frame the foreground stopped
advancing) — the boundary ring overwrites quickly, so query it right after the
trigger, not 90k frames later.

1. Fresh relaunch — the instance is now softlocked, so reload-without-restart
   no longer applies. Boot → Highway → `_fastfire.ps1 -loadslot 1`.
2. Immediately `boundary_get` for the window around the wedge — find the last
   task the scheduler ran before it stuck at `YieldOneFrame_M0X0`, and what it
   was waiting on / what state it left. If the ring can't retain the
   transition, ENLARGE it (always-on ring; do not arm-and-hope).
3. Apply the resolved Chill Penguin methodology (same terminus): boundary ring
   + pmut ring to find where m/x got cleared / where the scheduler resume
   state diverged. Determine if it's the same class (stack/return corruption
   feeding a bad dispatch) or distinct.
4. First sub-question: foreground waiting on a never-arriving NMI handshake
   (`$0B9D`/`$0BA0`) vs. a corrupted slot table ($0100+). Check both at the
   transition frame.

#### Tooling added this session

- `_fastfire.ps1` — persistent-connection fast input driver (loadstate +
  rapid button burst with no per-call reconnect/sleep). Reusable for any
  future input-timing repro.

---

## Resolved (retained for regression)

## [RESOLVED 2026-05-26] Chill Penguin kill freeze — repro recipe (filed 2026-05-25)

> **Status: RESOLVED (2026-05-26).** No longer reproduces against the
> current build. The fix landed framework-side in snesrecomp
> ([`recompiler/v2/emit_function.py:385`](https://github.com/mstan/snesrecomp/commit/fe39fb7),
> shipped in MMX v0.1.1): the NLR detector now handles the
> `PLA* + cross-bank long-tail JMP/JML` pattern, so the simulated stack
> is no longer over-popped before a cross-bank tail-call. The original
> "misdecoded M0X0 variant / why are m/x both 0" hypothesis below is
> **superseded** — the bad m/x at the dispatch site was a downstream
> effect of the stack-pointer displacement, not a decoder bug. The
> diagnosis and repro recipe are kept for regression.

### Symptom

The recompiled game **softlocks** the moment Chill Penguin dies to X's
charged X-Buster shot. Distinguishing features vs the Dr Light freeze
below:

- **Cosmetic loop stays alive** — `get_frame` advances every poll,
  NMI and IRQ keep firing each frame. Not a watchdog trip.
- **Scene freezes mid-explosion.** Chill Penguin's death explosion
  sprite is rendered statically; no progression to the capsule /
  weapon-get / stage-clear sequence ever occurs. Screenshots taken
  4 s apart are byte-identical.
- `call_stack` parks at `["YieldOneFrame_M0X0"]` and `get_cpu_state`
  shows `mf:false, xf:false` (cpu->m_flag = cpu->x_flag = 0).
- Boundary ring is dominated by an infinite-loop signature: the slot 0
  task body re-enters `bank_00_94E7_M0X0 → YieldOneFrame_M0X0` once
  per frame at depth=1. `bank_00_94E7_M0X0` is a **misdecoded M=0
  variant** — the asm bytes `A9 02 8D 80 1F` are interpreted as
  `LDA #$8D02; BRA $951D` (M=0 16-bit immediate eating the next
  opcode) instead of the intended `LDA #$02; STA $1F80; …` (M=1
  8-bit). The M0X0 body does six STZs, loads a junk literal, then
  falls into an RTI and returns — never advancing the state byte at
  `$7E:D+$D1` (`$0103` for slot 0, D=$0032) from 0 to 2. State stays
  at 0 → dispatch keeps hitting case 0 (94E7) → loop forever.
- The `bank_00_94E7_M1X1` variant is the correct decode and DOES
  advance the state. The dispatch site at `bank_00_931F` / `932B`
  picks the variant from runtime `cpu->m_flag / cpu->x_flag`, so the
  root question is **why m/x are 0 at the dispatch site** when the
  asm-side convention is that the task body runs at M=1, X=1.

This is the next bug in the chain after the [Dr Light capsule
freeze](#dr-light-capsule-freeze--repro-recipe) below; both surface
in the same Chill Penguin stage but at different progression points.

### Build

```
Configuration: Oracle | Platform: x64
Output:        build/bin-x64-Oracle/mmx.exe
```

`mmx.ini` ships with `[General] EnableSnes9xOracle = false` — same
rationale as the Dr Light section: save-state-load repro, from-boot
oracle cannot follow.

### Exact reproduction (TCP-driven, no human input)

Pre-requisite save states (authored by human, not by these steps):
- **slot 1**: X in Chill Penguin's stage, paused into the WEAPONS
  ENERGY / X-BUSTER inventory screen, charged X-Buster shot held,
  Chill Penguin alive within reach. Loading the state lands directly
  in the inventory menu.

Steps 1–3 are identical to the Dr Light recipe — get through boot
and verify Highway intro stage. Then:

4. **Load save state slot 1** (TCP):

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "loadstate 1"
   Start-Sleep -Seconds 2
   ```

   Expected screenshot: WEAPONS ENERGY / X-BUSTER inventory pause
   menu (X centered, sub-tank icons visible). The slot-1 state was
   captured WHILE PAUSED in the inventory.

5. **Press START once to exit the pause menu** (X re-enters the
   playable stage with the charged shot still held):

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "set_controller p1=start"
   Start-Sleep -Milliseconds 100
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "clear_controller"
   Start-Sleep -Milliseconds 800
   ```

6. **Load slot 1 again** (now you've loaded the inventory-paused state
   ONTO a live in-stage frame — the game's internal "paused" flag is
   inconsistent with the menu state):

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "loadstate 1"
   Start-Sleep -Seconds 2
   ```

   Screenshot: inventory menu again, looks identical to step 4.

7. **Press START once.** This exits the pause menu and releases X's
   charged shot, which connects with Chill Penguin and kills him in
   one hit:

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "set_controller p1=start"
   Start-Sleep -Milliseconds 100
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "clear_controller"
   Start-Sleep -Seconds 3
   ```

   Screenshot at this point: Chill Penguin in his death explosion,
   X on the right of the screen.

8. **Confirm softlock.** Frame counter must keep advancing across
   two polls 2 s apart (NOT a hard freeze); call_stack must park at
   `YieldOneFrame_M0X0`:

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "get_frame"
   Start-Sleep -Seconds 2
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "get_frame"  # frame advances
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "call_stack"
   # → {"depth":1,"stack":["YieldOneFrame_M0X0"]}
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "get_cpu_state"
   # → mf:false, xf:false, D:0x0032, S:0x013f, A:0x8d02, Y:0xaa77, func:YieldOneFrame_M0X0
   ```

### Known so far

- `bank_00_94E7_M0X0` runs every frame as the task body's case-0
  dispatch target (`bank_00_931F` reads `[D+$D1]` and dispatches; idx=0
  → 94E7, idx=1 → 953F). State byte stays 0.
- The M0X0 variant is **decoded but structurally wrong** for asm
  bytes at `$00:94F9`. Both the M0X0 and M1X1 variants exist in the
  build; the runtime picks by runtime `(cpu->m_flag, cpu->x_flag)`.
- An mmx_fiber_entry fix that forces `m=1, x=1` at fiber creation
  (currently in mmx_rtl.c lines 169–184) does NOT resolve the
  softlock. The slot 0 fiber predates the boss-defeat sequence —
  it's been alive since stage start, running at M=1 X=1 initially,
  and m/x must have been cleared by some SEP/REP/PLP/RTI inside the
  task body between fiber creation and the bad dispatch.
- The `mx_claim_check` ring shows huge mismatch counts on
  `I_NMI_M1X1` / `IrqHandler_M1X1` (suffix says M=1 X=1, runtime
  says M=0 X=0; ~246k hits each) — the recompiler's hardware-IRQ /
  NMI stubs don't reflect that the 65816 hardware forces M=1 X=1
  on interrupt entry in native mode. Body's own SEP #$30 corrects
  it before the interrupt's first sub-dispatch, so this is
  cosmetic for IRQ/NMI but tells us something general: cpu->m_flag
  / x_flag aren't kept in sync with what the asm-level convention
  expects in several places.

### Tooling added in this session

- `cpu_trace.h`: `PX_TRIPWIRE_PMUT_RING` bumped 64 → 65536.
- `cpu_trace.c`: `cpu_trace_px_record` now records into the pmut
  ring **always-on**, regardless of `g_px_tripwire.armed /
  triggered`. The auto-trigger logic below still gates on armed.
- `debug_server.c`: `pxwatch_get` accepts `count=N` arg (default
  256, max = ring size). Newest-first emission, buf bumped to 1 MB.

These let any session walk back through tens of thousands of
P-mutations to find where m/x got cleared en route to the bad
dispatch site.

### Next session pick-up

The next step is: query the pmut ring (`pxwatch_get count=4096`)
at the softlock moment and find the LAST `cpu_p_to_mirrors` /
PLP / REP that cleared `m_flag` to 0 before the call to
`bank_00_94E7_M0X0`. That instruction's `pc24` identifies the
function whose body is incorrectly handling m/x at the boss-defeat
junction.

---

## [RESOLVED 2026-05-26] Dr Light capsule freeze — repro recipe

> **Status: RESOLVED (2026-05-26).** No longer reproduces against the
> current build. The root cause documented below — the PEI-trampoline
> at `$04:9A02` leaking bytes onto the simulated stack, propagating up
> to `D1ED`'s `PLD` and corrupting the DMA queue tail — was fixed
> framework-side in snesrecomp
> ([`recompiler/v2/emit_function.py:385`](https://github.com/mstan/snesrecomp/commit/fe39fb7),
> shipped in MMX v0.1.1). The NLR detector now recognises the
> `PLA* + cross-bank long-tail JMP/JML` stack-discard idiom and emits
> NLR-aware SKIP-N propagation instead of a plain `RecompStackPop+return`,
> so the C-call-stack model and the SNES hardware-stack control flow no
> longer diverge on this shape. "Option 1 / Option 2" in the analysis
> below describe the candidate approaches considered before the NLR-
> detector route was taken; they are kept for context. The full repro
> recipe is retained for regression.

### Symptom

The recompiled game freezes shortly after entering Chill Penguin's
stage and walking right toward the Dr Light capsule. Visible
symptoms:
- Screen fades to black.
- The recomp's per-frame counter stops advancing (every `ping`
  returns the same `frame` value).
- `call_stack` shows the foreground stuck inside the NMI DMA queue
  walker:
  `["bank_00_82C8_M1X1", "bank_00_83F1_M1X1", "bank_00_83D9_M1X1",
    "NmiHandler_M1X1", "I_NMI_M1X1"]`
  — that's the BCS-self spin trap at $00:BA48 inside the DMA-queue
  walker reached via the L_83C5 → L_B9F3 chain. The walker advances
  X by `4 + entry_size` per iteration and exits cleanly when
  `X == queue_tail` ($00:00A5..A6). When the tail is non-aligned,
  X overshoots → C=1 → BCS to self → watchdog trips after 5 s.
- After the watchdog trips, the runtime disables itself
  (`g_watchdog_enabled = 0` + `longjmp` out, see
  `common_cpu_infra.c:196`) and the recomp's CPU keeps running into
  invalid state, scribbling garbage into WRAM. Historical
  `dump_frame_wram <freeze_frame>` is the only reliable view; `read_ram`
  at "now" shows post-freeze garbage and should not be trusted.

### Build

```
Configuration: Oracle | Platform: x64
Output:        build/bin-x64-Oracle/mmx.exe
```

`mmx.ini` ships with `[General] EnableSnes9xOracle = false` — the
snes9x oracle backend is disabled for this game because the repro
relies on save-state load and the from-boot oracle cannot follow
that (every `emu_*` TCP command returns a structured warning).

### Exact reproduction (TCP-driven, no human input)

Assumes `mmx.exe` is built and a save-state slot 0 already exists at
`build/bin-x64-Oracle/saves/<state-0>` that lands X in Chill Penguin's
stage just before the Dr Light capsule. Save state was authored by
the human; the TCP commands below do not create it.

Helper used throughout: `F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1
-cmd "<command>"` (TCP debug client wrapper, port 4379).

1. **Stop any running mmx and launch fresh.**

   ```powershell
   Stop-Process -Name mmx -Force -ErrorAction SilentlyContinue
   Start-Sleep -Milliseconds 500
   Start-Process -FilePath 'F:\Projects\snesrecomp\MegamanXRecomp\build\bin-x64-Oracle\mmx.exe' `
     -WorkingDirectory 'F:\Projects\snesrecomp\MegamanXRecomp\build\bin-x64-Oracle'
   ```

2. **Wait through the boot sequence and reach the Highway intro
   stage.** The boot animation is multi-stage (mock-BIOS "NOM
   Engineer Work System" terminal, "MEGAMAN X SPECIFICATION", X
   silhouette, Capcom logo, storyline crawl, Mega Man X title logo,
   GAME START / PASS WORD / OPTION MODE menu). After ~10 s of
   wall-clock boot, spam Start (Enter) at ~500 ms intervals for ~12 s
   to skip through the storyline crawl, the title screen, and the
   menu before the title attract demo kicks back in. Extra Start
   presses are swallowed harmlessly during the boot animation.

   ```powershell
   Start-Sleep -Seconds 10  # let the boot animation settle into the storyline crawl
   for ($i = 0; $i -lt 25; $i++) {
       & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "set_controller p1=start" | Out-Null
       Start-Sleep -Milliseconds 80
       & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "clear_controller" | Out-Null
       Start-Sleep -Milliseconds 450
   }
   ```

   IMPORTANT: this aggressive cadence is only safe when transitioning
   through the boot / title / menu chain. If a screenshot at the end
   shows the **in-game pause sub-menu** instead of the playable
   Highway stage (extra presses landed in-level and paused), do
   **NOT** spam-press to recover — issue exactly one `p1=start` to
   unpause, wait, and screenshot again. Spam-pressing inside the
   pause menu navigates options unpredictably.

3. **Verify Highway intro stage.** Screenshot and compare:

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "screenshot post_boot.bmp"
   ```

   Convert with `System.Drawing` (BMP → PNG so the Read tool can view
   it). Expected: X standing on a green metal platform with a
   cityscape backdrop (purple sky, buildings), health bar full on the
   left edge, X-icon top-left. This is the Highway intro stage start.

4. **Load save state slot 0** (TCP — equivalent to pressing F1 in
   the runner's keymap, but driven over the debug port):

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "loadstate 0"
   ```

   Server response: `{"ok":true,"loading_slot":0}`. The runtime
   consumes the pending loadstate at the start of the next frame.
   Wait ~1.5 s and screenshot — expected scene is Chill Penguin's
   stage, X on snowy/icy ground, two Dr Light capsules (cyan figures)
   visible to the right against a backdrop of blue rocks with
   icicles. NB: the `load_state <filename>` command (L3 harness
   variant) is broken in the agent's environment — use `loadstate
   <slot>` only. The slot-based form works.

5. **Hold right for ~4 seconds** — X walks right toward the
   capsule:

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "set_controller p1=right"
   Start-Sleep -Seconds 4
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "clear_controller"
   ```

6. **Confirm freeze.** Ping twice with a delay between; if the
   second ping's `frame` matches the first, the recomp is frozen.
   `call_stack` will show the NMI-DMA-walker spin signature above.

   ```powershell
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "ping"
   Start-Sleep -Seconds 2
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "ping"   # frame must equal previous
   & F:\Projects\snesrecomp\MegamanXRecomp\_dbg.ps1 -cmd "call_stack"
   ```

### What's known about the freeze mechanism

- **WRAM corruption site.** $00:00A5 (low byte of DMA queue tail)
  and $00:00A6 (high byte) get corrupted in the same frame as the
  freeze. Visible via `wram_writes_at A5` and `wram_writes_at A6`.
  Two 16-bit enqueues by `bank_00_B670_M0X0` (parent `B660_M0X0`)
  set the tail to $0088 then $0110, then a chain of 8-bit writes by
  `bank_00_D76A_M1X0` (parent `D6A7_M1X0`) byte-clobber $A5 and 8-bit
  writes by `bank_00_D5DF_M1X0` (parent `D56F_M1X1`) byte-clobber
  $A6 with a small counter (0→4→0). Final tail value is mis-aligned,
  walker can't reach it cleanly.

- **The D-relative byte writes are intended SNES code, not a
  recompiler bug.** D76A's emit (`cpu_write8(cpu, ..., cpu->D +
  0x0d, _v10)` at mmx_00_v2.c around line 474586) is faithful to the
  asm STA $0D,D. The asm runs with D=$0098 deliberately — that's
  how the game accesses the Dr Light capsule's per-task state block
  at $7E:0098..00FF via DP-relative addressing. $0D+$98 = $A5
  collides with the DMA queue tail because the two regions share
  address space.

- **The previous "wrong M/X variant" hypothesis (chase-the-flag
  dispatch bug) is ruled out.** Forcing the M1X1 variant of DC36 +
  9E68 at JSR sites $00:9A9D / $00:9AA0 via the new `force_variant_at`
  cfg directive did **not** prevent the freeze. With the pin in
  place, the M1X1 variant of DC36 still entered with `D=0x0098` and
  `m=0` at the freeze frame, identical to what M0X1 produced. So
  whatever sets `cpu->D = 0x0098` and `cpu->m_flag = 0` runs
  **before** the JSR DC36 site, somewhere inside `bank_00_D1ED_M1X1`
  (the task dispatcher called just upstream) or inside one of its
  sub-tasks. The leak is in D1ED's exit state, not in any specific
  variant's body. See `feedback_no_oracle_when_savestate_repro` for
  why the snes9x cross-ref is not available to validate this.

### Root cause located: D1ED PLD-via-leaked-stack (2026-05-24 cont.)

The recompiler-discipline session traced the freeze to a stack-balance
violation. `D1ED_M1X1` opens with `PHP; PHD` (preserves P + D) at
$00:D1ED and closes with `PLD; PLP; RTS` at $00:D29A. ROM-faithful.
Recomp's emit faithfully reproduces both the prologue and the
epilogue (mmx_00_v2.c lines 91091-91103 and 91585-91609). One RTS
exit, no early-return paths.

Boundary ring at the freeze frame (`boundary_get kind=ANY` at f=5093
of a fresh repro):

| Function                     | depth | S in     | S out    | Δ      |
|------------------------------|-------|----------|----------|--------|
| `bank_00_9A6A_M1X1`          | 3     | $013f    | $0139    | **-6** |
| `bank_00_D1ED_M1X1`          | 4     | $013f    | $0136    | **-9** |
| `bank_00_D4EA_M1X1`          | 5     | $013c    | $0136    | -6     |
| `bank_00_D530_M1X1`          | 6     | $013c    | $0136    | -6     |
| `bank_00_FB32_M1X1`          | 7     | $013c    | $0136    | -6     |
| `bank_07_CA7B_M1X1`          | 8     | $013c    | $0136    | -6     |
| `bank_07_CB16_M1X1`          | 9     | $013c    | $0136    | -6     |
| `bank_04_9A02_M0X1` (1st)    | 10    | $013c    | $013c    | 0      |
| `bank_04_9A02_M0X1` (**2nd**)| 10    | $013c    | $0136    | **-6** |

The 6-byte stack leak originates inside the **second** call to
`bank_04_9A02_M0X1`. The function calls itself the same way both
times — same A, same X — but the sub-call to `bank_04_9C0E_M0X0`
returns C=0 the first time and C=1 the second time. With C=1, the
`BCC $9A22` fall-through executes:

```
$04:9A02  PHP                  ; push 1 byte
$04:9A03  REP #$10
$04:9A05  LDX #$0BA8
$04:9A08  JSL $84:9C0E
$04:9A0C  BCC $9A22            ; if C=0, skip to PLP+RTL (path A: balanced)
$04:9A0E  LDA #$0C80
$04:9A11  PEI ($0B)            ; push 2 bytes
$04:9A13  LDA #$0C40
$04:9A16  PEI ($0B)            ; push 2 bytes
$04:9A18  LDY $0000            ; abs read of $DB:0000
$04:9A1B  BPL $9A22            ; if N=0, skip last PEI (path B: 4-byte leak)
$04:9A1D  LDA #$1C40
$04:9A20  PEI ($0B)            ; push 2 bytes (path C: 6-byte leak)
$04:9A22  PLP                  ; pop 1 byte
$04:9A23  RTL
```

With both `BCC` and `BPL` falling through (path C), three PEIs push
6 bytes that PLP+RTL cannot reconcile. The 6-byte deficit propagates
through `CB16 → CA7B → FB32 → D530 → D4EA` (none of which PLA/PLX/PLY
the extras) all the way back to `D1ED`, where the `PLD` at $00:D29A
ends up reading the topmost leaked PEI bytes as if they were the
originally-pushed `D` — the read value is $0098 (the high byte of
whatever PEI ($0B) pushed). Likewise `PLP` restores `m=0` from a
PEI'd byte instead of the original PHP'd P. Then `D1ED` RTS-returns
to `9A6A_M1X1`. The leaked D=$0098 and m=0 propagate untouched into
the JSR DC36 at $00:9A9D, downstream the D5DF/D6A7/D76A subtree uses
D-relative addressing that lands directly on $00:00A5/A6 (the DMA
queue tail), corrupting it. NMI's queue walker then BCS-spins.

### Why this is a recompiler-level bug, not a game bug

Real hardware would also be hit by 6 bytes on the stack — except
that on real hardware, `bank_04_9A02_M0X1`'s `RTL` at $04:9A23
**doesn't return to its JSL caller**. With the stack leaky, RTL
pops the PEI'd bytes as a new return address frame, computing a
new (PB, PC) from the topmost PEI's bytes. The function is a
**computed-RTL trampoline**: it pushes parameters to the stack
*and* uses the topmost pushed bytes as the long-jump target. The
caller's original return address (pushed by JSL into 9A02) stays
buried under the params and is consumed by a *later* RTL after the
trampolined-to routine eventually returns. Real-hardware control
flow goes `JSL 9A02 → 9A02 trampolines to V → V's body runs (using
the PEI params as stack-frame locals) → V eventually RTLs back to
the JSL's caller`.

Recomp models JSR/JSL **via the host C call stack**, not via
`cpu->S` pushes. So when 9A02's body returns from the generated C
function, control transfers straight back to the caller's emit code
— skipping V's body entirely. The PEI-pushed bytes remain in
`g_ram` at `cpu->S` and propagate through every subsequent stack op.
This is a systemic divergence between the recomp's C-call-stack model
and the SNES's hardware-stack-as-control-flow idiom.

The PEI-trampoline pattern is widely used in MMX (and likely other
Capcom games of the era); fixing it requires recompiler-level work.
Two candidate fixes, in order of completeness:

1. **Model JSR/JSL pushes in `cpu->S` and emit RTS/RTL as
   indirect-jumps-via-popped-PC.** Most-complete option per the
   completeness rule. Lets `cpu->S` accurately reflect what real
   hardware would have at every point; lets RTL transfer control to
   the popped (PB:PC), not the C return. The host C call stack
   becomes auxiliary (not control). This needs every JSR/JSL emit
   to push (PB+)PC2 on cpu->S, every RTS/RTL emit to pop and jump,
   and a dispatch shim that resolves (PB,PC) → C function pointer.
   Significant refactor of the codegen + runtime model.

2. **Detect the PEI-trampoline idiom statically in the decoder** and
   emit it as a tail-call to a runtime-computed target. Less
   invasive but narrower: only catches the specific
   `PEI...PEI..PLP RTL` shape. Misses other stack-frame manipulation
   idioms.

Both fixes need cross-game regression against SMW + LttP.

### What was tested and ruled out

- M/X dispatch variant (`force_variant_at` diagnostic, 2026-05-24).
  Forcing `M1X1` at `JSR DC36` ($00:9A9D) and `JSR 9E68` ($00:9AA0)
  did **not** prevent the freeze. The pinned `M1X1` variant of DC36
  entered with D=$0098 and m=0 anyway — confirming the leak is
  upstream of these call sites, in `D1ED`'s sub-task chain, not in
  the variant selection. The framework `force_variant_at` cfg
  directive remains in the recompiler as a diagnostic primitive
  (cfg_loader.py + codegen.py + v2_regen.py + lowering.py); the
  MMX-specific cfg hints have been removed.

- D-relative emit at D76A/D5DF. Both faithfully reproduce the ROM
  asm. The byte writes to $A5/A6 are intended hardware behavior;
  the corruption only manifests because D=$0098 leaked from D1ED's
  exit. Fix the upstream leak, the corruption disappears.

- D1ED's own prologue/epilogue. PHP+PHD at entry, PLD+PLP at exit,
  one RTS — emit is structurally correct. The PLD reads the wrong
  bytes because cpu->S is displaced, not because the PLD op itself
  is wrong.

### How the diagnosis was made (tooling reference)

1. **Repro frozen and captured.** `boundary_get count=10 kind=ANY
   func=D1ED` showed D1ED EXIT at the freeze frame had **S out
   $0136 vs S in $013f → delta -9**, with D out $0098 (vs $0000
   in) and m out 0 (vs 1 in). Clean adjacent frames had Δ=0.

2. **Sub-task balance audit.** `boundary_get count=400 kind=ANY
   skip=1230` covered the full D1ED subtree at the freeze frame.
   The dispatcher's entry/exit events were paired via the
   `entry_seq` field and S-deltas computed per call (script
   _tmp_subtask_balance.py during the session, now deleted).
   The propagation chain bubbled up from `bank_04_9A02_M0X1`'s
   second call (Δ=-6) through every parent unchanged, plus 3
   bytes from D1ED's own PHP+PHD never reaching PLD+PLP — hence
   D1ED's total Δ=-9.

3. **9C0E flag divergence.** P at 9C0E exit was 0x60 (C=0) on
   the first call and 0x21 (C=1) on the second, with different
   input A values ($d32e and $d332). 9C0E itself is ROM-faithful;
   the C-flag divergence is input-data dependent, not a bug.

4. **ROM asm walked from $04:9A02.** Confirmed three exit paths
   (path A balanced, path B leak 4, path C leak 6) tied to the
   BCC at $9A0C and BPL at $9A1B. The recomp emit at
   `mmx_04_v2.c:91288-91410` faithfully reproduces every PEI and
   PLP — no codegen bug at this layer.

### Pre-requisites for productive next-session work

- snes9x oracle is **off** by `mmx.ini` and the dispatcher refuses
  all `emu_*` commands with a structured warning. See
  `feedback_no_oracle_when_savestate_repro` in memory for the
  rationale. Do **not** re-enable. The Dr Light repro is save-state-
  load-based; the from-boot oracle cannot follow.
- The `force_variant_at` diagnostic hints have been removed from
  `recomp/bank00.cfg` (they had no effect on the freeze — see the
  ruled-out section above). The framework directive remains
  available in `cfg_loader.py` + `codegen.py` + `lowering.py` +
  `v2_regen.py` as a permanent diagnostic primitive.
- Full regen is required after any framework-level change touching
  JSR/JSL or RTS/RTL semantics. `--banks` filtered regen is **not**
  sufficient — cross-bank demand discovery is incomplete in the
  filtered path (link errors like `unresolved bank_00_B071_M1X1`
  surface when other banks call into bank-0 functions that the
  filtered-regen didn't re-emit). 15-20 min full regen + Oracle
  build, then both SMW and LttP regen + build + smoke test to
  guard against cross-game regression.

### Files the next session will need

For the framework-level fix (option 1 above):

- `snesrecomp/recompiler/v2/codegen.py` — `_emit_call` (around line
  1342) emits the JSR/JSL dispatch as a C call without touching
  `cpu->S`. The push semantics belong here.
- `snesrecomp/recompiler/v2/codegen.py` — `_emit_return` (around
  line 1461) emits RTS/RTL as a plain `return _ps;`. The
  pop-and-jump-to-popped-PC belongs here.
- `snesrecomp/recompiler/v2/ir.py` — `Call` and `Return` IR ops
  may need an explicit `pushes_return_addr` / `pops_and_jumps`
  bit so the codegen doesn't need to special-case opcode name.
- `snesrecomp/runner/src/cpu_state.c` — would gain a
  `cpu_dispatch_pc(cpu, pc24)` runtime helper that maps a
  popped (PB:PC) to the right `bank_BB_PPPP_MmXn` C function and
  calls it. The mapping table is built from the
  `_NAME_RESOLVER` already-populated in codegen.

For option 2 (PEI-trampoline detector), additional files:

- `snesrecomp/recompiler/v2/decoder.py` — extend
  `analyze_function_exit_mx` / `_labeled_successors` to also
  compute the net stack delta per path and flag the unbalanced
  RTS/RTL endings as `is_trampoline` on the function entry.
- `snesrecomp/recompiler/v2/codegen.py` — when emitting a function
  marked `is_trampoline`, replace its RTS/RTL with a dispatch
  on the topmost stack bytes computed via `cpu->S`.

---

## Fix proposal — shared-tail multi-level non-local return (2026-05-27)

> **STATUS: PROPOSAL / THEORY — not implemented.** Evidence is in
> "Root-cause investigation — 2026-05-27" above. snes9x oracle is
> disabled for save-state repros, so the "hardware skips the park"
> premise is inferred from the popped PC + the user's report, not
> oracle-confirmed. Validate with the RTS-decision trace before/after.

### The defect (recap)

`D6A7`'s OAM-overflow exit does a manual stack rebalance
(`$D765: PLX PLX … $D5CC: PLB`) that pops `cpu->S` back to the OAM
routine's entry level, then `RTS` ($D5DE). On hardware that single RTS
returns to `$9AA6` — the continuation of `JSR $D56F` at `$9AA3` — i.e.
it returns *out of the whole OAM routine*, several call levels up, and
the park (`$D5C1`) is never reached.

In the Option-1 model each `JSR` is a host-C call paired with one
`cpu->S` frame, assuming the two stacks stay in lockstep. The "pop N
frames then RTS out" idiom breaks lockstep: it unwinds `cpu->S` without
returning the matching host-C functions. The overflow `RTS` then runs in
`D6A7`'s host frame with `_ret_s = 0x138` (the rebalanced level) but
`_entry_s = 0x132` (D6A7's), so `s_eq = 0`; it dispatches on `$9AA6`,
which is a host-return continuation (`found = 0`), and the miss-unwind
returns `RECOMP_RETURN_NORMAL` **one** host frame. The popped return
belongs to an ancestor several frames up, so `D625`/`D56F` wrongly
resume and run the park.

### Recommended fix — ancestor-frame unwind keyed on `_ret_s`

Observation: after the manual rebalance, the missing RTS's `_ret_s`
(`cpu->S` before its pop) equals the **entry_s of the ancestor host
frame whose return this RTS actually is** — here `0x138` == `D56F`'s
`_entry_s`. The intermediate frames have strictly deeper (smaller)
`_entry_s` (`D6A7=0x132`, `D625/D5DF=0x134`). So the unwind target is
identified by `_ret_s`, using only `_entry_s` which every frame already
tracks — no PC table needed.

Mechanism:

1. New return code `RECOMP_RETURN_DISPATCH_UNWIND` (distinct from the
   existing NLR-skip codes).
2. In `_emit_return`'s RTS/RTL dispatch path (codegen.py): on a lookup
   **miss**, if `_ret_s != _entry_s` (the stack is shallower than this
   frame's entry — a manual rebalance unwound past it), set a global
   `g_unwind_target_s = _ret_s` and return `DISPATCH_UNWIND` instead of
   restoring S + returning NORMAL. (If `_ret_s == _entry_s`, keep the
   current leaf-unwind NORMAL behavior.)
3. At every host-call site (the `_r = callee(cpu); if (_r != NORMAL)…`
   pattern emitted by `_emit_call`): when `_r == DISPATCH_UNWIND`,
   compare `g_unwind_target_s` to the **enclosing** function's
   `_entry_s`:
   - **equal** → this frame is the one whose return the RTS represents:
     return `RECOMP_RETURN_NORMAL` (host-return; the caller resumes at
     its natural continuation — for `9A6A` that is `$9AA6`).
   - **not equal** → return `DISPATCH_UNWIND` (propagate up without
     running this frame's continuation).
4. Guard: if the unwind reaches the outermost frame without a match,
   fall back to the current restore-S + NORMAL behavior (a genuine leaf
   miss, not a multi-level return).

Why this is correct and why it does NOT repeat the reverted cfg mistake:
the continuation `$9AA6` runs in **`9A6A`'s** host context (via D56F's
normal host-return), not dispatched into with `host_return_valid = 0`.
`cpu->S` is already at the rebalanced level (the PLX/PLB popped it), so
intermediate frames returning early without their own pops is correct —
same rationale as the existing NLR-skip path.

### Alternative considered

- **PC-keyed unwind**: carry the popped PC and resume at the host-call
  site whose continuation == that PC. Requires a continuation-PC table
  and per-call-site comparisons; strictly more machinery than the
  `_ret_s == _entry_s` key, with no extra correctness. Not recommended.
- **Declare `$9AA6`/`$D6A6` as cfg `func` entries**: REJECTED — already
  tried; dispatches into the continuation in the wrong (`hrv=0`) context
  and regressed to a black screen (see "Ruled out" above).
- **Cap `$E4` / suppress the park**: REJECTED — `$E4` legitimately
  reaches `0x80`; capping or suppressing is a symptom patch.

### Risks / open questions

- Multiple ancestor frames sharing the same `_entry_s` (recursion) would
  stop at the innermost match. No such case in the OAM path; note as a
  caveat for the general fix.
- Frames between the originator and the target that pushed extra
  `cpu->S` state must have had it popped by the manual rebalance already
  (true for this idiom). Verify no path returns `DISPATCH_UNWIND` while
  still owing a `cpu->S` pop.
- Interaction with the existing `_pending_skip` NLR codes — pick a
  non-colliding `RECOMP_RETURN_*` value and audit the propagation joins.

### Validation plan (with the new traces)

After implementing, re-run the fish repro and confirm via
`get_rtstrace`/`get_oamblk`:
- the overflow `$D5DE` RTS in `D6A7` now classifies as the new
  `DISPATCH_UNWIND` and propagates to `D56F` which host-returns;
- `D56F`'s park (`$D5C1`) executes **once** (tail clear), not 128×;
- `[oamdrop]` does not fire; HUD + sprites stay through the explosion;
- SMW + LttP cross-game smoke unaffected (the change only alters the
  dispatch-MISS path when `_ret_s != _entry_s`).
