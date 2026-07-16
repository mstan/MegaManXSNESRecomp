#!/usr/bin/env bash
# build-macos.sh — DEFINITIVE macOS build/package script for a recomp game.
#
# macOS counterpart to tools/build-linux.sh and tools/make_release.ps1, with the
# same prod-vs-debug discipline (prod strips the TCP debug server + rings).
#
# Run this on a Mac (it cannot be built on Linux/Windows). It configures and
# builds with clang, then
# bundles a relocatable <APP_NAME>.app (dylibbundler copies SDL2 in and rewrites
# the install names) and a <APP_NAME>.dmg for distribution.
#
# Usage:
#   bash tools/build-macos.sh                  # prod .app + .dmg (default)
#   bash tools/build-macos.sh --config debug   # debug build (TCP server + rings)
#   bash tools/build-macos.sh --rom path --regen # regen src/gen first
#   bash tools/build-macos.sh --no-dmg         # .app only
#   bash tools/build-macos.sh --nopin          # allow local snesrecomp changes
#   bash tools/build-macos.sh --arch arm64|x86_64|universal   # default: host arch
#
# Prereqs (Homebrew): cmake, sdl2, dylibbundler, create-dmg (optional).
#   brew install cmake sdl2 dylibbundler create-dmg
set -euo pipefail

# ============================ PER-GAME CONFIG ===============================
APP_NAME="MegaManX"
CMAKE_TARGET="MegaManXSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
PREBUILD_CMD=""
POSTBUILD_CMD=""
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )
BUNDLE_ID="com.mstan.megamanxrecomp"
# ============================================================================

CONFIG="prod"; DO_REGEN=0; DO_DMG=1; NOPIN=0
ROM_PATH=""
ARCH="$(uname -m)"
# A translated x86_64 shell on Apple Silicon still needs an arm64 default so it
# links against the normal /opt/homebrew dependency set.
if [ "$(uname -s)" = "Darwin" ] &&
   [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ]; then
  ARCH="arm64"
fi
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/release-macos"

while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2;;
    --prod) CONFIG="prod"; shift;;
    --debug) CONFIG="debug"; shift;;
    --regen) DO_REGEN=1; shift;;
    --rom) ROM_PATH="$2"; shift 2;;
    --no-dmg) DO_DMG=0; shift;;
    --nopin) NOPIN=1; shift;;
    --arch) ARCH="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    -h|--help) sed -n '2,30p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
case "$CONFIG" in prod) FLAGS=( "${PROD_CMAKE_FLAGS[@]}" );; debug) FLAGS=( "${DEBUG_CMAKE_FLAGS[@]}" );;
  *) echo "--config must be prod or debug" >&2; exit 2;; esac
if [ "$NOPIN" = "1" ]; then
  FLAGS+=( -DMMX_ALLOW_UNPINNED_SNESRECOMP=ON )
else
  FLAGS+=( -DMMX_ALLOW_UNPINNED_SNESRECOMP=OFF )
fi
[ "$(uname -s)" = "Darwin" ] || { echo "ERROR: run this on macOS." >&2; exit 1; }

case "$ARCH" in
  universal) OSX_ARCHS="x86_64;arm64";;
  arm64|x86_64) OSX_ARCHS="$ARCH";;
  *) echo "--arch must be arm64, x86_64, or universal" >&2; exit 2;;
esac

BUILD="$REPO/build-macos-$CONFIG"
echo "==================== $APP_NAME ($CONFIG, $ARCH) ===================="
cd "$REPO"

[ -f "$REPO/snesrecomp/runner/runner.cmake" ] || {
  echo "ERROR: snesrecomp is not initialized; run 'bash tools/bootstrap.sh' first." >&2
  exit 1
}

if [ -z "$ROM_PATH" ]; then
  for candidate in "$REPO"/mmx.sfc "$REPO"/mmx.smc "$REPO"/roms/*.sfc "$REPO"/roms/*.smc; do
    if [ -f "$candidate" ]; then ROM_PATH="$candidate"; break; fi
  done
fi

if [ "$DO_REGEN" = "1" ]; then
  [ -n "$ROM_PATH" ] || { echo "ERROR: --regen requires --rom path (or a ROM in mmx.sfc/mmx.smc/roms/)" >&2; exit 1; }
  case "$ROM_PATH" in
    *.smc) cp "$ROM_PATH" "$REPO/mmx.sfc" ;;
    *.sfc) cp "$ROM_PATH" "$REPO/mmx.sfc" ;;
    *) echo "ERROR: ROM must have .sfc or .smc extension" >&2; exit 1;;
  esac
  bash "$REPO/tools/regen.sh" usa --no-tests
fi

RAN_PREBUILD=0
cleanup() { [ "$RAN_PREBUILD" = "1" ] && [ -n "$POSTBUILD_CMD" ] && { echo "[postbuild] $POSTBUILD_CMD"; eval "$POSTBUILD_CMD" || true; }; return 0; }
trap cleanup EXIT

if [ -n "$PREBUILD_CMD" ]; then echo "[prebuild] $PREBUILD_CMD"; RAN_PREBUILD=1; eval "$PREBUILD_CMD"; fi

echo "[1/4] configure"
cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCHS" "${FLAGS[@]}"
echo "[2/4] build ($CMAKE_TARGET)"
cmake --build "$BUILD" --target "$CMAKE_TARGET" -j"$(sysctl -n hw.ncpu)"

BIN="$(find "$BUILD" -type f -path "*/Contents/MacOS/$CMAKE_TARGET" | head -1)"
[ -n "$BIN" ] || { echo "ERROR: no binary named '$CMAKE_TARGET' under $BUILD" >&2; exit 1; }
echo "      bin: $BIN"

echo "[3/4] bundle $APP_NAME.app"
mkdir -p "$OUT"
APPDIR="$OUT/$APP_NAME.app"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/Contents/MacOS" "$APPDIR/Contents/Resources" "$APPDIR/Contents/Frameworks"
# The real game binary lives next to a launcher that finds the ROM in the same
# folder as the .app and runs from there (so saves land beside the .app).
cp "$BIN" "$APPDIR/Contents/MacOS/$CMAKE_TARGET"
cp "$BUILD/$CMAKE_TARGET.app/Contents/Resources/MegaManX.icns" \
   "$APPDIR/Contents/Resources/MegaManX.icns"
cat > "$APPDIR/Contents/MacOS/$APP_NAME" <<EOF
#!/bin/sh
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
# Folder the .app sits in (writable, user-facing).
APPDIR="\$(cd "\$DIR/../../.." && pwd)"
export SDL_JOYSTICK_HIDAPI_STEAM=1
cd "\$APPDIR" 2>/dev/null || true
ROM=""
for ext in $ROM_EXTS; do
    [ "\$ext" = "none" ] && break
    for f in "\$APPDIR"/*."\$ext"; do [ -e "\$f" ] && ROM="\$f" && break 2; done
done
if [ "\$#" -eq 0 ]; then
    [ -n "\$ROM" ] && exec "\$DIR/$CMAKE_TARGET" "\$ROM"
    exec "\$DIR/$CMAKE_TARGET" $EXTRA_ARGS
fi
exec "\$DIR/$CMAKE_TARGET" "\$@"
EOF
chmod +x "$APPDIR/Contents/MacOS/$APP_NAME"

cat > "$APPDIR/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>0.1.0</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleIconFile</key><string>MegaManX.icns</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF

# Copy + relink the SDL2 dylib (and any other non-system deps) into the bundle.
if command -v dylibbundler >/dev/null 2>&1; then
  dylibbundler -od -b -x "$APPDIR/Contents/MacOS/$CMAKE_TARGET" \
      -d "$APPDIR/Contents/Frameworks" -p @executable_path/../Frameworks
else
  SDL_DYLIB="$(otool -L "$APPDIR/Contents/MacOS/$CMAKE_TARGET" | awk '/libSDL2.*dylib/ { print $1; exit }')"
  if [ -n "$SDL_DYLIB" ] && [ -f "$SDL_DYLIB" ]; then
    SDL_NAME="$(basename "$SDL_DYLIB")"
    cp "$SDL_DYLIB" "$APPDIR/Contents/Frameworks/$SDL_NAME"
    install_name_tool -change "$SDL_DYLIB" "@executable_path/../Frameworks/$SDL_NAME" \
      "$APPDIR/Contents/MacOS/$CMAKE_TARGET"
    echo "      bundled $SDL_NAME (dylibbundler unavailable)"
  else
    echo "      WARNING: SDL2 dylib not found — .app will need a system SDL2."
  fi
fi
# Ad-hoc codesign so Gatekeeper lets it run locally (no Developer ID required).
codesign --force --deep --sign - "$APPDIR" 2>/dev/null || \
  echo "      (codesign skipped — run 'xattr -dr com.apple.quarantine' if Gatekeeper blocks it)"
echo "      BUILT: $APPDIR"

if [ "$DO_DMG" = "1" ]; then
  echo "[4/4] package .dmg"
  DMG="$OUT/$APP_NAME-macos-$ARCH.dmg"
  rm -f "$DMG"
  if command -v create-dmg >/dev/null 2>&1; then
    create-dmg --volname "$APP_NAME" --app-drop-link 420 180 "$DMG" "$APPDIR" || \
      hdiutil create -volname "$APP_NAME" -srcfolder "$APPDIR" -ov -format UDZO "$DMG"
  else
    hdiutil create -volname "$APP_NAME" -srcfolder "$APPDIR" -ov -format UDZO "$DMG"
  fi
  echo "      BUILT: $DMG"
else
  echo "[4/4] (--no-dmg) skipped"
fi
