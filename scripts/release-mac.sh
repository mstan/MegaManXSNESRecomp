#!/usr/bin/env bash
#
# release-mac.sh — canonical macOS (Apple-Silicon) release builder.
#
# Codifies "what a release is" for this repo:
#   build-mac/<TARGET>  ->  <APP_NAME>.app  ->  dist/<REPO_NAME>-macos-arm64.zip
# The zip is self-contained (SDL2 bundled, ad-hoc signed) and ROM-FREE.
#
# Usage:
#   scripts/release-mac.sh             # build (if needed) + bundle + zip into dist/
#   scripts/release-mac.sh --publish   # also: gh release create <VERSION>
#   scripts/release-mac.sh --rebuild   # force a clean rebuild before packaging
#
# Requires: cmake, ninja, sdl2, dylibbundler (brew); gh (for --publish).
set -euo pipefail

# ======== per-repo config (EDIT THESE) ========
REPO_NAME="MegaManXSNESRecomp"      # GitHub repo / zip basename
APP_NAME="Mega Man X"        # .app display name (may contain spaces)
TARGET="MegaManXSNESRecomp"            # cmake target == built binary filename in build-mac/
SYSTEM="snes"            # nes | snes | genesis | vb | psx
VERSION="v1.0.8-macos"          # release tag, e.g. v0.1.0-macos
ROM_HINT="Mega Man X (USA) .sfc dump"        # human description of the ROM the user must supply
# ==============================================

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DIST="$ROOT/dist"
STAGE="$ROOT/dist/stage"
BREW="${HOMEBREW_PREFIX:-/opt/homebrew}"
NCPU="$(sysctl -n hw.ncpu)"
BIN="build-mac/$TARGET"

log(){ printf '\033[36m[release]\033[0m %s\n' "$*"; }
die(){ printf '\033[31m[release] FAIL:\033[0m %s\n' "$*" >&2; exit 1; }

# ---- 1. build (only if binary missing, or --rebuild). Per-system recipe. ----
build() {
  log "configure+build ($SYSTEM, arm64, oracle/beetle OFF) -> $BIN"
  # build vs framework HEAD: bypass the *.pin SHA gate during configure
  local pins=(); shopt -s nullglob
  for p in *.pin; do mv -f "$p" "$p.bak"; pins+=("$p"); done
  shopt -u nullglob
  restore_pins(){ for p in "${pins[@]:-}"; do [ -f "$p.bak" ] && mv -f "$p.bak" "$p"; done; }
  trap restore_pins RETURN

  case "$SYSTEM" in
    nes)
      # regen the recompiled C from the ROM, then build (oracle OFF)
      [ -n "${ROM:-}" ] && [ -f "${ROM:-}" ] && [ -x nesrecomp/build/recompiler/NESRecomp ] \
        && nesrecomp/build/recompiler/NESRecomp "$ROM" --game game.toml
      cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_NESTOPIA_ORACLE=OFF \
        -DCMAKE_PREFIX_PATH="$BREW" -DSDL2_DIR="$BREW/lib/cmake/SDL2"
      cmake --build build-mac -j"$NCPU" ;;
    snes)
      # NOTE: regen against the current framework tip emits unresolved-goto stubs
      # (hard error) for SMW/MMX; this repo ships a validated build-mac binary.
      # Reuse it. To rebuild you need the framework state that regen'd cleanly.
      cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$BREW" -DSDL2_DIR="$BREW/lib/cmake/SDL2"
      cmake --build build-mac --target "$TARGET" -j"$NCPU" ;;
    genesis)
      cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSONIC_REVERSE_DEBUG=OFF -DGEN_DEV_TRACE=OFF \
        -DCMAKE_PREFIX_PATH="$BREW" -DSDL2_DIR="$BREW/lib/cmake/SDL2"
      cmake --build build-mac --target "$TARGET" -j"$NCPU" ;;   # native target only (no _oracle)
    vb|psx)
      cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$BREW"
      cmake --build build-mac --target "$TARGET" -j"$NCPU" ;;    # runtime only (no beetle/oracle)
    *) die "unknown SYSTEM=$SYSTEM" ;;
  esac
}

# ---- 2. bundle .app (self-contained dylibs + ad-hoc codesign) ----
bundle() {
  BINPATH="$(find build-mac -type f -name "$TARGET" -perm +111 | head -1)"
  [ -n "$BINPATH" ] || die "built binary not found (build-mac/$TARGET)"
  APPDIR="$STAGE/$APP_NAME.app"
  rm -rf "$APPDIR"; mkdir -p "$APPDIR/Contents/MacOS" "$APPDIR/Contents/libs"
  cp "$BINPATH" "$APPDIR/Contents/MacOS/$APP_NAME"
  cat > "$APPDIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>org.recomp.$(echo "$APP_NAME" | tr -dc 'A-Za-z0-9')</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>${VERSION#v}</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
  log "dylibbundler + codesign"
  dylibbundler -of -b -x "$APPDIR/Contents/MacOS/$APP_NAME" \
    -d "$APPDIR/Contents/libs" -p @executable_path/../libs >/dev/null
  codesign --force --deep --sign - "$APPDIR" >/dev/null
}

# ---- 3. package zip (.app + README + keybinds; NO ROM) ----
package() {
  mkdir -p "$STAGE"
  # carry a keybinds.ini if the build produced one
  for kb in build-mac/keybinds.ini keybinds.ini; do
    [ -f "$kb" ] && cp "$kb" "$STAGE/keybinds.ini" && break
  done
  cat > "$STAGE/README.txt" <<TXT
$APP_NAME — static recompilation, native macOS (Apple Silicon)
$(printf '%*s' "${#APP_NAME}" '' | tr ' ' '=')================================================

This build runs the original game as native arm64 code (no emulator core
shipped). Gatekeeper note: it is ad-hoc signed, so on first launch right-click
the .app -> Open (or: xattr -dr com.apple.quarantine "$APP_NAME.app").

ROM NOT INCLUDED. Provide your own dump: $ROM_HINT

Run it (Terminal is the simplest path; these runners take the ROM as an arg):
  "$APP_NAME.app/Contents/MacOS/$APP_NAME"  /path/to/your/rom

Source: https://github.com/mstan/$REPO_NAME
TXT
  mkdir -p "$DIST"
  local zip="$DIST/$REPO_NAME-macos-arm64.zip"
  rm -f "$zip"
  ( cd "$STAGE" && zip -qr -X "$zip" "$APP_NAME.app" README.txt $( [ -f keybinds.ini ] && echo keybinds.ini ) )
  log "packaged $zip"
  unzip -l "$zip" | tail -n +2
}

# ---- 4. publish (gh release) ----
publish() {
  local zip="$DIST/$REPO_NAME-macos-arm64.zip"
  [ -f "$zip" ] || die "no zip to publish"
  log "gh release create $VERSION"
  gh release create "$VERSION" "$zip" \
    --repo "mstan/$REPO_NAME" \
    --title "$APP_NAME — macOS (Apple Silicon) $VERSION" \
    --notes-file "$ROOT/RELEASE.md" 2>/dev/null \
    || gh release upload "$VERSION" "$zip" --repo "mstan/$REPO_NAME" --clobber
}

DO_PUBLISH=0; DO_REBUILD=0
for a in "$@"; do case "$a" in --publish) DO_PUBLISH=1;; --rebuild) DO_REBUILD=1;; esac; done

[ "$DO_REBUILD" = 1 ] && rm -rf build-mac
{ [ -x "$BIN" ] && [ "$DO_REBUILD" = 0 ]; } || build
bundle
package
[ "$DO_PUBLISH" = 1 ] && publish
log "done."
