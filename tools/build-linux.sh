#!/usr/bin/env bash
# build-linux.sh — DEFINITIVE Linux build/package script for a recomp game.
#
# This is the Linux counterpart to tools/make_release.ps1 (Windows). It mirrors
# the same prod-vs-debug discipline:
#
#   prod  (default) — strips ALL developer tooling: no TCP debug server, no
#                     observability rings, no oracle backend. The shipped build.
#   debug           — compiles the TCP debug server + rings back in.
#
# It configures + builds with cmake/make, then wraps the ELF into a
# self-contained x86_64 AppImage whose AppRun:
#   * auto-finds a ROM (by extension) sitting next to the .AppImage and passes
#     it as argv[1] — so the user just drops their ROM beside the AppImage,
#   * exports the SDL hints that make controllers work out-of-the-box on a
#     Steam Deck (reads the pad natively instead of Steam's keyboard remap).
#
# Usage:
#   bash tools/build-linux.sh                 # prod AppImage (default)
#   bash tools/build-linux.sh --config debug  # debug build (TCP server + rings)
#   bash tools/build-linux.sh --regen         # regen src/gen first (tools/regen.sh)
#   bash tools/build-linux.sh --run           # launch the AppImage after building
#   bash tools/build-linux.sh --no-package    # configure + build only, skip AppImage
#   bash tools/build-linux.sh --nopin         # allow local snesrecomp changes
#   bash tools/build-linux.sh --out DIR       # where to drop the .AppImage
#   bash tools/build-linux.sh --jobs N        # parallel build jobs (default: nproc)
#
# Prereqs: cmake, a C/C++ toolchain, libsdl2-dev, libgl1-mesa-dev, and the
# AppImage tools at ~/recomp-tools/{linuxdeploy,appimagetool}. Regen needs a
# verified ROM at the repo root (see tools/regen.sh).
set -euo pipefail

# ============================ PER-GAME CONFIG ===============================
APP_NAME="MegaManX"
CMAKE_TARGET="MegaManXSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
REGEN_CMD="bash tools/regen.sh usa --no-tests"
PREBUILD_CMD=""
POSTBUILD_CMD=""
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )
# ============================================================================

CONFIG="prod"
DO_REGEN=0
DO_RUN=0
DO_PACKAGE=1
NOPIN=0
JOBS="$(nproc 2>/dev/null || echo 4)"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/release-linux"

while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2;;
    --prod) CONFIG="prod"; shift;;
    --debug) CONFIG="debug"; shift;;
    --regen) DO_REGEN=1; shift;;
    --run) DO_RUN=1; shift;;
    --no-package) DO_PACKAGE=0; shift;;
    --nopin) NOPIN=1; shift;;
    --out) OUT="$2"; shift 2;;
    --jobs) JOBS="$2"; shift 2;;
    -h|--help) sed -n '2,40p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
case "$CONFIG" in prod) FLAGS=( "${PROD_CMAKE_FLAGS[@]}" );; debug) FLAGS=( "${DEBUG_CMAKE_FLAGS[@]}" );;
  *) echo "--config must be prod or debug (got '$CONFIG')" >&2; exit 2;; esac

# Point cmake at the HOST's Linux SDL2. Several game CMakeLists pin a bundled
# (Windows) SDL2 dev pack on CMAKE_PREFIX_PATH for the MSVC build; -DSDL2_DIR is
# the explicit CONFIG-mode hint and is consulted before CMAKE_PREFIX_PATH, so the
# real Linux .so links instead of a Windows import lib. Harmless when the game
# already finds system SDL2.
SDL2_CFG_DIR="$( { find /usr/lib /usr/lib64 /usr/local/lib -type d -path '*cmake/SDL2' 2>/dev/null || true; } | head -1 )"
[ -n "$SDL2_CFG_DIR" ] && FLAGS+=( -DSDL2_DIR="$SDL2_CFG_DIR" )

# Single cleanup hook: remove the AppDir scratch dir and restore generated files.
WORK=""; RAN_PREBUILD=0
cleanup() {
  [ -n "$WORK" ] && rm -rf "$WORK"
  # Restore the repo's gen state (re-run POSTBUILD) even if the build failed.
  [ "$RAN_PREBUILD" = "1" ] && [ -n "$POSTBUILD_CMD" ] && { echo "[postbuild] $POSTBUILD_CMD"; eval "$POSTBUILD_CMD" || true; }
  return 0   # never let the trap's last test override the script's real exit code
}
trap cleanup EXIT

BUILD="$REPO/build-linux-$CONFIG"
echo "==================== $APP_NAME ($CONFIG) ===================="
cd "$REPO"

[ -f "$REPO/snesrecomp/runner/runner.cmake" ] || {
  echo "ERROR: snesrecomp is not initialized; run 'bash tools/bootstrap.sh' first." >&2
  exit 1
}

if [ "$NOPIN" = "1" ]; then
  FLAGS+=( -DMMX_ALLOW_UNPINNED_SNESRECOMP=ON )
  echo "      pin bypass: MMX_ALLOW_UNPINNED_SNESRECOMP=ON"
else
  FLAGS+=( -DMMX_ALLOW_UNPINNED_SNESRECOMP=OFF )
fi

if [ "$DO_REGEN" = "1" ] && [ -n "$REGEN_CMD" ]; then
  echo "[regen] $REGEN_CMD"
  eval "$REGEN_CMD"
fi

if [ -n "$PREBUILD_CMD" ]; then
  echo "[prebuild] $PREBUILD_CMD"
  RAN_PREBUILD=1
  eval "$PREBUILD_CMD"
fi

echo "[1/3] configure ($CONFIG: ${FLAGS[*]})"
cmake -S "$REPO" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release "${FLAGS[@]}"
echo "[2/3] build ($CMAKE_TARGET, -j$JOBS)"
cmake --build "$BUILD" --target "$CMAKE_TARGET" -j"$JOBS"

# Locate the produced ELF by magic (the NTFS mount marks every file executable,
# so the exec bit is meaningless here).
BIN=""
while IFS= read -r f; do
  if [ "$(basename "$f")" = "$CMAKE_TARGET" ] && file -b "$f" 2>/dev/null | grep -q "ELF.*executable"; then BIN="$f"; break; fi
done < <(find "$BUILD" -maxdepth 3 -type f)
[ -n "$BIN" ] || { echo "ERROR: no ELF named '$CMAKE_TARGET' under $BUILD" >&2; exit 1; }
echo "      ELF: $BIN ($(du -h "$BIN" | cut -f1))"

if [ "$DO_PACKAGE" = "0" ]; then echo "      (--no-package) done."; exit 0; fi

echo "[3/3] package AppImage"
TOOLS="$HOME/recomp-tools"
LINUXDEPLOY="$TOOLS/linuxdeploy --appimage-extract-and-run"
APPIMAGETOOL="$TOOLS/appimagetool --appimage-extract-and-run"
mkdir -p "$OUT"
EXE="$(basename "$BIN")"
SLUG="$(echo "$APP_NAME" | tr '[:upper:] ' '[:lower:]-' | tr -cd 'a-z0-9-')"
WORK="$(mktemp -d)"   # cleaned by the EXIT trap registered above
APPDIR="$WORK/AppDir"; mkdir -p "$APPDIR"

# Minimal 256x256 PNG icon (flat color from the slug; no image deps).
python3 - "$WORK/$SLUG.png" "$SLUG" <<'PY'
import sys, zlib, struct, hashlib
out, slug = sys.argv[1], sys.argv[2]
h = hashlib.md5(slug.encode()).digest()
r, g, b = h[0] | 0x30, h[1] | 0x30, h[2] | 0x30
N = 256; row = bytes([0]) + bytes([r, g, b]) * N; raw = row * N
def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(out, "wb").write(png)
PY

cat > "$WORK/$SLUG.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
Exec=$EXE
Icon=$SLUG
Categories=Game;
Terminal=false
EOF

$LINUXDEPLOY --appdir "$APPDIR" --executable "$BIN" \
    --desktop-file "$WORK/$SLUG.desktop" --icon-file "$WORK/$SLUG.png"

# Custom AppRun: bundle libs, read the controller natively on a Steam Deck, find
# the ROM next to the .AppImage, run from the ROM's folder so saves land there.
rm -f "$APPDIR/AppRun"   # linuxdeploy leaves it a symlink to the real exe
cat > "$APPDIR/AppRun" <<EOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export LD_LIBRARY_PATH="\$HERE/usr/lib:\${LD_LIBRARY_PATH}"
# Steam Deck: read the built-in pad as a real gamepad instead of letting Steam's
# desktop layout retype it as keyboard (which otherwise sends Esc on B, etc.).
export SDL_JOYSTICK_HIDAPI_STEAM=1
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
SELF="\${APPIMAGE:-\$0}"
ROMDIR="\$(dirname "\$(readlink -f "\$SELF")")"
ROM=""
for ext in $ROM_EXTS; do
    [ "\$ext" = "none" ] && break
    for f in "\$ROMDIR"/*."\$ext"; do [ -e "\$f" ] && ROM="\$f" && break 2; done
done
cd "\$ROMDIR" 2>/dev/null || true
if [ "\$#" -eq 0 ]; then
    [ -n "\$ROM" ] && exec "\$HERE/usr/bin/$EXE" "\$ROM"
    exec "\$HERE/usr/bin/$EXE" $EXTRA_ARGS
fi
exec "\$HERE/usr/bin/$EXE" "\$@"
EOF
chmod +x "$APPDIR/AppRun"

# The ImGui pre-boot launcher loads fonts + images from assets/ next to the exe
# (SDL_GetBasePath resolves to usr/bin inside the AppImage). CMake's launcher_ng
# POST_BUILD staged them beside the build ELF; carry them into the AppDir so the
# launcher renders inside the packaged AppImage.
if [ -d "$(dirname "$BIN")/assets" ]; then
    echo "      staging launcher assets/ -> AppDir/usr/bin/assets"
    mkdir -p "$APPDIR/usr/bin"
    cp -r "$(dirname "$BIN")/assets" "$APPDIR/usr/bin/assets"
fi

APP="$OUT/$APP_NAME-x86_64.AppImage"
ARCH=x86_64 $APPIMAGETOOL "$APPDIR" "$APP"
chmod +x "$APP"
echo "      BUILT: $APP ($(du -h "$APP" | cut -f1))"

if [ "$DO_RUN" = "1" ]; then echo "[run] $APP"; "$APP" || true; fi
