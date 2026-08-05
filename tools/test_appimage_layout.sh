#!/bin/sh
# test_appimage_layout.sh <AppDir> — prove the packaged layout cannot trap
# user state inside the AppImage.
#
# State policy under test (same as the Windows zip, where everything lives
# next to the exe): config.ini, keybinds.ini, rom.cfg, saves/ and mods/ live
# NEXT TO the .AppImage file. The engine anchors there because host_paths.c
# prefers $APPIMAGE over /proc/self/exe. This script simulates the AppImage
# runtime against a read-only AppDir and asserts:
#   1. first launch seeds config.ini and the release-owned mod catalog beside
#      the (simulated) .AppImage — in a writable dir, not the payload;
#   2. a user edit to config.ini and a user-installed mod survive a relaunch
#      (release refresh repairs its own packages, never user state);
#   3. moving the .AppImage re-anchors state beside the new location;
#   4. nothing is ever written inside the read-only AppDir payload.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/AppDir" >&2
    exit 2
fi

appdir=$(CDPATH= cd -- "$1" && pwd)
exe=$(basename "$(find "$appdir/usr/bin" -maxdepth 1 -type f -name '*SNESRecomp' | head -1)")
[ -n "$exe" ] || { echo "no *SNESRecomp ELF under $appdir/usr/bin" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'chmod -R u+w "$appdir" "$tmp" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM

# Launch helper: simulate the AppImage runtime (APPDIR mount + APPIMAGE path),
# headless SDL, skip the GUI launcher. The game exits nonzero without a ROM;
# that is fine — config seeding and the mods refresh happen before ROM load.
run_apprun() { # simulated_appimage_path
    sim=$1
    mkdir -p "$(dirname "$sim")"
    ( cd "$(dirname "$sim")" && \
      APPIMAGE=$sim \
      SDL_VIDEODRIVER=dummy SDL_VIDEO_DRIVER=dummy \
      SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      SNESRECOMP_NO_LAUNCHER=1 \
      timeout 20 "$appdir/AppRun" >/dev/null 2>&1 ) || true
}

chmod -R a-w "$appdir"

state1=$tmp/state1
run_apprun "$state1/MegaManX.AppImage"

# 1. First launch seeded state beside the simulated .AppImage.
test -f "$state1/config.ini" || { echo "FAIL: config.ini not seeded beside the AppImage" >&2; exit 1; }
test -f "$state1/mods/packages/megaman-x.enhancement.widescreen/1.0.0/manifest.toml" || {
    echo "FAIL: release mod catalog not seeded beside the AppImage" >&2; exit 1; }

# 2. User state survives a relaunch: an edited config line and a
#    user-installed third-party mod package.
printf '\n# user-owned marker\nLinearFiltering = 1\n' >> "$state1/config.ini"
cfg_before=$(cat "$state1/config.ini")
mkdir -p "$state1/mods/packages/user.thirdparty.example/1.0.0"
printf 'user-owned\n' > "$state1/mods/packages/user.thirdparty.example/1.0.0/manifest.toml"
run_apprun "$state1/MegaManX.AppImage"
test "$(cat "$state1/config.ini")" = "$cfg_before" || {
    echo "FAIL: user config.ini edit clobbered by relaunch" >&2; exit 1; }
test "$(cat "$state1/mods/packages/user.thirdparty.example/1.0.0/manifest.toml")" = "user-owned" || {
    echo "FAIL: user-installed mod clobbered by relaunch" >&2; exit 1; }

# 3. Moving the .AppImage re-anchors state beside the new location.
state2=$tmp/state2
run_apprun "$state2/moved.AppImage"
test -f "$state2/config.ini" || { echo "FAIL: moved AppImage did not re-anchor config" >&2; exit 1; }

# 3b. A ROM dropped beside the .AppImage seeds rom.cfg rather than being passed
#     as argv[1]. Passing it positionally skips the GUI launcher, which on this
#     title strands the user with no route to the Mods page where Widescreen
#     lives - and no way back, since the file picker needs zenity/kdialog/yad.
state3=$tmp/state3
mkdir -p "$state3"
: > "$state3/pretend.sfc"
run_apprun "$state3/MegaManX.AppImage"
test -f "$state3/rom.cfg" || {
    echo "FAIL: adjacent ROM did not seed rom.cfg" >&2; exit 1; }
test "$(head -n1 "$state3/rom.cfg")" = "$state3/pretend.sfc" || {
    echo "FAIL: rom.cfg does not point at the adjacent ROM: $(cat "$state3/rom.cfg")" >&2
    exit 1; }
# AppRun must not REPOINT a cache that already resolves. The game itself owns
# rom.cfg and may rewrite it with the same resolved path, so assert the target
# rather than byte-identity.
: > "$state3/other.sfc"
printf '%s\n' "$state3/other.sfc" > "$state3/rom.cfg"
run_apprun "$state3/MegaManX.AppImage"
test "$(head -n1 "$state3/rom.cfg")" = "$state3/other.sfc" || {
    echo "FAIL: AppRun repointed a rom.cfg that already resolved: \
$(head -n1 "$state3/rom.cfg")" >&2; exit 1; }

# 4. The read-only payload stayed pristine: no state files anywhere in AppDir.
for leak in config.ini keybinds.ini rom.cfg saves tier2_coverage.json last_run_report.json; do
    found=$(find "$appdir" -name "$leak" | grep -v '^$' || true)
    [ -z "$found" ] || { echo "FAIL: state leaked into the payload: $found" >&2; exit 1; }
done

echo "AppImage layout test passed: state anchors beside the .AppImage, user files survive relaunch, moved image re-anchors, payload stays read-only"
