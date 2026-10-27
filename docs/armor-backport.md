# X2 / X3 armor backport (in development)

Central issue: `beads-8wg.1.149`. Worktree branch:
`feature/armor-backports-20261027`, based on released v2.3.6.

One optional feature selects X2 or X3. Only the selected USA donor ROM is
needed; the shared resource keys also serve the existing weapons and Zero
features. Nothing extracted from these ROMs may enter public source or builds.
The local native extractor validates the normalized ROM digest before publishing
its cache atomically.

X starts with the donor's basic ground dash. The actual X1 armor inventory
remains authoritative: helmet bit 1, arms 2, body 4, legs 8 at `$1F99`.
Normal capsules grant the corresponding standard donor part. For X3, Gold is
awarded alongside Hadouken by the original secret capsule reward at `$87:D0DB`;
having four regular pieces or changing the dropdown does not award it.

## Burndown

- [x] Verify owner's X1/X2/X3 USA ROMs and isolate work from the delivered game.
- [x] Native extraction of original body, helmet and three separate armor planes.
- [x] Extract original X3 Gold palette (palette key `$1B8`).
- [x] Match ordinary X1 animation aliases against original donor records.
- [x] Integrate an independent X overlay without replacing Saber Zero's provider.
- [x] Add initial simulation serialization, replay identity and frame-capture state.
- [x] Hook the actual Hadouken reward in generated and interpreted execution.
- [x] Gate Play on the selected donor only (provider component checks).
- [x] Initial original-data busters, barrier, helmet tools, Giga and Hyper Charge.
- [x] Add donor Ride Armor pilot arrangements and charge/weapon palette mapping.
- [ ] Validate every partial armor combination in built solo/co-op/native/LLE views.
- [ ] Match X2/X3 air-dash timing, recovery, dash-jump restrictions and collisions.
- [x] Implement X3 upward dash and its separate body/armor animation groups.
- [x] Implement X2 double charge and X3 cross charge, with donor projectiles/effects.
- [x] Implement X2 Giga Crush, projectile energy conversion and usable meter/input.
- [x] Implement X3 reactive barrier graphics and its damage lifecycle.
- [x] Implement X2 Item Tracer and X3 radar against actual uncollected X1 content.
- [x] Validate Gold recovery, enhanced barrier, Hyper Charge and second air dash.
- [x] Validate actual regular-capsule awards and the Hadouken/Gold reward.
- [x] Validate current-build snapshots/rewind, co-op X + Saber Zero, Boss Rush.
- [ ] Complete death/new-game/native-password and Ride Armor coverage.
- [ ] Complete a two-peer netplay session and launcher GUI check.
- [ ] Prepare a lightweight owner playtest selector with reproducible ability checks.

This is unfinished development work, not a release or completed feature.

## Current validation and remaining risks

The current work remains isolated from v2.3.6. All 16 part combinations and
81 ordinary animation aliases per donor pass the component traversal. The
existing Zero and both portable/native compositor scene suites pass. Built
co-op runs cover all 16 part combinations, input isolation and live Saber
damage, with X on P2 for X2 and P1 for X3. Fifteen exported scene/aspect pairs
(co-op, radar, Gold Hyper Charge and Gold barrier at 4:3, 21:9 and maximum
width) render identically through the portable and native compositors.

Runtime validation found and fixed two introduced defects: dismissing the X3
radar could leak Start into the native pause menu, and the game-chunk loader's
tail-size check omitted the appended armor state. The first save/load comparison
was insufficient: matching native RAM after two invalid loads did not establish
that host projectiles restored. The revised check also requires active host
shots, unchanged radar state and clean loader diagnostics. It must pass on the
current build before handoff. It now passes in solo and both co-op configurations.

Combined validation also caught an introduced collision-table overlap that
disabled Saber Zero. All armor bounds now occupy verified padding at
`$86:FF00..FF2B`, separate from every Saber/Zero/imported-weapon range. A
component check preserves all three Saber ranges, and the built co-op check
requires a real Saber hit to reduce a native enemy's HP (4 to 1).

Actual capsule entry/reward sequences award legs, arms, head and body in
order (inventory 8, 10, 11, 15), leaving Gold off. The native secret-capsule
reward then sets Hadouken and Gold and fills Hyper energy. These tests bypass
travel, the capsule's introductory dialogue and secret access prerequisites;
they do not write the reward flags. Live Gold checks cover its donor Hyper
cost, HP then Sub Tank recovery, second air dash and enhanced barrier after
native contact. Gold survives native stage reloads; transient actions reset
even when the stage number is unchanged.

Giga's built check measures 31 pixels of lift, unchanged world actors during
startup, an attack at tick 146, meter consumption and a native enemy kill.
Mid-cast save/load reproduces an identical picture. Co-op additionally covers
the stored X body and a rewind through the cast. X3 radar restores an identical
picture after closing it and moving elsewhere. Boss Rush with X3/Saber Zero
charges and releases the donor buster and replays players/bosses exactly after
restore. Native grabs correctly pause the charge while its controller is held.

Missing selected donor data now refuses the launch before frame zero. Debug
setup/export/rewind requests are handed to the simulation thread. No owner
acceptance, public release or two-peer netplay qualification is claimed.

The original reference fixtures now enter a regular stage with parts already
owned, so original armor controllers exist. Earlier post-entry inventory-only
fixtures are retained as investigation evidence, not graphics acceptance.

Still needed: broader matched original/built movement and charged-component
paths, complete death/password and Ride Armor checks, launcher GUI and a real
two-peer session. Pilot poses outside the donor's standard Ride Armor table
retain the native arrangement and need a live check. Owner visual/play review
is still pending.

## Controls and adaptations

The existing input mapping still assigns the SNES buttons. Hold L+R and press
Up to use the acquired helmet. X2 marks actual uncollected X1 items for three
seconds. X3 scans on stage entry and opens a map of live X1 collision terrain;
the same combination reopens it. Press and release Jump, Dash, Start or Select
to dismiss it. The map and markers are an adaptation to X1's stages, including
destroyed terrain, rather than imported donor level data. Original X1 block
breaking remains available so its armor-capsule routes remain completable.

L+R+Down uses a full X2 Giga meter, or toggles X3 Gold's Hyper Charge. Giga
requires the body part and no owned shots, spends its meter, lifts X with a
ceiling probe, then releases its native-contact attack. The visible flash
adapts to the window; damaging bounds remain the canonical simulation field.
X1 boss armor/immunity and native positive buster responses remain authoritative;
ordinary enemy damage is normalized using donor basic-buster ratios.

Hyper Charge requires Gold and the arm part. It spends the original donor
weapon cost, including the head/body cost reduction. Gold's energy refills on
the ordinary full-weapon-refill hook and stage entry. Normal password files
remain unchanged; Gold follows the secret-capsule session state, alongside
Hadouken, and is included in current-build snapshots and rollback.

## Source evidence

Addresses are LoROM CPU addresses unless called normalized offsets. ROM IDs:

| Game | Normalized SHA256 |
| --- | --- |
| X2 | `f3246755f608a1e1dc9c848b61da3b824c7853b29b3be40df6fc7f2793a887ed` |
| X3 | `65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7` |

Both donors place the player at RAM `$09D8`, with the original movement and
input fields matching X1's `$0BA8` object layout. Ordinary body arrangements
are groups 0 / `$18` (without/with helmet), and armor overlays `$5D/$5E/$5F`.
X2 has 112 ordinary poses and X3 has 100. The player's DMA already contains
the separate armor tiles. DMA tables: X2 `$85:9609`, X3 `$85:CE45`.
Palette normalized offsets: X2 `$2B100`, X3 `$62400`; X3 Gold `$62580`.

Original animation directories: X2 `$2F:A222` (133 X entries), X3 `$3F:CB64`
(136 X entries, followed by Zero's parallel directory). A first directory
entry is not necessarily the start of the records. Preserve interior aliases.
X3's upward dash uses extended groups `$33/$34` with armor `$35/$36/$37`.

Measured original horizontal air dash speed is `$0375` (8.8) in both games;
it moves for 17 ticks, followed by an eight-tick recovery. The prototype now
uses 17 movement ticks; the recovery and collision QA remain unfinished.
X3 upward dash starts with VY `$0078` and changes speed by animation phase.
An unobstructed original trial at X=1024 has 21 slow and 12 thrust movement
ticks: its `$20` countdown integrates once more before underflow. A constant
`$0375` vertical dash is not equivalent.

Source charge thresholds are 20, 80, 140 and 200 charging ticks. Because the
first held frame fires the pellet, boundary trials need 21/81/141/201 input
frames. X2's lower arm tier fires two half shots; the upper tier fires a full
green shot followed by the pink components. X3's lower arm tier follows its
first shot with a half shot, which cannot create the full cross charge.
A fully charged X2 first shot is class 3/group
`$9E`; its second is class `$1D`/group `$54` with trailing components.
X3 reverses the pattern: the first is class `$1D`/group `$3A` with three
satellites; the following green shot intersects it to create the cross charge
(group `$54`, then a central class `$1F` and four class `$20` fragments).
The cross lobes initially move toward X and accelerate back forward. The
component test checks subtype 3 against original ticks 272..283: VX -1536,
-1440, ... -480, with VY 192. They must not travel forward at constant speed.

X2 pink satellites add four to the parent's tile base, and their three trails
use a 32-entry ring. `$81:8557` reads offsets -16/-24/-32 bytes (2/3/4 frames)
from `$86:B59E`, with static sequences 1/0/0 at `$86:B5A4`. X3 satellites use
three different source palettes; treating all of them as palette 3 is wrong.

Original X2 Giga trials: first 31 ticks rise one pixel, class `$0F` remains
pending until input tick 146, then lasts 120 ticks. X returns to falling around
tick 242. Source damage bounds are `$86:B7CE`; ordinary class `$0F` damage
comes from the neutral row already extracted. Hyper Charge's source cost is
the word at X3 `$86:B364`; `$84:A55A` halves it when both head/body are owned.

X2 `$88:DA15` reduces damage by one (minimum one), then by one more for a
remaining value >=5. It is not X1's rounded half-damage rule. X3 `$84:CE3C`
halves damage, then an active standard barrier subtracts one and is consumed;
an active enhanced barrier subtracts two and remains. Minimum positive damage
is one. X3 barrier actor `$81:868E` has a 300-tick lifetime.

X2 source Giga energy is 8.8 with a `$1C00` cap; projectile IDs differ between
games. The current adapter charges from actual unarmored X1 hostile-projectile
damage, rather than treating an X1 projectile ID as an X2 ID. This is an explicit
adaptation requiring gameplay validation.

X3 head enhancement `$84:A9FC`: standing without input waits 308 ticks, then
recovers one HP every 180 ticks. Once HP is full it fills the first owned,
non-full Sub Tank. X1's corresponding tank bytes are `$1F83..$1F86`, cap `$8E`.

Private original-ROM trials and raw per-frame body/projectile captures live
outside the repository at the task root's `oracle/x2-trials` and `oracle/x3-trials`.
Corrected stage-entry fixtures are in the `-trials-v2` directories, with charge
boundary/palette captures in `x2-charges` / `x3-charges` and utility trials in
`x2-utilities` / `x3-utilities`. None of these ROM-derived files are tracked.
Initial source extraction, existing Zero tests and native renderer tests passed.
Those checks do not establish the unfinished mechanics above.
