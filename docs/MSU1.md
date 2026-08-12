# MSU-1 behavior - attribution and notes

Mega Man X MSU-1 behavior in this build is based on DarkShock's Mega Man X
MSU-1 hack for the USA Rev 1 ROM:

<https://github.com/mlarouche/MegamanX-MSU1>

The active implementation does not patch the ROM. The stock Mega Man X ROM
remains the analysis input and runtime ROM; a trusted Mods plugin observes the
game's SPC music commands and drives the runner's MSU-1 device directly.

## Author Credit

- DarkShock authored the original Mega Man X MSU-1 hack behavior.
- The host-side Mods integration in this repository was written by mstan.

## Behavioral Reference

DarkShock's `mmx_msu1_music.asm` maps game music commands to MSU-1 tracks by
subtracting `$10` from the command byte. The host plugin follows that mapping.

Non-looping tracks are:

- `$00` - Capcom jingle
- `$0F` - title screen
- `$11` - victory jingle
- `$12` - stage selected jingle
- `$17` - got a weapon
- `$1E` - boss tension 1

Special SPC commands handled by the plugin:

- `$F5` resumes music and fades in.
- `$F6` fades out and stops music.
- `$FE` restores full music volume.
- `$FF` ducks music volume for the pause menu.

Missing PCM tracks fall back to the original SPC behavior.
