# Mega Man X for Android

[Download the Android APK](https://github.com/mstan/MegaManXSNESRecomp/releases/tag/v2.3.13-android.1) | [Desktop release](https://github.com/mstan/MegaManXSNESRecomp/releases/tag/v2.3.13)

The Android APK accompanies the desktop release. Android 9 or newer, ARM64 or
x86-64, with OpenGL ES 2.0. No ROM is included. Import an unmodified Mega Man X
(USA) (Rev 1) `.sfc` or `.smc`; a 512-byte copier header is accepted. The
importer verifies SHA-256 before atomically replacing the installed copy.

Hunter HQ provides native Home, Settings and Mods pages with touch-sized
controls in portrait and landscape. Fit to screen and adaptive widescreen
are enabled for fresh settings. Explicit aspect and mod choices are retained.
The original X campaign, password saves and optional Boss Rush are available.
The desktop mod catalog, donor-ROM imports, Zero/co-op setup, shaders and online
lobby remain desktop features in this initial Android release.

Move, jump, shoot and dash with simultaneous touch controls or a standard SDL
gamepad. Android Back opens pause, quick save, quick load and return to launcher.
Saves and settings live in private app storage and survive same-key APK updates.
The SDL game uses a separate process so relaunching cannot reuse stale globals.
Crash diagnostics and gameplay replay recording are an independent visible mod,
enabled by default; an explicit opt-out is remembered. Uninstalling removes app
storage, as with other Android applications.

## Build

Use JDK 17, SDK 35, NDK 27.1.12297006, CMake 3.22.1 and Python 3.12 or newer.
Gradle 8.7 and Android Gradle Plugin 8.6.1 are pinned by the wrapper/project.
Initialize the pinned engine and UI submodules. Android uses UI's public C
contracts with its own native mobile launcher; it does not embed desktop ImGui.
Dependencies are pinned and SHA-256 checked in `sources.json`.

Generate the private USA program using the normal host regeneration pipeline,
and build the normal host asset caches before cross-compiling. Set:

- `MMX_GEN_DIR`: generated USA source directory from the matching consumer head.
- `MMX_SABER_SFX_CACHE`: host-generated `cache/mmx-source/saber-sfx-v2.bin`.
  Its directory must also contain `saber-v1.bin` and `ride-zero-v1.bin`.
- Optional `SNESRECOMP_ROOT` and `RECOMP_UI_ROOT`: isolated engine/UI worktrees.

Android consumes these verified host inputs; it never executes an Android
asset-conversion tool on the build host. ROMs, generated guest code, caches,
keystores and player data stay outside Git.

```
python android/tools/stage_content.py
python android/tools/build.py --debug
```

For release signing, create a permanent private JSON file outside Git with
`keystore`, `alias`, `store_password` and `key_password`, then run:

```
python android/tools/build.py --signing path/to/private-signing.json
```

Back up the key privately: future in-place updates require the same certificate.
Builds use two compilation workers and one linker, without elapsed-time limits.
Native libraries and APK packaging support 16 KiB page alignment.

`android/tools/test_game_files.java` exercises ROM integrity/header stripping,
failed-import preservation, atomic replacement, content containment, fresh
settings and saved-choice persistence using the JDK and an optional private ROM.
