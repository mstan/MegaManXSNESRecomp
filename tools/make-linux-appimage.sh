#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/release-linux}"
BIN="${2:-$ROOT/build-linux-prod/MegaManXSNESRecomp}"
RUNTIME="${APPIMAGE_RUNTIME:-$ROOT/appimage-runtime-x86_64}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/AppDir/usr/bin" "$WORK/AppDir/usr/share/applications" "$WORK/AppDir/usr/share/icons/hicolor/256x256/apps"
cp "$BIN" "$WORK/AppDir/usr/bin/MegaManXSNESRecomp"
cp -R "$(dirname "$BIN")/assets" "$WORK/AppDir/usr/bin/"
cp "$ROOT/config.ini" "$WORK/AppDir/usr/bin/"
cat > "$WORK/AppDir/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH}"
export SDL_JOYSTICK_HIDAPI_STEAM=1
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
ROMDIR="$(dirname "$(readlink -f "${APPIMAGE:-$0}")")"
ROM=""
for f in "$ROMDIR"/*.sfc "$ROMDIR"/*.smc; do [ -e "$f" ] && ROM="$f" && break; done
cd "$ROMDIR" 2>/dev/null || true
[ "$#" -gt 0 ] && exec "$HERE/usr/bin/MegaManXSNESRecomp" "$@"
[ -n "$ROM" ] && exec "$HERE/usr/bin/MegaManXSNESRecomp" "$ROM"
exec "$HERE/usr/bin/MegaManXSNESRecomp"
EOF
chmod +x "$WORK/AppDir/AppRun"
cat > "$WORK/AppDir/usr/share/applications/megamanx.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Mega Man X
Exec=MegaManXSNESRecomp
Icon=megamanx
Categories=Game;
Terminal=false
EOF
cp "$ROOT/assets/MegaManX.icns" "$WORK/AppDir/usr/share/icons/hicolor/256x256/apps/megamanx.icns" 2>/dev/null || true
mkdir -p "$OUT"
mksquashfs "$WORK/AppDir" "$WORK/filesystem.squashfs" -all-root -noappend -quiet
cat "$RUNTIME" "$WORK/filesystem.squashfs" > "$OUT/MegaManX-linux-x86_64.AppImage"
chmod +x "$OUT/MegaManX-linux-x86_64.AppImage"
echo "BUILT: $OUT/MegaManX-linux-x86_64.AppImage"
