# Mega Man X Recomp — Enhancements

Player-facing features layered on top of the base recompilation. Each is
intended to be implemented game-side (in `src/`, no hand-edits to generated
code), mirroring the `MetroidNESRecomp` synthetic-SRAM pattern.

---

## Synthesized SRAM: password persistence (planned — not yet built)

**Status: idea only. Not implemented; not currently being built.**

Mega Man X has **no battery SRAM** — progress is preserved via a password.
Because of that, the launcher's generic **SAVES panel is intentionally hidden**
for this game (`gi.sram_path = NULL` in `src/main.c`); wiring it to a
`saves/*.srm` would advertise a battery save the cartridge doesn't have.

The eventual enhancement is to give MMX the same save UX as a battery game by
*synthesizing* SRAM on top of the password system, rather than faking a battery:

- **Save-anywhere capture** — periodically run the game's *own* password encoder
  out-of-band to produce a valid password for the current progress, and persist
  it to disk (one small file next to the exe).
- **History log** — append each distinct captured password (timestamped) so the
  player can return to any earlier state by re-entering an older password.
- **Launcher display/edit** — surface the last password in the launcher's SAVES
  panel (read-only, with an edit→confirm flow), reusing the shared Dear ImGui
  launcher / `sram_path` plumbing rather than adding game-specific UI.
- **Auto-prefill** — on the password-entry screen, inject the saved password
  automatically so the player never has to type it.

The captured/edited value must be a *real* MMX password (correct checksum) so it
round-trips cleanly through the game's own decode.

### Reference

See `../MetroidNESRecomp/ENHANCEMENTS.md` ("Synthetic SRAM: password save
system") for the worked-out NES version of this pattern — out-of-band encoder
call bracketed by `runtime_begin/end_post_nmi()` with zero-page + WRAM scratch
snapshotted/restored, `.srm` persistence, launcher SAVES panel, and entry-screen
prefill. The MMX implementation would follow the same shape against the SNES
password engine.

**Prerequisite reverse-engineering (not yet done):** locate MMX's password
encoder/decoder routines, the WRAM password buffer, the entry-screen tick +
cursor RAM, and the gameplay gate used to decide when capture is safe.

---

## LLE task scheduler: correctness ground truth

**Status: LLE is the default. The legacy HLE scheduler is an explicit,
deprecated compatibility/performance override (`SNESRECOMP_EXECUTION_MODE=hle`),
not an independent source of game semantics.**

The MMX cooperative task scheduler at `$00:8099` can now run two ways (see
`RunOneFrameOfGame` in `src/mmx_rtl.c`):

- **LLE (default):** runs the *real* `$8099` scheduler under
  interp816 via `interp_bridge_run_scheduler` (engine `interp_bridge.c`), yielding
  at the `$8080A1` vblank-spin. The interpreter handles the infinite loop,
  coroutine stack-switching, and `JMP ($0032,X)` dispatch faithfully.
- **HLE (`SNESRECOMP_EXECUTION_MODE=hle`):** `MmxSchedulerTick` — the inherited
  hand-written C-host scheduler with per-slot host fibers. It remains available
  for comparison, but LLE behavior wins whenever the two paths disagree.

The mixed-tier bridge keeps the small scheduler and coroutine machinery under
the interpreter while eligible task bodies execute as compiled code. Stack
continuations, interrupt-owned poll loops, and non-returning yield primitives
are engine responsibilities; they must not be repaired with per-game decoder
ownership hints or by changing ROM function boundaries to suit HLE.

The migration gate is an artifact-free, non-freezing, clean-audio attract-demo
pass. Full gameplay validation (stages, bosses, pause/menu, save states, and
determinism) remains a separate follow-up before a production release.
