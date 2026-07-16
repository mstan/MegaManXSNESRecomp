# Contributing

Thank you for helping improve Mega Man X SNES Recomp. The project keeps ROM
data and generated game code out of Git, so a working checkout has two explicit
inputs: the pinned `snesrecomp` submodule and your own legally obtained ROM.

## Set up a checkout

```bash
git clone --recurse-submodules https://github.com/navjack/MegaManXSNESRecomp-macos-linux-wide.git
cd MegaManXSNESRecomp-macos-linux-wide
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
msbuild mmx.sln /p:Configuration=Release /p:Platform=x64 /m
```

Run `bash tools/regen.sh usa` when changing the recompiler or generated-code
configuration; omitting `--no-tests` also runs the real framework test suite.
Use `--strict-idempotent` when a change can affect emitted C.

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
