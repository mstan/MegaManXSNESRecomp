Mega Man X 1.6.6 fixes Reset and lets you reopen the launcher mid-game.

- **Reset works again (#45).** Ctrl+R used to leave a black screen that never
  cleared. It now restarts the game from its opening, whether you press it
  on the title screen or in the middle of a stage.
- **Open the launcher mid-game (#46).** Press **Ctrl+L** (or **Select + L3**
  on a controller) and the game pauses while the full launcher opens. Change
  display, sound, controls or hotkeys, then press **RESUME** to continue from
  the same moment. Closing the launcher window also resumes. **QUIT GAME**
  exits.
  - Most settings apply immediately. A few need a restart: the renderer, the
    audio rate, mods, or a different ROM. For those the game saves your exact
    position, restarts itself, and continues from there.
  - Both bindings can be changed on the launcher's Hotkeys page
    (`OpenLauncher`) or in `config.ini` (`[Controller] LauncherGesture`).
- Controller button changes made in the launcher now take effect straight
  away. Before, they only applied the next time you started the game.

Download the Windows ZIP or Linux AppImage below. Supply your own Mega Man X
ROM; no ROM is included. SHA-256 hashes are in `SHA256SUMS.txt`.
