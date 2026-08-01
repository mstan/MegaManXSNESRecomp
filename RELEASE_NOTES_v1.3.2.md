## What's new

- Fixed the Windows release archive so nested files use portable ZIP paths.
  Linux and Steam Deck extractors now create the bundled `mods/packages`
  hierarchy correctly.
- The launcher can now discover the bundled **Widescreen (16:9)** option when
  the Windows build runs through Proton.
- Added release-packaging validation that rejects Windows-only ZIP entry names.

## Notes

- Windows/Proton zip only. Extract the full archive, then run
  `MegaManXSNESRecomp.exe`.
- A verified Mega Man X (USA) ROM is required and is not included.
