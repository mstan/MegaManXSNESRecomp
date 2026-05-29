# MegaManXSNESRecomp

Static recompilation of *Mega Man X* (SNES) into native C, using the
[snesrecomp](https://github.com/mstan/snesrecomp) framework. This repo
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

## Current status: 1.0.0 — fully playable

Hand-verified playable end-to-end. The Option-1 `cpu->S` return-frame
model (snesrecomp `926d61e`) resolved the heavy-load softlocks and
fiber-scheduler lockups that capped the 0.1.x line — the game now
recovers from heavy sprite/OAM load instead of stalling.

- Boot → Capcom logo → attract intro (X on the Highway stage, Zero on
  the motorcycle) → title screen → gameplay.

**Known issues at this release (minor, non-blocking):**

- Isolated rough edges in sprite-mode timing / HDMA windowing / BG
  layer priority in a few spots.

This is `1.0.0` — the first fully-playable release. It remains a
recompilation project under active development; recompiler output and
internal APIs may change between versions.

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

System shortcuts (configured in `mmx.ini`'s `[KeyMap]` section):

| Action               | Default |
|----------------------|---------|
| Save state 1-10      | Shift+F1..F10 |
| Load state 1-10      | F1..F10 |
| Toggle pause         | P |
| Reset                | Ctrl+R |
| Toggle fullscreen    | Alt+Enter |
| Turbo (fast-forward) | Tab |

## Building from source

Prerequisites: Windows 10+, Visual Studio 2022 (with C++ desktop
workload), Python 3.9+ on PATH.

```bash
git clone https://github.com/mstan/MegaManXSNESRecomp
git clone https://github.com/mstan/snesrecomp
cd MegaManXSNESRecomp
```

The `snesrecomp/` directory is a [sibling repo](https://github.com/mstan/snesrecomp)
accessed via a junction/symlink to the clone next to this repo.

Build:

```bash
# From a Developer Command Prompt for VS 2022, or with MSBuild on PATH:
msbuild mmx.sln /p:Configuration=Oracle /p:Platform=x64 /m
```

### macOS / Linux (CMake)

Builds natively on macOS (Apple Silicon + Intel) and Linux with clang/gcc.
Prerequisites: CMake 3.16+, SDL2, Ninja, Python 3.9+
(`brew install cmake sdl2 ninja python3`).

```bash
ln -s ../snesrecomp snesrecomp     # sibling framework checkout (see above)
bash tools/regen.sh --no-tests     # generate src/gen/ (needs a verified mmx.sfc at repo root)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build
./build/MegaManXSNESRecomp         # first run prompts for / caches the ROM path in rom.cfg
```

`Alt+Enter` toggles fullscreen and SDL game controllers work out of the box
(see Controls above). The emulator oracle is a developer-only feature and is
off in this build.

The recompiled C in `src/gen/` is **not** committed — contributors must
regenerate it from a local ROM before the first build. See the next
section.

### Regenerating the recompiled C (contributors)

1. Drop a legally-obtained `mmx.sfc` at the repo root (`.gitignore`
   excludes it).
2. Run `bash tools/regen.sh --no-tests` (drives the recompiler over every
   `recomp/bank*.cfg` and writes `src/gen/bankXX_v2.c` + `dispatch_v2.c`).
   On Windows without bash, invoke the underlying tool directly:
   ```bash
   python snesrecomp/tools/v2_regen.py --rom mmx.sfc --cfg-dir recomp --out-dir src/gen
   ```
3. Rebuild as above.

## Repo layout

| Path | Purpose |
|------|---------|
| `src/` | Runtime C (CPU state glue, NMI orchestration, hand-written bodies for things the framework doesn't recompile). |
| `src/gen/` | Recompiler output (gitignored; regenerated from ROM). |
| `recomp/bank*.cfg` | Per-bank function declarations + hardware hints the framework cannot derive from the ROM alone. |
| `recomp/funcs.h` | Auto-regenerated by `v2_regen.py`; never hand-edit. |
| `snesrecomp/` | Symlink to a sibling clone of the [snesrecomp framework](https://github.com/mstan/snesrecomp). |
| `third_party/` | Vendored deps (gl_core, stb_image) with their own licenses. |
| `mmx.sln` + `src/mmx.vcxproj` | Visual Studio build glue. |
| `mmx.ini` | Sample config; the runtime auto-copies a fresh one next to the exe on first run. |

## License

Not yet declared. Code in this repo is original; vendored dependencies
under `third_party/` retain their own licenses.

The *Mega Man X* ROM and any data extracted from it are **not** in
this repo and are not licensed for redistribution.
