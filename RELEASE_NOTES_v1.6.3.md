Mega Man X 1.6.3 fixes two widescreen rendering problems:

- **Boomer Kuwanger's tower:** rear platform pieces now stay behind the tower
  wall instead of drawing over it (#42).
- **Sigma 1, Vile cutscene:** the electricity around X keeps the correct
  graphics after Zero's explosion (#43).

Both fixes were confirmed in playtesting. Automated checks cover both 4:3 and
8:7 display settings, multiple widescreen widths, and the explosion/dialogue
sequence. Game state and widescreen-off output remain unchanged.

This patch also includes the rewind enable, depth, and interval controls on
the Settings page from the current main branch.

Download the Windows ZIP or Linux AppImage below. Supply your own Mega Man X
ROM; no ROM is included. SHA-256 hashes are in `SHA256SUMS.txt`.
