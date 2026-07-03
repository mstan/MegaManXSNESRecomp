#!/usr/bin/env bash
# make_release_rmx.sh — build the Rockman X (Japan v1.1) Windows release zip.
#
# JP ships via the CMake/mingw Release build; the MSVC mmx.sln is USA-only.
# Stage layout mirrors tools/make_release.ps1 (the USA release):
#   RockmanXSNESRecomp.exe  (Release, console-free via -mwindows)
#   SDL2.dll + mingw runtime DLLs (bundled via ldd so it runs without msys2)
#   config.ini              (repo copy — the launcher rewrites it in place)
#   launcher/               (shared RmlUi assets + per-game boxart)
#   README.md
#
# Zip lands in release-stage/RockmanXSNESRecomp-windows-<Version>.zip.
# Usage: bash tools/make_release_rmx.sh [Version]   (default 0.1.0)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${1:-0.1.0}"
export PATH="/c/msys64/mingw64/bin:$PATH"

BUILD="$ROOT/build-release-jp"
STAGE_NAME="RockmanXSNESRecomp-windows-$VERSION"
STAGE="$ROOT/release-stage/$STAGE_NAME"
ZIP="$ROOT/release-stage/$STAGE_NAME.zip"

echo "=== configure + build (mingw Release, console-free) ==="
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DSDL2_DIR=C:/msys64/mingw64/lib/cmake/SDL2 \
  -DCMAKE_EXE_LINKER_FLAGS="-mwindows"
cmake --build "$BUILD" --target RockmanXSNESRecomp -j6

EXE="$BUILD/RockmanXSNESRecomp.exe"
[ -f "$EXE" ] || { echo "build produced no exe" >&2; exit 1; }

echo "=== stage ==="
rm -rf "$STAGE"; mkdir -p "$STAGE/launcher"
cp "$EXE" "$STAGE/"
cp "$ROOT/config.ini" "$STAGE/"
[ -f "$ROOT/README.md" ] && cp "$ROOT/README.md" "$STAGE/"

# Launcher assets: shared RmlUi set + per-game boxart (mirrors the vcxproj
# CopyLauncherAssets + CopyGameLauncherAssets targets).
cp -r "$ROOT/snesrecomp/runner/src/launcher/assets/." "$STAGE/launcher/"
[ -d "$ROOT/recomp/launcher" ] && cp -r "$ROOT/recomp/launcher/." "$STAGE/launcher/"

# Bundle the runtime DLLs the mingw exe links (SDL2 + libgcc/libstdc++/
# libwinpthread/etc.) so it runs on a machine without msys2. ldd lists them;
# copy only the ones under the mingw prefix (system DLLs stay on the target).
echo "=== bundle mingw runtime DLLs ==="
ldd "$EXE" | awk '/mingw64/ {print $3}' | while read -r dll; do
  [ -f "$dll" ] && cp -u "$dll" "$STAGE/" && echo "  + $(basename "$dll")"
done
# SDL2 is dlopen'd/linked; ensure it's present even if ldd missed it.
[ -f "$STAGE/SDL2.dll" ] || cp /c/msys64/mingw64/bin/SDL2.dll "$STAGE/"

echo "=== zip ==="
rm -f "$ZIP"
if command -v zip >/dev/null 2>&1; then
  ( cd "$STAGE" && zip -qr "$ZIP" . )
else
  powershell.exe -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$STAGE")\*' -DestinationPath '$(cygpath -w "$ZIP")' -Force"
fi

echo "=== $STAGE_NAME ==="
ls -la "$STAGE"
echo "--- zip ---"; ls -la "$ZIP"
