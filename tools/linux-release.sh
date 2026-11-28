# Title metadata and payload requirements for the engine release driver.
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
  "preloaded/packages/megaman-x.enhancement.widescreen/1.0.0/manifest.toml"
  "preloaded/packages/megaman-x.enhancement.msu1/1.0.0/manifest.toml"
  "preloaded/packages/megaman-x.enhancement.password-save/1.0.0/manifest.toml"
  "preloaded/packages/megaman-x.developer.tier2-diagnostics/1.0.0/manifest.toml"
)
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )
EXTRA_PAYLOAD=()

MOD_CATALOG_DIRS=(.)
UNPINNED_CMAKE_OPTION=MMX_ALLOW_UNPINNED_SNESRECOMP
REQUIRED_RASTER=NATIVE
BUNDLE_COMPILER_RUNTIME=1
ISOLATE_LIBRARY_PATH=1
test_game_package() { python3 "$REPO/tools/test_appimage_picker.py" "$APPDIR"; }
