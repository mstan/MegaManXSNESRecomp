# Mega Man X — macOS (Apple Silicon) build

Native arm64 macOS build of Mega Man X, attached to release **v1.0.8** as
`MegaManXSNESRecomp-macos-arm64.zip`.

## What this is
- The original game statically recompiled to native arm64 (no emulator core shipped).
- Self-contained `.app`: SDL2 bundled via `@executable_path`, ad-hoc codesigned.
- Verified by manual play on Apple Silicon (looks/sounds correct on the golden path).


## Install
1. Download `MegaManXSNESRecomp-macos-arm64.zip` from the **v1.0.8** release and unzip.
2. First launch: right-click `Mega Man X.app` -> Open (ad-hoc signed), or
   `xattr -dr com.apple.quarantine "Mega Man X.app"`.
3. ROM not included — supply your own dump: Mega Man X (USA) .sfc dump
4. Run: `"Mega Man X.app/Contents/MacOS/Mega Man X" /path/to/rom`

## Build it yourself
`scripts/release-mac.sh` reproduces this artifact (build -> .app -> zip);
`scripts/release-mac.sh --publish` re-attaches it to the latest release.
Requires: `brew install cmake ninja sdl2 dylibbundler` on Apple Silicon.
