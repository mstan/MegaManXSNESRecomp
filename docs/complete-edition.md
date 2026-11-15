# Complete Edition mods

The USA Rev 1 build includes six independent **Complete Edition** mods. All
start enabled in a fresh Mods profile. Turn any of them off on the launcher's
**Mods** page; saved selections are retained. Continue using your unmodified
Mega Man X USA Rev 1 ROM. No additional ROM or patch selection is needed.

| Mod | Effect |
| --- | --- |
| Retranslated Dialogue | CF and Windii Gitlord's revised story and ending text, with localized character names and the patch's ending credit. |
| Character Dialogue Colours | The original patch's speaker-specific colour controls, with either dialogue script. |
| Text Corrections | Homing Torpedo spelling in the weapon-acquisition text and ending enemy roll. |
| Hoganmer Shieldless Behaviour | A charged Rolling Shield can break Hoganmer's shield and expose its unused shieldless behaviour. |
| Consistent Buster and Hadouken Damage | Two buster damage corrections, plus Hadouken damage against Wolf Sigma. |
| Complete Edition Practice Menu | An in-game menu for armour upgrades, refill, free camera, restart and ghost mode. |

During gameplay, hold **L + X + A** on player one's controller to open the
practice menu. **Up/Down** selects an entry, **Y** sets or activates it, **B**
clears an upgrade or ghost mode, and **Start** returns to gameplay. It includes
helmet, buster, body and dash upgrades, health refill, free scrolling, restart,
and ghost mode. The original developer map editor is excluded from this
integration. Shortcut instructions belong here
and on Mods, with the original title and main menu presentation preserved.

The data mods have disjoint byte ownership. They retain the existing X1
collision routines and therefore compose with the damage multiplier and Easy
Hadouken mods. Imported weapons, characters, armor, co-op, widescreen, password
saves and MSU audio keep their existing implementations. A source-byte guard
rejects another patch overlapping a Complete Edition data span instead of
silently choosing a winner by package order. The debug menu edits shared X1
inventory and the currently projected player body; Zero retains its own body
and animation setup when the menu changes X's upgrade inventory.

The practice menu pauses game logic and consumes its navigation input. Its
selection and held input are included in current-build snapshots and rewind.
All six feature selections participate in the existing netplay mod identity.
The menu uses the resident game lettering and a pause-screen-inspired metal
frame and green field. Its panel stays readable at native and adaptive widths.
No injected debugger program or separately recompiled hack is used. Native
in-game saves and the ROM file are untouched.

## Original release and attribution

[Mega Man X: Complete Edition 1.0](https://www.romhacking.net/hacks/8335/),
released by **CF** on 31 December 2023; the release description and credits
are preserved on [NeoROMhacking](https://neoromhacking.net/hackpage.php?id=8335).

- **CF** — hacking and additional translation work.
- **Windii Gitlord** — retranslated script.
- **PowerPanda** — damage disassembly file.
- **MottZilla** — debug menu help.
- **Maël Hörz** — HxD, used by the original author.

Each package also includes these credits. The original release specifies no
license. Its authors retain their rights; its translation and patch data are
not relicensed under the project's integration-code license. Capcom owns the
original game. The native integration follows this repository's license.

## Reproducing the import and validation

`tools/import_complete_edition.py` verifies both original IPS checksums and the
normalized stock-ROM SHA-256, then emits only dialogue controls/text and the
four gameplay-table records into `src/mmx_complete_data.inc`. Original source
bytes are represented by verification fingerprints, with reversible backups
taken only from the live cartridge. Injected code, debug menu art and ROM
images are excluded.

```powershell
python tools/import_complete_edition.py --archive '<original ZIP>' --rom mmx.sfc
cmake --build build-dev --target MegaManXSNESRecomp mmx_state_tests mmx_complete_data_test
ctest --test-dir build-dev -R 'mmx_complete_(data|runtime)' --output-on-failure
python tests/test_complete_edition.py --archive '<original ZIP>' --rom mmx.sfc --exe build-dev/mmx_complete_data_test.exe
```

The C and Python data checks cover all 32 data-feature combinations against the
original IPS inputs, full reversal, and atomic overlap refusal. The ROM-backed
test exercises launcher defaults and independent selection, both collision
execution tiers, outgoing damage multipliers, Easy Hadouken, real retail boot,
practice controls and drawing, current-build menu snapshot replay,
native Hoganmer shield destruction and AI transition, and both co-op rosters
with the original Zero and X2/X3 weapon packs.
Run it from the CMake-created disposable profile, without native file dialogs.
