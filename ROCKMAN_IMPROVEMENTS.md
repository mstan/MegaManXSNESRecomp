# Rockman X Improvements

## Purpose

Bring the Japanese Rockman X v1.1 target to the same LLE-first maturity level as
the validated Mega Man X target. LLE is the correctness baseline; HLE may only
be added as an optional convenience or performance layer with a working LLE
path underneath it.

This work belongs on `feat/rockman-improvements` in the dedicated worktree at
`F:\Projects\snesrecomp\_wt_rockman_improvements`. Recompiler redesign work
belongs in the separate `snesrecomp` worktree/branch so compiler experiments do
not obscure game-specific regressions.

## Current State

- The Rockman X v1.1 ROM is available at
  `variants/jp/roms/rockmanx.sfc`.
- JP configuration exists in `variants/jp/config`.
- CMake defines `RockmanXSNESRecomp`, but skips it while
  `variants/jp/gen` is empty.
- The existing `tools/regen.sh` is hard-coded to the USA ROM, configuration,
  generated-output directory, and symbol prefix.
- Previous JP interpreter coverage reached roughly 4,000 frames of boot/title/
  attract execution. The first enrichment round recorded 239 distinct sites
  without bails; the second round found no additional sites.
- That evidence is useful but is not sufficient gameplay validation.

## Immediate Work

1. Add an explicit variant-aware regeneration interface.
   - Preserve the existing USA defaults.
   - Support JP ROM, configuration, output directory, and symbol prefix without
     editing the script between builds.
   - Keep generated output deterministic and avoid touching saves or unrelated
     build artifacts.
2. Regenerate `variants/jp/gen` with the current stable LLE-default engine.
3. Build and launch `RockmanXSNESRecomp` from a clean process.
4. Audit startup logs for unresolved traps, interpreter-cap bails, stack
   imbalance, M/X claim mismatches, and scheduler divergence.
5. Commit each independently validated change on this worktree branch.

## Validation Gates

The target is not considered mature based only on reaching the attract demo.
Validate each of the following without using stale save states:

- Clean boot, publisher/logo sequence, title screen, and complete attract loop.
- Stable graphics, palettes, scrolling, audio, and controller response.
- New-game intro and highway gameplay.
- Enemy spawning, projectile collision, damage, pickups, and scripted events.
- Pause/unpause, death/restart, and password entry/restore.
- Stage transition and at least one miniboss or boss path.
- Re-entry or repeated transitions where state restoration is involved.

For suspicious paths, compare normal LLE/AOT execution with interpreter-only
execution and use an oracle/cosim trace where practical. A fix must address the
general compiler/runtime behavior when the failure is not JP-specific.

## Architecture Rules

- Do not replace a ROM function with handwritten game logic merely to make a
  test pass.
- Prefer correct decoding, boundary discovery, M/X propagation, indirect
  dispatch, and interpreter continuation behavior in `snesrecomp`.
- If a temporary game-side annotation is required for diagnosis, document why
  automatic discovery failed and retain a faithful LLE fallback.
- Keep HLE opt-in and removable. Disabling it must leave a correct, if slower,
  LLE implementation.
- Do not edit generated C manually.
- Do not mix broad recompiler redesign commits into this game worktree.

## Longer-Term Improvements

- Make regeneration resumable, deterministic, and variant-aware.
- Cache analysis independently from generated source emission.
- Report non-convergence as a small dependency/SCC diagnostic rather than
  repeatedly rebuilding whole banks.
- Automatically discover balanced indirect continuations and live JMP/JML
  targets that currently need configuration hints.
- Produce machine-readable coverage showing AOT, interpreted, unresolved, and
  HLE-overridden execution for each target.

## Completion Criteria

Rockman X v1.1 builds reproducibly from its checked-in configuration, runs LLE
by default, passes the validation gates above without visual/audio corruption or
softlocks, and has no unexplained unresolved-dispatch or stack-balance failures
in the tested paths. Final acceptance requires interactive user validation.
