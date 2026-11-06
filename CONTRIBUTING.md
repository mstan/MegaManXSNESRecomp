# Contributing

Thank you for helping improve Mega Man X SNES Recomp. The project keeps ROM
data and generated game code out of Git, so a working checkout has two explicit
inputs: the pinned `snesrecomp` submodule and your own legally obtained ROM.

## Set up a checkout

```bash
git clone --recurse-submodules https://github.com/mstan/MegaManXSNESRecomp.git
cd MegaManXSNESRecomp
bash tools/bootstrap.sh
```

`tools/bootstrap.sh` is safe to rerun. It synchronizes submodule URLs,
initializes nested submodules, and verifies that `snesrecomp` matches the
gitlink committed by this repository.

Stage a USA Rev 1 ROM as `mmx.sfc` at the repository root and generate the
private C sources:

```bash
bash tools/regen.sh usa --no-tests
```

The ROM and `src/gen/` are ignored and must never be committed. See the README
for the expected ROM hash and regional-variant instructions.

## Build and test

For macOS or Linux development builds:

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dev --target MegaManXSNESRecomp
ctest --test-dir build-dev --output-on-failure
```

macOS contributors may need
`-DCMAKE_PREFIX_PATH="$(brew --prefix)"` during configuration. On Apple
Silicon, add `-DCMAKE_OSX_ARCHITECTURES=arm64` if the shell itself is running
under x86_64 translation. Windows contributors should generate sources from
Git Bash, then run this from a Visual Studio 2022 Developer Command Prompt:

```powershell
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Release
cmake --build build-dev --config Release --target MegaManXSNESRecomp
```

Run `bash tools/regen.sh usa` when changing the recompiler or generated-code
configuration; omitting `--no-tests` also runs the real framework test suite.
Use `--strict-idempotent` when a change can affect emitted C.

For the ROM-backed X3 Zero saber benchmark, build `mmx_state_tests` with
`-DMMX_STATE_TESTS=ON` and run it with the X1 ROM from an empty working
directory. Set `MMX_ZERO_SABER_PERF_TEST=1`, `MMX_REPLAY_RECORD=0`,
`SDL_AUDIODRIVER=dummy` and `MMX_ZERO_TEST_ASSETS` to the locally extracted
X3 Zero cache. Add `MMX_BOSS_RUSH_TEST=1` and set `MMX_BOSS_RUSH_ZERO` to
the same cache to measure Boss Rush. The test writes `saber-performance.csv`,
warms each scenario before comparing diagnostics off/on, separates simulation
and drawing costs, and requires byte-identical resulting snapshots. Cases
cover idle, repeated swings, positioned real boss hits, and the full native
charge/two-buster/saber input chain. Readiness is bypassed only for the isolated
swing probes; the full-chain probe instead protects the player while charging.
For the same comparison in local co-op, add `MMX_PERF_COOP=1` for campaign, or
set `MMX_BOSS_RUSH_COOP` to the Zero cache and `MMX_BOSS_RUSH_COOP_P1_ZERO=1`
for Boss Rush. Port 1 attacks as Zero; port 2 remains X.

For native block-crushing checks, run `mmx_state_tests` from an empty working
directory with the X1 ROM, `MMX_ZERO_BLOCKS_TEST=1`, `MMX_ZERO_TEST_ASSETS`
pointing to the extracted X3 Zero cache, `MMX_REPLAY_RECORD=0`, and
`SDL_AUDIODRIVER=dummy`. The test arranges contact with a real Flame Mammoth
block and calls the native head/foot destruction routines through compiled
and interpreted execution; it checks Zero's innate abilities, X's upgrade
gates, and unchanged inventory in both behavior
modes. It does not validate the full route to the capsule.

## Change the framework dependency

The `snesrecomp` gitlink is the single source of truth for the framework
revision. Normal game-only changes should leave it untouched.

For coordinated framework work, create a branch inside the submodule, commit
and push that change first, then stage the new gitlink in this repository:

```bash
git -C snesrecomp switch -c your-branch
# edit, test, commit, and push snesrecomp
git add snesrecomp
git diff --cached --submodule=log -- snesrecomp
```

CMake rejects a different or dirty framework checkout by default so release
builds remain reproducible. While iterating on coordinated changes, opt out
explicitly with `-DMMX_ALLOW_UNPINNED_SNESRECOMP=ON`, or pass `--nopin` to the
macOS/Linux packaging script. Do not submit a game-repository gitlink until the
referenced framework commit is reachable from the configured submodule remote.

## Before opening a pull request

- Rerun `bash tools/bootstrap.sh` and confirm `git submodule status --recursive`
  has no `-`, `+`, or `U` prefix.
- Build the targets affected by the change and run CTest with
  `--output-on-failure`.
- Run `git diff --check` and inspect `git status --short` for ROMs, generated
  sources, build trees, or unrelated files.
- Describe which platforms and real runtime paths were exercised. Keep human
  gameplay validation separate from automated coverage.

## Licensing

License each contribution under the terms already applicable to the files
being changed. For contributions to code covered by the
[PolyForm Noncommercial License](LICENSE), also grant the
[additional permissions in `PERMISSIONS.md`](PERMISSIONS.md) for the rights
you control in your contribution, and authorize Matthew Stanley to grant
those same additional permissions for it.

These terms do not replace the licenses of third-party code or project
components expressly licensed under other terms. Do not contribute material
you lack permission to license, or copy from incompatible or proprietary sources.
