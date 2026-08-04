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
# It configures + builds with cmake, then wraps the ELF into a self-contained
# x86_64 AppImage. State policy (identical to the Windows zip, where everything
# lives next to the exe): config.ini, keybinds.ini, rom.cfg, saves/ and mods/
# all live NEXT TO the .AppImage file. The engine anchors there itself —
# host_paths.c prefers $APPIMAGE over /proc/self/exe precisely so state never
# resolves into the read-only squashfs mount. The AppRun:
#   * seeds/refreshes the release-owned mod catalog beside the .AppImage
#     (mod_runtime resolves "mods" against the anchored cwd, so the catalog
#     bundled inside the mount is invisible to it) while never touching
#     user-installed packages or user config,
#   * auto-finds a ROM (by extension) sitting next to the .AppImage and passes
#     it as argv[1] — so the user just drops their ROM beside the AppImage,
#   * exports the SDL hints that make controllers work out-of-the-box on a
#     Steam Deck (reads the pad natively instead of Steam's keyboard remap).
#
# After packaging, tools/test_appimage_layout.sh runs against the AppDir and
# the build FAILS if state ever lands inside the read-only payload or a user
# edit to config.ini does not survive a relaunch.
#
# Usage:
#   bash tools/build-linux.sh                 # prod AppImage (default)
#   bash tools/build-linux.sh --version 1.3.2 # stamp + name a release build
#   bash tools/build-linux.sh --config debug  # debug build (TCP server + rings)
#   bash tools/build-linux.sh --regen         # regen src/gen first (tools/regen.sh)
#   bash tools/build-linux.sh --run           # launch the AppImage after building
#   bash tools/build-linux.sh --no-package    # configure + build only, skip AppImage
#   bash tools/build-linux.sh --nopin         # allow local snesrecomp changes
#   bash tools/build-linux.sh --out DIR       # where to drop the .AppImage
#   bash tools/build-linux.sh --jobs N        # parallel build jobs (default: nproc)
#
# Prereqs: cmake, a C/C++ toolchain, SDL3 (or SNESRECOMP_SDL_BACKEND=SDL2 with
# libsdl2-dev), libgl1-mesa-dev. linuxdeploy/appimagetool are fetched into the
# build tree and verified against pinned SHA-256s (reproducible packaging).
# Regen needs a verified ROM at the repo root (see tools/regen.sh).
set -euo pipefail

# ============================ PER-GAME CONFIG ===============================
APP_NAME="MegaManX"
RELEASE_SLUG="MegaManXSNESRecomp"           # matches the windows zip prefix
CMAKE_TARGET="MegaManXSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
REGEN_CMD="bash tools/regen.sh usa --no-tests"
PREBUILD_CMD=""
POSTBUILD_CMD=""
BOXART="recomp/launcher/boxart.tga"          # AppImage icon source (optional)
# Release-owned mod catalog: packaging fails if the built tree is missing it
# rather than shipping an AppImage with an empty Mods page.
REQUIRED_MOD_MANIFESTS=(
  "packages/megaman-x.enhancement.widescreen/1.0.0/manifest.toml"
)
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )
# ============================================================================

# Pinned AppImage tooling (same pins as the Tomba Linux release).
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA=421ca71d5c69ea97c6309276232990d43df1dcece0edfaa26bbf926ff96ed12e
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0

CONFIG="prod"
DO_REGEN=0
DO_RUN=0
DO_PACKAGE=1
NOPIN=0
VERSION=""
JOBS="$(nproc 2>/dev/null || echo 4)"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/release-linux"

while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2;;
    --prod) CONFIG="prod"; shift;;
    --debug) CONFIG="debug"; shift;;
    --version) VERSION="$2"; shift 2;;
    --regen) DO_REGEN=1; shift;;
    --run) DO_RUN=1; shift;;
    --no-package) DO_PACKAGE=0; shift;;
    --nopin) NOPIN=1; shift;;
    --out) OUT="$2"; shift 2;;
    --jobs) JOBS="$2"; shift 2;;
    -h|--help) sed -n '2,48p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
case "$CONFIG" in prod) FLAGS=( "${PROD_CMAKE_FLAGS[@]}" );; debug) FLAGS=( "${DEBUG_CMAKE_FLAGS[@]}" );;
  *) echo "--config must be prod or debug (got '$CONFIG')" >&2; exit 2;; esac

# Default the version to the checked-out tag so a release build can't ship
# stamped "dev" by accident; explicit --version always wins.
if [ -z "$VERSION" ]; then
  VERSION="$(git -C "$REPO" describe --tags --exact-match 2>/dev/null | sed 's/^v//' || true)"
  [ -n "$VERSION" ] || VERSION="dev"
fi
FLAGS+=( -DSNESRECOMP_BUILD_VERSION="$VERSION" )

# SDL3 is the default; SNESRECOMP_SDL_BACKEND=SDL2 selects the compatibility
# package. Prefer the host package over any cross-platform dependency prefix.
SDL_BACKEND="${SNESRECOMP_SDL_BACKEND:-SDL3}"
SDL_CFG_DIR="$( { find /usr/lib /usr/lib64 /usr/local/lib -type d -path "*cmake/$SDL_BACKEND" 2>/dev/null || true; } | head -1 )"
FLAGS+=( -DSNESRECOMP_SDL_BACKEND="$SDL_BACKEND" )
[ -n "$SDL_CFG_DIR" ] && FLAGS+=( "-D${SDL_BACKEND}_DIR=$SDL_CFG_DIR" )

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
echo "==================== $APP_NAME ($CONFIG, v$VERSION) ===================="
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

echo "[1/4] configure ($CONFIG: ${FLAGS[*]})"
cmake -S "$REPO" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release "${FLAGS[@]}"
echo "[2/4] build ($CMAKE_TARGET, -j$JOBS)"
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

echo "[3/4] package AppImage"
mkdir -p "$OUT"
EXE="$(basename "$BIN")"
SLUG="$(echo "$APP_NAME" | tr '[:upper:] ' '[:lower:]-' | tr -cd 'a-z0-9-')"
WORK="$(mktemp -d)"   # cleaned by the EXIT trap registered above
APPDIR="$WORK/AppDir"; mkdir -p "$APPDIR"

# Pinned tooling, fetched into the build tree and SHA-verified so packaging is
# reproducible regardless of what happens to live in ~/recomp-tools.
TOOLS_DIR="$BUILD/appimage-tools"
mkdir -p "$TOOLS_DIR"
fetch_tool() { # url sha dest
  local url="$1" sha="$2" dest="$3"
  if [ ! -f "$dest" ] || [ "$(sha256sum "$dest" | awk '{print $1}')" != "$sha" ]; then
    echo "      fetching $(basename "$dest")"
    curl -fL --retry 3 "$url" -o "$dest.tmp"
    printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c - >/dev/null
    mv "$dest.tmp" "$dest"
  fi
  chmod 0755 "$dest"
}
LINUXDEPLOY_BIN="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL_BIN="$TOOLS_DIR/appimagetool-x86_64.AppImage"
fetch_tool "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA" "$LINUXDEPLOY_BIN"
fetch_tool "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA" "$APPIMAGETOOL_BIN"
LINUXDEPLOY="$LINUXDEPLOY_BIN --appimage-extract-and-run"
APPIMAGETOOL="$APPIMAGETOOL_BIN --appimage-extract-and-run"

# Icon: real boxart when ImageMagick is available, flat placeholder otherwise.
ICON="$WORK/$SLUG.png"
IMAGE_TOOL=""
command -v magick >/dev/null 2>&1 && IMAGE_TOOL=magick
[ -z "$IMAGE_TOOL" ] && command -v convert >/dev/null 2>&1 && IMAGE_TOOL=convert
if [ -n "$IMAGE_TOOL" ] && [ -f "$REPO/$BOXART" ]; then
  "$IMAGE_TOOL" "$REPO/$BOXART" -resize 240x240 -background transparent \
      -gravity center -extent 256x256 "$ICON"
else
  python3 - "$ICON" "$SLUG" <<'PY'
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
fi

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
    --desktop-file "$WORK/$SLUG.desktop" --icon-file "$ICON"

# The ImGui pre-boot launcher loads fonts + images from assets/ next to the exe
# (SDL_GetBasePath resolves to usr/bin inside the AppImage). CMake's launcher
# POST_BUILD staged them beside the build ELF; carry them into the AppDir so the
# launcher renders inside the packaged AppImage.
[ -d "$(dirname "$BIN")/assets" ] || {
  echo "ERROR: recomp-ui launcher assets/ missing beside $BIN" >&2
  exit 1
}
echo "      staging launcher assets/ -> AppDir/usr/bin/assets"
cp -r "$(dirname "$BIN")/assets" "$APPDIR/usr/bin/assets"

# The release-owned mod catalog. mod_runtime resolves "mods" against the
# anchored cwd (next to the .AppImage), NOT the mount, so the AppRun seeds it
# beside the .AppImage at launch; here we just carry the payload and refuse to
# publish an AppImage whose Mods page would come up empty.
BUILT_MODS="$(dirname "$BIN")/mods"
for manifest in "${REQUIRED_MOD_MANIFESTS[@]}"; do
  [ -f "$BUILT_MODS/$manifest" ] || {
    echo "ERROR: built-in mod catalog missing: $BUILT_MODS/$manifest" >&2
    exit 1
  }
done
echo "      staging mod catalog   -> AppDir/usr/bin/mods"
cp -r "$BUILT_MODS" "$APPDIR/usr/bin/mods"

# Custom AppRun. State policy: everything user-visible lives NEXT TO the
# .AppImage, exactly like the Windows zip keeps it next to the exe. The engine
# itself anchors there ($APPIMAGE is preferred over /proc/self/exe in
# host_paths.c), so this script must keep APPIMAGE exported. Here we:
#   * seed/refresh the RELEASE-OWNED mod catalog beside the .AppImage (an
#     AppImage update repairs/updates its bundled packages) without touching
#     user-installed packages, config.ini, keybinds.ini, rom.cfg, or saves/,
#   * read the controller natively on a Steam Deck,
#   * find the ROM next to the .AppImage and run from its folder.
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
# Seed/refresh the release-owned mod catalog beside the .AppImage. Directory
# trees get mkdir -p + cp of their CONTENTS (never a file where a dir belongs,
# never a dir where a file belongs); user files are left alone entirely.
if [ -d "\$HERE/usr/bin/mods" ] && [ -w "\$ROMDIR" ]; then
    mkdir -p "\$ROMDIR/mods" 2>/dev/null || true
    cp -a "\$HERE/usr/bin/mods/." "\$ROMDIR/mods/" 2>/dev/null || true
    chmod -R u+rwX "\$ROMDIR/mods" 2>/dev/null || true
fi
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

APP="$OUT/$RELEASE_SLUG-linux-$VERSION-x86_64.AppImage"
rm -f "$APP"
ARCH=x86_64 $APPIMAGETOOL "$APPDIR" "$APP"
chmod +x "$APP"
echo "      BUILT: $APP ($(du -h "$APP" | cut -f1))"

echo "[4/4] layout test (state next to the .AppImage, payload stays read-only)"
bash "$REPO/tools/test_appimage_layout.sh" "$APPDIR"

sha256sum "$APP"

if [ "$DO_RUN" = "1" ]; then echo "[run] $APP"; "$APP" || true; fi
