#!/usr/bin/env bash
# Build a ROM-free x86_64 Windows release from macOS/Linux with MinGW.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-local}"
SDL_BACKEND="${SNESRECOMP_SDL_BACKEND:-SDL3}"
case "$SDL_BACKEND" in
  SDL3) SDL_ROOT="${SDL3_MINGW_ROOT:-}"; SDL_LIB="libSDL3.dll.a"; SDL_DLL="SDL3.dll" ;;
  SDL2) SDL_ROOT="${SDL2_MINGW_ROOT:-}"; SDL_LIB="libSDL2.dll.a"; SDL_DLL="SDL2.dll" ;;
  *) echo "SNESRECOMP_SDL_BACKEND must be SDL3 or SDL2" >&2; exit 1 ;;
esac
BUILD="$ROOT/build-windows-mingw"
OUT="$ROOT/release-windows"
STAGE="$OUT/MegaManXSNESRecomp-windows-$VERSION"
ZIP="$OUT/MegaManXSNESRecomp-windows-$VERSION.zip"

command -v x86_64-w64-mingw32-gcc >/dev/null || {
    echo "missing x86_64-w64-mingw32-gcc" >&2; exit 1;
}
[ -n "$SDL_ROOT" ] || {
    echo "set ${SDL_BACKEND}_MINGW_ROOT to an extracted $SDL_BACKEND MinGW development package" >&2; exit 1;
}
[ -f "$SDL_ROOT/x86_64-w64-mingw32/lib/$SDL_LIB" ] || {
    echo "$SDL_BACKEND import library not found below $SDL_ROOT" >&2; exit 1;
}

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_EXE_LINKER_FLAGS=-mwindows \
    -DSNESRECOMP_SDL_BACKEND="$SDL_BACKEND" \
    -DCMAKE_PREFIX_PATH="$SDL_ROOT/x86_64-w64-mingw32"
cmake --build "$BUILD" --target MegaManXSNESRecomp -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

mkdir -p "$STAGE"
cp "$BUILD/MegaManXSNESRecomp.exe" "$STAGE/"
cp -R "$BUILD/assets" "$STAGE/"
cp "$ROOT/config.ini" "$STAGE/"
cp "$ROOT/README.md" "$STAGE/"
cp "$SDL_ROOT/x86_64-w64-mingw32/bin/$SDL_DLL" "$STAGE/"

# Bundle only MinGW runtime DLLs; Windows system DLLs remain supplied by Windows.
for dll in libgcc_s_seh-1.dll libstdc++-6.dll; do
    src="$(dirname "$(x86_64-w64-mingw32-gcc -print-file-name="$dll")")/$dll"
    [ -f "$src" ] && cp "$src" "$STAGE/"
done

rm -f "$ZIP"
(cd "$OUT" && ditto -c -k --sequesterRsrc --keepParent "$(basename "$STAGE")" "$(basename "$ZIP")")
echo "BUILT: $ZIP"
