# Widescreen Hardening and Polish

## Summary

Harden true widescreen across the shared PPU, MMX renderer, predictive-enemy overlay, presentation backends, build reproducibility, and validation. Preserve authentic 4:3 pixel geometry, seamless opaque enemy previews, host-only rendering state, and macOS/Linux parity.

## Implementation Changes

- Create an isolated `snesrecomp` worktree from the currently pinned revision on `codex/mmx-widescreen-hardening`; leave the existing detached checkout and all unrelated dirty changes untouched.
- Add a shared host-only PPU line-enhancer API with callback context. When registered, suppress stale BG1 margin tiles, invoke the game producer before final composition on main/sub screens, and exclude callback state from savestates.
- Consolidate the two debug screenshot paths behind one scoped 256-wide capture helper that saves and restores render buffer, pitch, margins, and enhancer state. Disable widescreen during capture to eliminate the current buffer-overflow risk.
- Commit and push the focused dependency branch, update the game repository's `snesrecomp` gitlink to its reachable SHA, and restore MMX’s per-frame enhancer registration.
- Enforce the submodule gitlink during CMake configuration, including a clean runner tree. Expose `MMX_ALLOW_UNPINNED_SNESRECOMP=ON` through the explicit `--nopin` escape hatch on macOS and Linux, and keep release builds strict by default.

- Move width and viewport policy into a cohesive MMX display module. Extend the renderer interface with an output-size query implemented by SDL, OpenGL, and Metal so geometry uses drawable pixels.
- Preserve the SNES 7:6 pixel aspect ratio: calculate internal width as `round_even(192 × drawable_width / drawable_height)`, clamped to 256–446. Expected results are 256 at 4:3, 308 at 16:10, approximately 342 at 16:9, and 446 at the ultrawide cap.
- Present frames using the same 7:6 correction so the center 256×224 image remains 4:3. Window scaling uses a 320×240 authentic baseline and expands width proportionally in widescreen.
- Retain the last valid geometry while minimized or during zero-sized drawable transitions; only resize host textures when the computed even width changes.
- Preserve the selected legacy/new PPU setting while forcing the priority-buffer renderer only during active widened frames.
- Fix OpenGL vertical centering, honor aspect/stretch policy consistently across backends, and let Metal fractionally downscale when a 1× integer image does not fit.

- Keep predictive enemies as seamless opaque host overlays, restricted to the added right margin. Clamp every preview pixel so it can never enter the authentic center viewport.
- Replace unchecked ROM reads with a bounded ROM-view API sourced from the loaded cartridge size. Validate LoROM translations, tables, decompression streams, palettes, arrangements, and enemy-event records before caching or drawing.
- Register the BG1 enhancer only when the current playable-stage data validates; otherwise retain the generic PPU path rather than suppressing margins.
- Suppress previews during forced blank, invalid stages, and zero brightness; apply the current PPU brightness/fade to cached source colors while leaving guest RAM, OAM, CGRAM, object slots, AI, collision, and spawn state untouched.
- Update macOS and Linux UI text to report enabled versus currently active status and the active internal dimensions. Patch only the widescreen documentation section, preserving the existing unrelated README and `.github` work.

## Interfaces

- Extend `RendererFuncs` with a read-only drawable/output-size callback.
- Add `PpuSetWidescreenLineEnhancer(ppu, callback, context)`; callback and context are host-only and reset to null with PPU initialization/reset.
- Add pure MMX geometry functions for internal width, presentation aspect, and fitted viewport calculations so they can be tested without SDL or a running game.
- Treat the `snesrecomp` gitlink as the single dependency pin. Add `MMX_ALLOW_UNPINNED_SNESRECOMP`, defaulting off; `--nopin` is the only supported opt-out in build scripts.

## Test and Validation Plan

- Add CTest coverage for 4:3, 16:10, 16:9, ultrawide, cap, portrait, zero-size, and nearest-even geometry; verify authentic presentation remains 4:3.
- Add shared PPU tests proving enhancer-off output is byte-identical, callback writes stay inside margin buffers, callback state is not serialized, and 256-wide debug captures retain red-zone guards while widescreen is active.
- Add preview tests using valid and truncated synthetic ROM views: malformed pointers must fail closed, previews must remain outside the center viewport, forced blank/zero brightness must produce none, and rendering must not change guest-state hashes.
- Run AddressSanitizer and UndefinedBehaviorSanitizer over geometry, preview parsing, maximum-width PPU rendering, runtime toggling, and debug screenshots.
- With the verified local ROM/save state, compare widescreen-off output against the authentic baseline and prove the center 256 columns remain pixel-identical while reconstructed margins contain stage geometry.
- Build USA and Japanese targets on macOS and Linux/Steam Deck paths; smoke-test SDL, OpenGL, and Metal resizing, fullscreen transitions, `Alt+W`, F1 UI state, CRT mode, and repeated toggling.
- Rebuild the canonical root `MegaManX.app` and Steam Deck package only after source validation passes, then report automated coverage separately from any remaining human gameplay checks.

## Assumptions

- Predictive enemies intentionally remain authored-pose previews and visually opaque, but never affect emulated state.
- Authentic 4:3 geometry takes precedence over the previous square-pixel width formula.
- Only the focused `snesrecomp` dependency branch is committed and pushed; the game repository is not committed or pushed unless separately requested.
- Existing unrelated dirty changes in both repositories are preserved and excluded from dependency commits and packaging scope.
