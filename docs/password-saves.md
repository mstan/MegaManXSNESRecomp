# Password saves (experimental, USA Rev 1)

Enable **Password Saves (SRAM)** on the launcher's **Mods** page. The mod
remembers the last password the game displays after a stage or game over.
On a later launch, choose **Password** from the title screen: the saved digits
are filled in automatically. Confirm them normally to continue. You can still
edit the digits yourself; typing a password does not replace the saved one.

The mod's **SRAM file** picker selects an existing password save. Leave it
empty to create and use `mmx-password.srm` beside the executable. This is a
custom PC save format, not emulator SRAM or a save state. It stores only what
the game's twelve-digit password encodes. The previous file is retained as
`mmx-password.srm.bak` when a different password is saved. An unrecognized
file is left untouched and the mod reports the problem in the log.

The text format is a `MMX-PASSWORD 1` header, newline, twelve digits from 1 to
8, and a final newline. A newly created file has a blank second line until
the game displays a password.

## Integration boundaries

- No synthetic SRAM, cartridge mapping changes, ROM patches, or save-state
  format changes. Files are read and written by the host only.
- `$00:EF25` (D = `$1E48`): prefill the twelve entry bytes at `$1E60` once,
  immediately after the original entry-screen initializer copies its defaults.
  This address is reused during gameplay and must never be written there.
- `$00:F05E` (D = `$1E48`): read the twelve displayed digits at `$7E:FFCB`
  when the generated-password screen is ready. Save only when they change.
  The bytes at `$1E60` are encoder scratch here, not displayed digits.
- The original game performs encoding and validation. No guest routine is
  called out of band. Hooks leave CPU registers, flags, cycles and control
  flow untouched; the disabled mod performs no file or RAM operations.
- Interpreter callbacks cover both points. `tools/apply_password_hooks.py`
  inserts matching callbacks into any compiled blocks at these addresses.

This replaces neither the old, failed synthetic-SRAM experiment nor its mod
ID; it is a separate opt-in package, `megaman-x.enhancement.password-save`.

## Focused validation (2026-09-19)

- Windows Release build and `mmx_password_save` test passed. The test checks
  capture without RAM mutation, exact twelve-byte prefill, empty/disabled
  behavior, malformed-file preservation, disk reload and backup behavior.
- Owner's F1 fixture naturally displayed `6676 4727 3184`; the mod created the
  executable-relative file and saved those digits. The 600-frame low-WRAM
  trace and final screen were byte-identical to the v1.5.0 build without this
  mod. Save-state size also remained unchanged.
- A fresh process, without loading any save state, entered Password from the
  title menu, displayed the saved digits and accepted them through the game's
  normal validation, reaching stage select. No broad playthrough was repeated.
