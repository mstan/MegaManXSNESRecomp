# MegaManXSNESRecomp

Static recompilation of *Mega Man X* (SNES) into native C, using the
[snesrecomp](https://github.com/navjack/snesrecomp) framework. This repo
is the per-game side: the runtime, the recompiled C output, the
per-game `.cfg`, and the build glue.

## What "static recompilation" means here

The 65816 CPU code from the ROM is statically translated to C — every
function the game runs on the SNES's main CPU is a real generated C
function in `src/gen/`. **The rest of the SNES is not recompiled** —
it's hardware. PPU rendering, the APU/SPC700 audio coprocessor, DMA
and HDMA channels, hardware register I/O, and bank-mapping all run
through an embedded copy of snes9x's emulator core
(`snesrecomp/runner/snes9x-core/`). Same model as N64Recomp and
similar projects: recompile the CPU, emulate the silicon.

The ROM is **never** redistributed — you supply your own legally-dumped
copy.

## Current status: 1.0.5 — fully playable

The game has been tested and is playable end to end. 

**Known issues at this release (minor, non-blocking):**

No known issues at this time on Windows builds. 

macOS and Linux are supported. Linux on Steam Deck has been validated through
full game completion with the true widescreen renderer enabled by
tester [littlerobotfairy](https://www.twitch.tv/videos/2820912518).

### Linux / Steam Deck validation

Tester **littlerobotfairy** completed the game on Linux running on Steam Deck
with true widescreen enabled. The complete playthrough is documented in the
[Twitch VOD](https://www.twitch.tv/videos/2820912518).

If you hit a reproducible lockup or visual regression, please open an
issue with a savestate (`Shift+F1`) and the frame at which it
manifested.

## Quick start (pre-built release)

1. Download the latest release zip from [Releases](../../releases) and
   extract it.
2. Run `mmx.exe`. On first launch a file picker asks for your
   **legally-obtained** *Mega Man X (USA) (Rev 1)* ROM (`.sfc` /
   `.smc`). The expected SHA-256 is
   `b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32`
   (1.5 MiB, LoROM). 512-byte SMC copier headers are auto-stripped
   before hashing, so headered or unheadered both work.
3. Edit `keybinds.ini` (auto-generated next to the exe on first run) to
   remap keys, then restart.

The path you pick is cached to `rom.cfg` next to the exe so subsequent
launches skip the picker.

## Controls (default `keybinds.ini`)

| SNES button | Default key |
|-------------|-------------|
| D-Pad       | Arrow keys |
| A           | X |
| B           | Z |
| X           | S |
| Y           | A |
| L           | C |
| R           | V |
| Start       | Enter |
| Select      | Right Shift |

Player 2 is unbound by default — fill in keys in `keybinds.ini` to
enable a second keyboard player.

**Xbox / PlayStation / Switch Pro controllers** are auto-detected via
SDL_GameController (XInput on Windows). Plug it in before launching, or
hot-plug after.

System shortcuts (all rebindable in `config.ini`'s `[KeyMap]` section;
set a key to an empty value there to unbind it, e.g. `DisplayPerf =`):

| Action               | Default |
|----------------------|---------|
| Save state 1-10      | Shift+F1..F10 |
| Load state 1-10      | F1..F10 (except macOS/Linux F1) |
| Toggle pause         | P |
| Pause (dimmed)       | Shift+P |
| Reset                | Ctrl+R |
| Toggle fullscreen    | Alt+Enter |
| Turbo (fast-forward) | Tab |
| FPS / perf readout   | F |
| Toggle PPU renderer  | R |
| Toggle true widescreen renderer | Alt+W |
| Volume up / down     | Shift+= / Shift+- |

## Reporting crashes

The game continuously records its own boot/run diagnostics. If it
crashes (or exits with an error), it writes these files next to
`mmx.exe` — attaching them to a GitHub issue usually lets the crash be
diagnosed without a repro:

- `crash_report_<timestamp>.json` and `crash_minidump_<timestamp>.dmp`
  — written at the moment of a crash; never overwritten by later runs.
- `last_run_report.json` — written at the end of **every** run
  (crash or clean exit), so grab it right after the bad run if there
  is no `crash_report_*` file.

None of these contain personal data beyond your Windows version,
hardware model, and the folder path the game runs from.

## Building from source

Clone the fork with all framework dependencies, then run the idempotent
bootstrap check:

```bash
git clone --recurse-submodules https://github.com/navjack/MegaManXSNESRecomp-macos-linux-wide.git
cd MegaManXSNESRecomp-macos-linux-wide
bash tools/bootstrap.sh
```

The `snesrecomp/` directory is a pinned submodule from
[navjack/snesrecomp](https://github.com/navjack/snesrecomp). If you cloned
without `--recurse-submodules`, `tools/bootstrap.sh` initializes it and its
nested dependencies. The gitlink in this repository is the dependency pin;
there is no separate SHA to keep synchronized.

Generated game C is not redistributed. Before the first build, stage a legally
obtained USA Rev 1 ROM as `mmx.sfc`, then run:

```bash
cp "/path/to/Mega Man X (USA Rev 1).sfc" mmx.sfc
bash tools/regen.sh usa --no-tests
```

On Windows 10 or newer, install Visual Studio 2022 with the C++ desktop
workload, Git, and Python 3.9 or newer. Run the bootstrap and regeneration steps
from Git Bash, then build from a Developer Command Prompt:

```powershell
msbuild mmx.sln /p:Configuration=Release /p:Platform=x64 /m
```

### macOS / Linux (CMake)

Builds natively on macOS (Apple Silicon + Intel) and Linux with clang/gcc.
On macOS, install dependencies with
`brew install cmake sdl2 ninja python3`. On Ubuntu/Debian, install
`build-essential cmake ninja-build libsdl2-dev libgl1-mesa-dev python3`.

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dev --target MegaManXSNESRecomp
ctest --test-dir build-dev --output-on-failure
```

On macOS, add `-DCMAKE_PREFIX_PATH="$(brew --prefix)"` if CMake does not find
Homebrew's SDL2. Apple Silicon contributors running an x86_64-translated shell
must also configure with `-DCMAKE_OSX_ARCHITECTURES=arm64`. Packaging helpers
detect the native hardware architecture and are documented by
`bash tools/build-macos.sh --help` and `bash tools/build-linux.sh --help`.
See [CONTRIBUTING.md](CONTRIBUTING.md) for dependency development, validation,
and pull-request guidance.

### GitHub Actions builds

`.github/workflows/build-artifacts.yml` runs ROM-free source checks for every
pull request. Pushes to `main` and manual runs also build production artifacts
for Windows x64, macOS arm64 (macOS 26), and Linux x86-64/Steam Deck. Artifact
jobs require a repository Actions secret named `MMX_ROM_BASE64` containing the
base64-encoded, legally obtained USA Rev 1 ROM. The workflow uses it only on the
runner to regenerate `src/gen/`; it is not included in any artifact.

On macOS, the runtime uses Metal for presentation, `GameController.framework`
for Xbox/PlayStation/Switch-compatible controllers, and a Core Audio output
AudioUnit for sound. SDL2 remains only for the window, event loop, and
keyboard path. `Alt+Enter` toggles fullscreen. The emulator oracle is a
developer-only feature and is off in this build.

Press `F1` on macOS to open the Dear ImGui display menu. It can enable the
optional NTSC-CRT mode, scanlines, phosphor blending, signal noise, and hue
adjustment while the game is running. NTSC-CRT is vendored under
`third_party/ntsc-crt/` and uses its SNES encoder/decoder path; the normal
Metal presentation path remains unchanged when the mode is off.

macOS presentation uses integer scales (1×, 2×, 3×, ...). Resizing or
fullscreen leaves centered black borders rather than applying a fractional
scale. The F1 menu also exposes Save/Load buttons for the ten existing
serialized savestate slots; the keyboard bindings remain available as
documented below.

Linux also includes the in-game Dear ImGui menu on both SDL and OpenGL output
backends. Press `F1` to toggle it while playing; it exposes the true-widescreen
switch and Save/Load buttons for all ten slots. `Alt+W` toggles widescreen
directly without opening the menu.

### True widescreen renderer

Set `Widescreen = 1` in `[Graphics]`, enable it from the launcher, or press
`Alt+W` (all rebindable through `[KeyMap]`) to render genuine additional PPU
columns. The internal width preserves the SNES 7:6 horizontal pixel aspect:
256 pixels at 4:3, about 342 at 16:9, and a safe 446-pixel ultrawide cap.
Presentation keeps the authentic center image at 4:3. This changes only host
rendering: Mega Man X's camera, collision, AI, scrolling, and save-state data
remain untouched. Background layers use the PPU tilemap data already prepared
by the game. Foreground geometry in the margins is resolved from MMX's prepared
level data and inserted into the live PPU priority buffer, so
it uses current VRAM graphics, palettes, fades, sprite priority, and color math
without advancing the camera or VRAM streamer. Future enemy records in the
added right margin are decoded from the ROM into a separate host-only preview
list and drawn in their authored resting pose. These previews allocate no
emulated object slots and run no AI or collision; they disappear when they
enter the original 256-pixel window, where the game's normal event spawner
becomes authoritative. At the beginning or end of a stage, empty space beyond
the level boundary remains empty by design.

The S-DSP retains the SNES BRR predictor filters and canonical four-tap
Gaussian interpolation. Host-rate conversion uses continuous interpolation
instead of nearest-sample hold. The current SPC700 core is instruction-cycle
stepped with canonical opcode timing; a sub-cycle bsnes-style SPC700 core is a
separate emulator-core replacement and is not represented as complete here.

The supported packaged workflow is:

```bash
bash tools/build-macos.sh --rom "/path/to/your/rom.sfc" --regen --no-dmg
```

The script builds an arm64 `.app` by default; use `--arch universal` for an
Intel/Apple Silicon package. The ROM is used only for local regeneration and
is never copied into release output.

The recompiled C in `src/gen/` is **not** committed — contributors must
regenerate it from a local ROM before the first build. See the next
section.

### Regenerating the recompiled C (contributors)

1. Stage a legally-obtained USA Rev 1 ROM as `mmx.sfc` at the repo root
   (`.gitignore` excludes it), or pass it to `tools/build-macos.sh --rom`.
2. Run `bash tools/regen.sh usa --no-tests` (drives the recompiler over every
   `recomp/bank*.cfg` and writes `src/gen/bankXX_v2.c` + `dispatch_v2.c`).
   On Windows without bash, invoke the underlying tool directly:
   ```bash
   python snesrecomp/tools/v2_emit.py --rom mmx.sfc --cfg-dir recomp --out-dir src/gen
   ```
3. Rebuild as above.

For Rockman X (Japan v1.1), stage `rockmanx.sfc` under
`variants/jp/roms/` and run `bash tools/regen.sh jp --no-tests`. The JP path
uses its checked-in LLE coverage profile as optional AOT input; variants the
compiler cannot prove remain on the authoritative interpreter fallback.
`bash tools/regen.sh all` regenerates both regions.

## Repo layout

| Path | Purpose |
|------|---------|
| `src/` | Runtime C (CPU state glue, NMI orchestration, hand-written bodies for things the framework doesn't recompile). |
| `src/gen/` | Recompiler output (gitignored; regenerated from ROM). |
| `recomp/bank*.cfg` | Per-bank function declarations + hardware hints the framework cannot derive from the ROM alone. |
| `recomp/funcs.h` | Auto-regenerated by `tools/regen.sh`; never hand-edit. |
| `snesrecomp/` | Pinned submodule containing the [snesrecomp framework](https://github.com/navjack/snesrecomp). |
| `third_party/` | Vendored deps (gl_core, stb_image) with their own licenses. |
| `mmx.sln` + `src/mmx.vcxproj` | Visual Studio build glue. |
| `config.ini` | The config. Generated next to the exe on first run if missing. |

## License

Not yet declared. Code in this repo is original; vendored dependencies
under `third_party/` retain their own licenses.

The *Mega Man X* ROM and any data extracted from it are **not** in
this repo and are not licensed for redistribution.

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
