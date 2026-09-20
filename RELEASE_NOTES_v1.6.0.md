# v1.6.0 — Password saves

Mega Man X can now remember its last password between launches.

- Enable **Password Saves (SRAM)** on the launcher's **Mods** page.
- Passwords shown after a stage or game over are saved automatically. Choose
  **Password** from the title screen on a later launch to prefill the saved
  digits, then confirm them normally.
- Use the mod's optional **SRAM file** picker to select an existing save, or
  leave it empty for `mmx-password.srm` beside the executable/AppImage. The
  picker also supports clearing a selection to return to the default file.
- Saves use a custom PC file format with a backup of the previous password.
  The mod does not allocate SNES SRAM, change cartridge memory mapping, or
  change the save-state format. It preserves only progress encoded by the
  original game's password system.
- Keeps the adaptive widescreen renderer from v1.5.0 and updates the shared
  framework and launcher pins to their current main/master commits.

Password saves and widescreen are optional and disabled by default. Existing
player settings and save files are preserved when upgrading.

**Windows:** extract the ZIP and run `MegaManXSNESRecomp.exe`.
**Linux:** make the AppImage executable and launch it.
Supply your own Mega Man X (USA) (Rev 1) ROM; no ROM is included.
