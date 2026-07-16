# Universal true-widescreen renderer

## Summary

Add an engine-level, display-derived widescreen mode for both USA and JP builds on Windows, Linux, and macOS. It will widen the PPU render surface and render real additional background/sprite columns; it will not alter emulated camera, collision, AI, scrolling, or other gameplay state.

## Implementation changes

- Add a persisted `[Graphics] Widescreen` boolean, defaulting off; expose it in the shared launcher, macOS F1 display menu, and a rebindable `ToggleWidescreen` system command (default `Alt+W`).
  - Runtime changes update the active config file immediately, including `--config` overrides.
  - Enable launcher support for MMX instead of hiding its existing widescreen panel.

- Centralize frame geometry in a small MMX display/widescreen module:
  - Query the current SDL drawable/window aspect before each PPU frame.
  - When enabled, calculate `frameWidth = clamp_even(round(224 * displayAspect), 256, 448)` and `extraPerSide = (frameWidth - 256) / 2`.
  - When disabled, always use the authentic 256×224 frame.
  - Allocate the host framebuffer for the PPU maximum (448×240), but use the current width and pitch for each frame so toggling, resize, fullscreen, and display changes take effect without restart.

- Wire the existing runner PPU widescreen path into MMX’s real render loop:
  - Define the runner’s widescreen state symbols and set `PpuSetExtraSpace` immediately before every PPU frame, including turbo frames that skip presentation.
  - Rebind `PpuBeginDrawing` with the active width/pitch each frame, then use `RtlWidescreenPresent` to copy the expanded rendered image.
  - Keep all changes strictly in host presentation/PPU rendering; do not inject generated-game-code overrides or modify any emulated RAM/register/camera state.
  - Preserve savestate compatibility: widescreen mode is host configuration, not serialized game state.

- Make every presenter consume dynamic frame dimensions correctly:
  - SDL renderer recreates its streaming texture and logical presentation size when the frame width changes.
  - OpenGL continues its dynamic texture allocation path and receives the active frame dimensions for correct aspect-preserving viewport/shader input.
  - Metal replaces its fixed 320×240 presentation assumptions with current source dimensions, including its integer-fit geometry and NTSC-CRT intermediate/output sizing, so widescreen is neither squashed nor cropped.
  - Keep black bars only when the safe 448-pixel PPU limit prevents filling an ultrawide display.

- Document the option in generated `config.ini` and README: it shows genuine offscreen scene data, may reveal objects outside the original camera view, is capped by safe SNES OAM/PPU limits, and deliberately does not change gameplay logic.

## Test plan

- Build the USA and JP CMake targets; build the Windows MSVC target to verify the shared runtime source list and launcher path.
- Verify disabled mode produces the existing 256×224 output unchanged.
- For SDL accelerated, SDL software, OpenGL, and macOS Metal:
  - toggle repeatedly in gameplay and at menus;
  - resize/window/fullscreen across 4:3, 16:9, and ultrawide displays;
  - confirm the visible scene expands horizontally, with no stretch/crop or texture/pitch corruption.
- On macOS, repeat with NTSC-CRT enabled and disabled.
- Exercise save/load, reset, pause, turbo, transitions, and a representative level in each ROM variant; confirm player movement, collision, enemy behavior, scrolling, and state loading remain unaffected.

## Assumptions

- “Display-derived” means the PPU render width tracks `224 × current display aspect`, rounded to an even width and capped at 448 pixels.
- The mode persists after runtime changes and is off by default.
- Seeing off-camera entities or level content is accepted; any behavior that changes emulated game logic is out of scope and must be treated as a defect.
