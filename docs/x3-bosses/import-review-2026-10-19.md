# X2/X3 Boss Rush import review — 2026-10-19

**Result: changes required; no gameplay signoff or final owner playtest build.**
Reviewed contributor PR128 head `ff6a204a6c5a3698299c936a4ea8fb8ea7549a9d`,
which includes PR123's Tunnel Rhino work. Framework
`0f5708d108bf4acfb21dd7ff75a0cdb2376eb238`, recomp-ui
`0546fbcb0126bb14bf88d2128b4fe8a4229570a3`. Windows x64, MinGW GCC 15.2,
SDL3, Release, freshly regenerated native-analyzer output. Automated launches
used dummy audio output while preserving audio emulation.

Review comments:
[Wire Sponge / PR128](https://github.com/mstan/MegaManXSNESRecomp/pull/128#issuecomment-6261887418),
[Tunnel Rhino / PR123](https://github.com/mstan/MegaManXSNESRecomp/pull/123#issuecomment-6261887935).

## Integration

The existing `megaman-x.mode.boss-rush` package now owns `boss_rush`,
`x2_bosses` and `x3_bosses`. The latter two are default-off toggles for all
implemented bosses from that game, currently **Wire Sponge (1/8)** and
**Tunnel Rhino (1/8)**. Their existing static activation IDs are preserved;
the two standalone package manifests are removed. Source resources retain
the canonical shared X2/X3 keys.

The real provider passes ordinary Boss Rush without either extra ROM, X2
alone, X3 alone, source selection through disabled weapon/Zero features,
shared-path propagation, missing/wrong ROM rejection and clearing a source.
Production shuffle/roster ordering is unchanged. Forced pair ordering exists
only in the test fixture and private snapshots.

## Confirmed failures

| Check | Original source | Imported result | Evidence |
| --- | --- | --- | --- |
| Sponge / normal Sonic Slicer | 2 HP | 3 HP | Native collision and actual fired projectile |
| Sponge / charged Sonic Slicer | 5 HP | 4 HP | Native collision and actual charged projectile |
| Rhino / normal Acid Burst | 3 HP | 3 HP | Passed control case, both methods |
| Rhino / charged Acid Burst | 5 HP | 4 HP | Native collision and actual charged projectile |
| Sponge / Chameleon Sting | No assigned Slicer reaction | 3/4 HP, reaction `$0B/$14` | Native normal/charged contacts |
| Sponge source defense `$0A` / normal Slicer | 2 HP | Reflected, 0 HP | Focused native collision/profile check; live child destruction still unverified |

Original damage directories are X2 `$86:F3A4`, profile `$09` (body) / `$0A`
(child defense), and X3 `$86:E4A5`, profile `$18` (body). Expected values are
read independently from the supplied source ROM by the review fixture.
Sponge is mapped to X1 row 8, actually **Storm Eagle's** row; comments calling
it Penguin's are incorrect. Rhino is mapped to Kuwanger's row 10.

The direct normal/charged source-projectile matrix checks 16 cases per boss:
7 X2 and 8 X3 cases differ. These are focused damage-table contacts, not
complete behavior validation of every weapon/phase. Other-game contacts are
recorded as observations with `expected_raw=-1, passed=-1`, not as passes.
The independent fixture ends with **54 passed and 21 failed assertions**,
including overlapping focused/live reproductions of the same root defects.
It deliberately returns nonzero until these are corrected.

The original character matrix passes all four Sponge cases and Rhino as X,
X3 Zero and Saber Zero. Rhino as Modern Zero fails its timed long-fight spread
coverage check: 13 volleys, no randomly selected spreads, no hand/shoulder
spread samples. Its separately forced spread passes with original offsets.
This establishes a reproducibility/coverage gap, not a proven broken spread.

## Passed runtime checks

- Both relocation descriptors regenerate byte for byte from the verified ROMs.
- Both imported introductions reach combat together in each seat order.
- Six combined-encounter pause cases: native menu entry, frozen bodies and
  projectile pool, X menu pixel equality with the native PPU, unpause, and
  byte-exact 60-frame full snapshot replay after unpause.
- Both combined encounters continue damaging X repeatedly after pauses.
- A real buster hit kills one-HP Rhino; cleanup counts him once and replaces
  his seat. One HP is a setup prerequisite, not a direct zero-HP defeat.
- Natural fatal contact with either imported boss reaches actual Game Over.
- Existing passing character fixtures exercise source art, nonempty source
  SPC effects and requested cues, pin/release and hurt-latch recovery,
  forced spread positions, sustained damage/slot reuse, snapshot replay,
  Sponge's ordinary/Slicer/saber death variants and their split art.
- Clean game/test builds pass. All 27 CTests are accounted for: 25 pass and
  two optional ROM-asset checks initially skip. Both skipped test programs
  subsequently pass with the supplied X3 ROM/generated sidecars (render via
  CTest, wave directly with `MMX_SABER_X3_ROM`). Separate native regressions pass all
  28 distinct X1 boss pairs, pause pixels, state-format migration, audio,
  death-latch recovery, hit visuals and health drops. Their results are
  retained with the private evidence.

Private captures cover native and 21:9 views in both seat orders, including
real margin terrain, boss poses, vines and drills, health meters and low-HP
combat. Contact sheets were visually inspected. A separate forced Sponge
lightning-controller trace covers behavior `$0A`; its anger-entry prerequisite
was bypassed, so this does not establish the natural low-HP transition.

## Failed or unverified visual/fidelity work

Sponge's lightning screen darkening is explicitly dropped by the port:
source `$04:A530/$A560/$A5A2` writes the color-math shadows, but Rush's private
seat globals prevent the effect from reaching the arena. The forced attack
captures retain normal background brightness. A documented multi-boss
lighting policy is needed; this is not a visual-fidelity pass.

Rhino's reported intermittent desperation-charge remnant is **unverified**:
the reviewed sampled captures did not establish that the artifact is fixed.
They are not frame-exhaustive or an original-emulator golden comparison.
Listening parity against original audio, every alternate projectile phase,
live Sponge child destruction, natural Sponge anger entry, and two-peer
netplay remain unverified. Compilation, cache presence and source-code
registration do not establish those outcomes.

## Reproduce and extend

Build with `-DMMX_STATE_TESTS=ON`. Run in a new evidence directory:

```text
python tools/test_boss_rush_imports.py --exe <build>/mmx_state_tests.exe --x1 <X1-ROM> --x2 <X2-ROM> --x3 <X3-ROM> --output <private-evidence> --fixture review --characters x
python tools/test_boss_rush_imports.py --exe <build>/mmx_state_tests.exe --x1 <X1-ROM> --x2 <X2-ROM> --x3 <X3-ROM> --output <another-private-directory> --fixture sponge --characters x zero modern saber
python tools/test_boss_rush_imports.py --exe <build>/mmx_state_tests.exe --x1 <X1-ROM> --x2 <X2-ROM> --x3 <X3-ROM> --output <another-private-directory> --fixture rhino --characters x zero modern saber
```

Retain `stdout.log`, `stderr.log`, the two damage CSVs, `imports-ai.csv`,
captures, PPMs and private snapshots. These outputs contain ROM-derived data
and must stay outside commits and distributable archives. The runner creates
a fresh catalog and sets dummy audio per process. Source ROMs remain private.

For future bosses, import the used source defense profiles instead of guessing
equivalent X1 rows. Keep HP values, reflection/immunity and reaction classes
separate, and account for projectile phases and children. Preserve calling
conventions, profile storage, per-seat scratch/palettes, generation ownership,
cleanup and snapshot state. Force each attack prerequisite systematically;
let real controllers and collisions produce the result. Require exact source
expectations, live normal/charged hits, both seats, another imported boss,
pause/replay and actual defeat/death transitions before marking a boss ready.
