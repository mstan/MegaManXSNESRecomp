# Issues — MegamanXRecomp

> **OPEN:** one rendering issue — sprite-bandwidth garbling (below).
>
> **RESOLVED (2026-05-26), retained for regression:** two freezes
> (Chill Penguin kill, Dr Light capsule). Neither reproduces against the
> current build (MMX v0.1.1, snesrecomp `main`). Their reproduction
> recipes and diagnoses are kept deliberately — NOT to be deleted —
> re-run them after any framework change touching JSR/JSL · RTS/RTL ·
> PLA/PLP/PLD · PEI · m/x flag propagation to confirm they stay fixed.

---

## Open

### Sprite / OBJ-layer dropout + tile artifacting under heavy sprite load (filed 2026-05-26)

**Status: OPEN.** Reproduced. Now understood as a **scheduler softlock**,
NOT a pure render bug: the foreground wedges at `YieldOneFrame_M0X0`, the
per-frame OAM build stops, and NMI keeps DMA-copying the frozen OAM buffer —
so sprites + HUD stay gone while the frame counter still advances. The
Y=0xE0 OAM contents are a downstream symptom. **Root-cause CLASS identified:**
stack-balance leaks in the JSR/JSL ↔ RTS/RTL + PEI-trampoline + indirect-
dispatch family (same systemic divergence as the resolved Dr Light freeze);
the complete fix is the snesrecomp `cpu->S` JSR/JSL model rewrite
(`IMPROVEMENTS.md` Option 1). See "Deeper finding" + "Root-cause CLASS" below
— supersedes the initial render/OAM-overflow framing.

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
frames on `cpu->S`, RTS/RTL as pop-and-dispatch (in progress on branch
`feat/cpu-s-stack-model`). Point patches per site are stubs and were
disproven by Dr Light's `force_variant_at` experiment.

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
