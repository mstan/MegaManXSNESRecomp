# Couch co-op implementation notes

Tracking: `beads-8wg.1.34`. Branch: `feat/couch-coop`, worktree
`F:/Projects/snesrecomp/_wt_mmx_coop`. Base: weapon mods merged through PR #53
at `7e2dcc3`. Scope and owner decisions are in
[the roadmap](zero-weapons-coop-roadmap.md#next-separate-simultaneous-couch-co-op-mod).

## Roster and release boundary

P1 chooses X or Zero; P2 receives the counterpart. The brief fixed P1=X/P2=Zero
answer was withdrawn. Character choice belongs to the co-op settings. SELECT
exchange is disabled. Co-op and the single-player exchange package must be
mutually exclusive. The original X3 ROM path is shared with the Zero/X3 weapon
packages; extraction happens internally. No source ROM or extracted art belongs
in Git or the eventual downloadable mod.

Latest join controls: enabling co-op automatically enrolls P2 and spawns the
counterpart when gameplay has a safe landing. Both players also return at later
stage entries/team restarts. Either player can hold **Select** for 90 gameplay frames (1.5
seconds) to withdraw, retaining HP, weapon energy and selections, provided
their teammate is alive and visible on screen. A withdrawn player stays out
until Select is pressed to rejoin or a new stage/team restart begins. Start
remains pause/menu. Shared subtanks are unchanged, and fallen players cannot
use Select to rejoin until a new stage/team restart.

Symmetric withdrawal (2026-10-03, `beads-8wg.1.79`) transfers the native
world anchor to the remaining seat, then returns it to P1 when P1 rejoins.
Both seats use the existing teleport art and timing, safe landing query,
and inventory preservation. Separate hold/arming counters prevent simultaneous
requests from withdrawing both players. P1's two counters occupy former
reserved menu bytes; struct sizes and existing field offsets are unchanged,
and older saves initialize those counters to zero. They participate in the
normal snapshot/rollback state. ROM checks cover both rosters, both seats,
the full hold interval, dead/dying/offscreen guards, survivor control,
retained HP/energy, and deterministic mid-arrival replay.

This branch provides a **development playtest build**, not an end-to-end
campaign certification. The launcher package is `megaman-x.coop` 0.0.1,
disabled by default. Enable Co-op, choose P1's character, select your
original X3 USA ROM, and assign both controllers in Controls. P2 receives
the other character. The real launcher provider rejects simultaneous co-op
and exchange activation, propagates the shared X3 ROM path, extracts assets
internally, and restores stock behavior after disabling the mod. Both roster
choices pass activation checks. No source assets are distributed.

Current executable: `build-coop/MegaManXSNESRecomp.exe` in this worktree.
Normal launches start through the launcher; owner playtests must never
implicitly load a save. Private fixture tests below use separate directories.

## Source findings (X1 USA, 2026-09-29)

The player controller is `$81:812E`. It saves P and D, sets DP=$0BA8, and begins
its body at `$81:8136`. Its common epilogue is `$81:819C` (PLD, PLP, RTL).
Re-entering at `$81:8136` from that epilogue can run the second player's
controller under the existing guest stack frame. Restore P1's post-controller
registers before the one shared epilogue; preserve elapsed guest cycles.
Do not invent a nested interpreter call without a matching guest return frame.

This runs only the player routine twice. It does **not** run the scheduler,
enemy AI, stage events, DMA, or audio frame twice. Additional ownership passes
are still required for collisions, projectiles, player effects and drawing.
The boundary hooks use the interpreter. `apply_coop_hooks.py` routes generated
entries for this one routine through the existing paired/dispatch bridge when
co-op is enabled, preserving the caller's real JSL frame. Child routines keep
their normal generated implementations. Stock and exchange mode retain their
original path.

| Native location | Ownership and handling |
| --- | --- |
| `$0BA8..0C37` | Player body/controller, 144 bytes; includes current HP, charge and input |
| `$0C38..0C97` | Player armor objects |
| `$0C98..0E17` | Player charge/small effect objects |
| `$0E18..0E67` | Ride armor; stage object, not copied into player context |
| `$0E68..1227` | Enemy pool; world state, not copied |
| `$1228..1427` | Eight 64-byte native player projectiles |
| `$1F83..86` | Four shared subtanks; reserve and unlock bits stay in world RAM |
| `$1F87..96` | Eight X1 weapon energy words; retain shared high unlock bits |
| `$1F99`, `$1F9A` | Shared upgrades and maximum HP |
| `$1F0D`, `$1F12` | Per-player firing command and weapon HUD state |
| `$7E:FFC0..FFC5` | Native configurable action button masks |

In particular, copying eighteen bytes beginning at `$1F85` as “buster plus
weapons” would overwrite two subtanks. Native buster selection has no weapon
energy word. Subtank UI source: `$00:CD16..CD37`; pickup unlock source:
`$81:E643..E65D`.

Native action input conversion is `$00:E543..E5F6`. It copies previous actions
from `$0BDE..DF` to `$0BE0..E1`, maps the buttons through the six configured
masks, and writes new press edges to `$0BE2..E3`. The co-op adapter applies
that mapping to seat 2, preserving native in-game control configuration.
Seat inputs come from `RtlGetPadState`, with the engine's 12-bit button format;
they must not be read directly from SDL. `RtlRunFrame` packs P2 at bit 12.

`$00:D1F3..D206` prepares previous X/Y coordinates and clears the firing
command before entering the player routine. P2 needs the same preparation;
without it, a P1 shot leaves `$1F0D` set and prevents P2 shooting that frame.
The native `$00:D21A` post-controller `$0BD4` clear is also per player.

The second context now runs these native object loops once after P1's pass:

| Pool | Entry | Common return |
| --- | --- | --- |
| Armor | `$00:D2BD` | `$00:D2DD` |
| Player projectiles | `$00:D3DD` | `$00:D3F9` |
| Projectiles while frozen | `$00:D3FA` | `$00:D422` |
| Charge/small effects | `$00:D43A` | `$00:D456` |
| Charge/small effects while frozen | `$00:D457` | `$00:D47F` |

Native `$82:80B4` still determines visibility, but P2 skips its draw-queue
insertion at `$82:80DF`: a second queued pointer to `$0BA8` would display P1
again after projection restores P1. The compositor receives an immutable P2
snapshot instead. Native X sprite arrangements and CHR draw X; extracted X3
poses draw Zero. Co-op suppresses only Zero body/armor CHR transfers at
`$84:8FCB` and returns through the original PLP/RTL, preserving the one X
actor's native dynamic tiles. The renderer keeps original OBJ/BG priorities.

Enemy contact entries are `$84:9B03` (body) and `$84:9B43` (player shots).
They retain the enemy in DP and return through shared RTL boundaries. The
co-op adapter remembers the entry stack/DP, then retries the other player's
context at the balanced return. Projectile scanning retries only after the
native miss at `$84:9B7D`; a hit, immune contact, or reflection at `$84:9EE9`
still consumes that enemy's native first-contact opportunity for the frame.
Body contact checks both players, preserving the caller's successful result
if either overlapped. Enemy AI itself still runs once. Native visibility
`enemy+$0E` must be active: a just-spawned offscreen enemy is intentionally
noninteractive, even when a test moves a player onto it.

Join terrain uses the live collision map already decoded for imported weapons.
At floor dispatch `$84:961C`, `$34..36` and `$3B..3D` call the ordinary solid
handler `$84:96B5` (the highway floor is `$35`). `$33/$3E/$3F` dispatch to
hurt/spike handlers; `$37/$38` are conveyors, and `$39/$3A` are one-way tops.
Joining currently accepts clear ordinary ground/slopes within the native
viewport and conservatively rejects special floors. The failed-password cue
is command `$74`, verified at `$00:F1E4..F1EA`; failed landing uses that cue.
The original morph poses and beam velocities are shared with SELECT exchange.

Player setup requires the native action pointers `+$31=$A597` and
`+$5F=$FA80` plus the constants from `$81:81A3..825A`. Clearing the entire
player tail without restoring those pointers permits movement but disables
firing. New joins clear transient movement/hurt/charge state, preserve personal
inventory on voluntary re-entry, and let native idle initialization resume.

The post-enemy player terrain pass `$81:9D67..9D79` must also run for P2;
it is separate from the movement controller. Camera helpers `$00:DE9D`
(horizontal) and `$00:DEBC` (vertical) retain the original room bounds and
scroll-rate logic, but read the pair's midpoint after each original coordinate
load. Horizontal separation is capped at 224 native pixels. A private check
holds P1 still while P2 reaches the right edge, verifies both remain visible,
then moves both and confirms scrolling resumes. Vertical extremes, forced
scrolling and boss transitions still need further handling.

Native pause entry `$00:9E68` runs against the requesting player's projected
context, including their HP and weapon inventory; subtanks remain shared. `$00:9EAC` rejects
an invalid request; `$00:C579` returns after the menu commits its selection.
The owner remains projected during the menu and is serialized in snapshots.
Input mapping `$00:E543..E57F` runs inside the game scheduler, so P2 input must
replace the native P1 mapping at its return, not only before each frame.
Private checks for both rosters verify that P1 cannot change P2's open menu,
P2 can change imported weapon pages/selection, exit returns both controllers,
and saving/replaying the menu produces an identical full snapshot. Menu
captures were visually reviewed. Subtank consumption/pickups are covered by the later checkpoint below.

The co-op HUD uses fixed columns at native X coordinates 8/24/40/56:
P1 health, P1 weapon, P2 health, P2 weapon. Buster selections leave their
weapon column empty. Native `$00:D82C` / `$00:D94A` supply the overlapping
16-pixel strip placement, partial values and cap position. X keeps X1's badge;
Zero keeps the original X3 badge. Imported weapons use their previously
validated gameplay footers, never pause icons. The native HUD visibility and
optional widescreen edge anchoring remain applicable.

Independent X1 weapon graphics use `$86:98C5`, indexed by `$3E + weapon*2`,
with seven-byte bulk DMA records. Palette directory `$86:8133` is read twice:
list `$40 + weapon*2` with destination offset `$30` for weapon art, and list
`$100 + weapon*2` without an offset for X's body. These privately decoded
resources prevent one player's selection from recoloring the other. In both
rosters, all eight X1 health/weapon meter comparisons matched original
renderer pixels at partial HP/energy. Four-column and buster-gap captures
were visually reviewed, along with an imported weapon pair. Co-op runtime
checks passed with scheduler bounce on/off, plus Zero/renderer regressions.

Native collectible contact `$84:9C0E` retries its original hitbox check for
P2 after P1 misses. Item kinds 1/2/4/5/11 are energy, health, life, Sub Tank and
Heart Tank; other actors in that pool are not treated as pickups. Scheduler
boundaries `$00:D2E6/D308` select the saved collector for an ongoing refill;
`$00:D2ED/D31B` restore the world player after each item. Item movement and
refill tasks still run once. Owner metadata resets when a slot is initialized
or empty, and travels in saves/rollback. Native collision returns at
`$84:9C15/9C1D/9D06` are matched by stack/DP before retrying.

The owner changed subtanks to **shared contents** during this work. `$1F83..86`
now remain in native world RAM and are never projected with a player. Ordinary
HP pickups heal only the collector. Full-health overflow fills the common
tanks; using a tank in either player's menu spends the common contents and
heals that menu's owner. X1's small health pickup stores one native tank unit
at full HP (versus healing two HP when hurt); preserve that original behavior.

Pickup checkpoint: both rosters pass collector-only native HP and imported
weapon-energy collection, with byte-identical save/replay during refill.
Full-health overflow fills shared subtanks without healing the partner. P2's
native pause action consumes the common tank, heals only P2, and returning to
P1 cannot restore spent reserves. These checks pass with generated bounce on
and off. Heart/Sub Tank unlocks retain shared native RAM; collecting those
stage upgrades and native boomerang retrieval still need playtest coverage.

The existing shared SNES launcher profile already allows two players.
MMX's desktop-host descriptor omitted `num_players`, so it advertised one.
USA now advertises two; JP remains unchanged. No recomp-ui fork is needed for
the controller assignment cards.

## Independent deaths and stage ownership

The native controller's death initializer is `$81:8A5C`; it sets global
freeze flags at `$1F13..19`, waits 30 frames at `$81:8A92`, then creates the
original expanding death orbs at `$81:8ADD`. With a living partner, preserve
those shared flags around initialization/countdown and stop the dead actor
at `$81:8B0B`, after the first orb allocation. Mark that seat fallen and clear
its personal attacks. Enemy logic and the survivor continue normally.

The main stage loop at `$00:9AC7` otherwise changes to mode 6 on zero HP.
Mode 6 omits controller polling and pause handling, so suppress that change
when another player is alive. The surviving seat becomes the native world
anchor: enemy/pickup passes, camera, menus and draw submission restore it.
P2 cannot voluntarily withdraw while P1 is fallen, because that would leave
no living player to advance the stage.

Amended 2026-10-07: Select now revives a fallen seat for one spare life
(`$1F80`; the death path spends it at `$80:9B43`, the 1-up adds it at
`$81:E4B3`). `respawn_tick` places him beside the living partner with the
join teleport, at full HP with the buster. The request (`respawn_pending`,
formerly `death_reserved`) waits while the team has no spare life, so a
collected 1-up revives him at once, and while `boss_fight` holds: the boss
health meter pointer `$1F0E` is set, or a boss/miniboss encounter class is
live. Minibosses are listed by class, and that list is incomplete.

When the last survivor dies, retain X1's original stage mode 6, life decrement
and checkpoint. Simultaneous fatalities use one death controller and spend
one life. `$00:9D9E` runs after native actor pools have been cleared: adopt
that cleared body as configured P1 rather than copying an old corpse over
it. Reset personal combat, then let native stage initialization create P1;
enrolled P2 arrives on safe ground with both HP pools full.

Private ROM-backed checks cover either death order and both rosters, continued
movement/shooting/pause, no mid-stage revival, exact survivor-only snapshot
replay, full team restart, and simultaneous fatalities. Generated bounce
on/off both pass the survivor checks; the simultaneous case also passes.
The P2-survivor capture was visually reviewed. These do not yet cover script
ownership at boss doors or a death during a scripted scene.

### Boss damage after a teammate dies

The owner's slot 11 (`save11.sav`) has P1 X fallen and P2 Zero alive, with
Chill Penguin stuck at 29 HP. Penguin's native combat tail at `$81:B6E4..B6ED`
sets its `.30` flag to 1 when `$0BCF & $7F` is zero. This is a permanent
player-dead latch: the shared projectile scan `$84:9B43..9B4F` skips damage
while it is set. It is separate from Penguin's normal `.35` post-hit timer
and damage-row changes at `$81:B63B..B649`.

Previously the co-op world actor changed only after the original 30-frame
death countdown emitted its orbs and marked that seat fallen. Native boss
logic could therefore see zero player HP while the partner remained alive.
World ownership now passes to the living partner at the frame/controller
boundary, and immediately after both native contact passes if contact itself
delivers the fatal hit. The dying actor continues its own controller pass,
death sound and orbs. The last player's death retains the native team restart.

For existing affected saves, a living survivor also clears Penguin's `.30=1`
in its active combat state (kind `$02`, primary state `$04`, positive boss HP).
That flag has no other writer in Penguin's active combat routine. This repair
does not clear its ordinary immunity timer, alter damage rows, or revive a
defeated boss. No save-layout changes or source-ROM modifications are needed.

The private `MMX_COOP_BOSS_SURVIVOR_FIXTURE` regression verifies real damage
from Zero's selected weapon in the reported save, preserves the native
post-hit immunity window, and reconstructs fatal Penguin contact for each
seat. Both cases hand ownership over before the death countdown ends and
finish the native death animation without latching boss immunity. The
existing full co-op suite also passes, including both death orders/rosters,
survivor input and menus, snapshot replay, and one-life team restarts.

## Doors and scene transport

Which player each interaction belongs to (world actor, both players, or
whoever triggers it) is indexed in `docs/coop-interaction-ownership.md`.

The ordinary door contact routines are `$81:E70D` (right-facing) and
`$81:EC98` (left-facing). Try the current world actor first, then the other
living player only after a miss at `$E724` / `$ECC6`. Match the guest stack
and direct-page owner before retrying. A hit at `$E725` / `$ECC7` makes that
seat the world actor, so the retail forced walk, door objects, scrolling,
boss introduction and unlock sequence remain authoritative.

The counterpart uses the original character departure and arrival art/timing.
Only the short teleport animations freeze the world; the native script runs
while the counterpart is hidden. Clear their projectiles and temporary combat
effects, retain HP/inventory, and resume only after the script releases its
body lock and a clear landing is available. Hidden partners cannot collect,
attack, take contact damage or pull the shared camera. Fallen partners never
return from a scene. Other scripts currently follow the world actor (normally
P1); the owner permits this simpler trigger policy for capsules and complex
cutscenes, rather than requiring a second contact implementation everywhere.

`$1F10` is also the boss health HUD state: values 2/4 do not mean the pause
menu is open. Treating all nonzero values as menus prevented the partner from
returning after a boss introduction and stopped imported weapon frame ticks.
The co-op gate now reserves the pause restriction for values >=6.

With generated bounce enabled, the private P2-driven Chill Penguin encounter
exposed an incorrect return from the enemy-projectile loop `$00:D48D`: the
stage resumed at `$80:9B01` with DP=$15E8 and an unbalanced task stack. The
same encounter passes in the interpreter. Co-op now routes this loop through
the existing paired interpreter bridge, as it already does for the duplicated
player/contact boundaries. This change is co-op-only; the shared engine and
stock generated path are unchanged. No synthetic guest return frames are added.

Private door checks drive each seat through both Chill Penguin doors, verify
counterpart departure/return, run subsequent boss combat, compare a snapshot
replay during arrival byte-for-byte, and reject revival of a fallen partner.
They pass with generated bounce on and off. Hidden/returned partner captures
were visually reviewed. Existing controller, weapons, pickup, menu, shared
tank, camera and independent-death checks also still pass in both modes.
A private Storm Eagle capsule approach also checks P1 triggering Dr. Light's
dialogue, the partner leaving, ordinary dialogue advancement, and the partner
returning when control resumes. Capsule interaction follows the current world
actor (normally P1); the second player's touch is not separately retried. This
check covers dialogue only; the full acquisition/demo regression below covers
Chill Penguin's boots capsule. Other capsules and story scenes still need
campaign playtesting.

## State and remaining integration

Validated controller checkpoint: private ROM-backed checks pass with scheduler
bounce both disabled and enabled. Each setting checks both roster orders,
P2 walking while P1 stays still, P1 walking while P2 jumps, one world-counter
advance per frame, and byte-exact full snapshot replay with both input streams.
The existing Zero unit test also passes. These checks do not cover the remaining
systems listed below and do not make co-op ready for playtesting.

Second checkpoint: independent buster creation and movement pass for both
roster orders with scheduler bounce on and off. Private captures were visually
reviewed for walking, jumping, and shots with both characters visible. A render
check verifies P2 contributes sprite pixels for either roster; Zero and custom
renderer unit checks pass. Render captures use MMXC v13 when they contain the
co-op snapshot; ordinary captures retain MMXC v12. Enemy damage and imported
weapon presentation are not covered by this checkpoint.

Third checkpoint: in both roster orders, the real highway enemy takes one
native buster hit from P2; contact hurts P2 alone when P1 is elsewhere and
hurts both when both overlap. Each return restores the caller's original
context. These checks exercise the native enemy routine, not a replacement
damage calculation. Pickup ownership and special scripted enemy reactions
still require integration.

Imported-weapon checkpoint: each player can select/fire a different imported
weapon and spends only their own energy. Both source-art projectile/effect
pools render, including P2 X's source weapon palette and adapted casting poses.
The same native enemy has one damage-fraction ledger across attackers; both
serialized player copies synchronize on projection. Frozen/captured enemies
are excluded from ordinary AI/contact for either player's ownership. Both
charged Crystal Hunter effects age every display frame, but share one
half-speed cadence so staggered effects cannot freeze every alternating frame.
Private checks pass for both rosters with generated bounce on/off, and the
Zero/custom-renderer regression checks pass. The coexistence captures were
visually reviewed. This is not an exhaustive two-player audit of all weapons.

Join checkpoint (updated controls): P2 joins automatically; after withdrawal,
P2 Start does not rejoin and Select performs the original arrival while the
world counter stays frozen. An 89-frame hold leaves P2 present; frame 90
starts the original departure. Withdrawal/rejoin retain HP,
weapon energy and subtank reserves and never heal P1. Mid-arrival save/replay
is byte-identical. Fallen status rejects Select re-entry. A real native P1
death with P2 already fallen consumes one life, runs the checkpoint restart,
and automatically returns enrolled P2 with both HP pools full. Both rosters
pass these checks. Actual one-player death is covered by the later checkpoint above; boss-door
ownership and later-stage arrival placement still need integration.

`MmxCoopPlayer` owns native body/effects/projectiles, per-player weapon energy,
Zero combat/animation state, imported weapon state, and input. Subtank reserves
and unlocks are shared world state, following the owner's later correction.
World progression and unlocks remain in native RAM. Switching the projected
player also updates the existing character collision-table patch.

The game save tail advances to MMXT v14 only when co-op is enabled. Stock,
exchange-only and weapon saves retain their existing v3/v8/v13 layouts. The
tail includes both players and the controller continuation metadata. Legacy
saves reset the co-op context; normal public loading must still enforce the
mod-set compatibility policy. State storage alone does not establish netplay
compatibility.

## Replaying a netplay log

A netplay session's `logs/coop-netplay-*.csv` records the input both seats
consumed on every tick from power-on. The simulation is deterministic, so
those inputs reproduce the session headlessly:

```bash
python -I tools/coop_replay_from_csv.py logs/coop-netplay-<...>.csv bug.replay
MMX_COOP_REPLAY=$PWD/bug.replay MMX_ZERO_TEST_ASSETS=<exe-dir>/cache/mmx-source/x3-zero-v7.bin \
  build/mmx_state_tests <X1 ROM>
```

The replay checks both seats' positions against the log every 30 ticks and
names the first divergence. It assumes P1 = X, X3 behavior and
`Widescreen = 0`; the compositor flag and view width feed culling, so a
session with other settings diverges early.

`tests/data/coop_highway_collapse.replay` (inputs only) is the Highway
collapse after Bee Blader. The road (enemy `$22`) and the falling slab (item
`$08`) carry riders through `$84:AB81`, but only the world actor had a rider
pass: P2 fell through the slab and stood inside it on the lower road, unable
to move. Both now get a second-seat pass. `MMX_COOP_COLLAPSE_TEST=<replay>`
checks the replay follows the recording to the slab, then that P2 lands on
the road beside X and can walk. (That recording predates the drop-frame pass
below, so it is matched only up to its last checkpoint before the drop.)

`tests/data/coop_highway_slab_drop.replay` is a later session at the same
spot. Pressing jump with both seats at once, P2 jumped and X did not. Both
inputs reached the simulation on the same tick: X's press landed on his
native landing frame, which retail MMX ignores (verified in single player),
while P2 had landed on the slab five ticks earlier. The cause was the slab's
one-frame drop state `$82:E62A`: its `$E64E..E666` block reads the world body
directly, moving a grounded body down 2 px and latching it as a rider, so
`$84:AB81` carries it one tick longer. Only X ever ran it; P2 left the road a
tick early and fell a few pixels ahead. `slab_drop_hook` replays the block for
the partner's own body (the routine is listed in `apply_coop_hooks.py` so it
runs on the interpreter under co-op), and keeps each seat's latch as its
`.2C` bit. Both seats now leave the road on the same tick; any later
difference comes from their own positions. `MMX_COOP_SLAB_DROP_TEST=<replay>`
checks both bodies are moved by the drop frame and fall on the same tick.

Other objects that reach `$84:AB81/AB56`, found by walking the generated call
graph from each class's dispatch entry (items `$00:F320`, enemies `$F8DD`,
enemy projectiles `$F77D`), still have no second-seat pass: items `$12`,
enemies `$03` and `$23`, enemy projectile `$17` (none touch `.2C` outside the
helper), and enemy `$2A`, which also keeps its own state in `.2C`. Enemy `$6B`
and enemy projectile `$06` could not be separated from shared code, and item
`$09` is absent from the generated code. These need a decision before they
join `platform_item()`.

## Playtest coverage still needed

The focused milestones above cover both roster orders, native/generated
execution, two-player rendering/combat, collector pickups, shared subtanks,
independent pause inventories, horizontal separation/camera, Select enrollment,
withdrawal/rejoin, death/team restart, boss doors, capsule dialogue and launcher
activation. They are bounded checks, not a complete two-player campaign.

Prioritize these during owner playtests:

- Vertical shafts, moving platforms and forced scrolling. Horizontal separation
  is limited to 224 native pixels and the camera follows the pair's midpoint;
  there is no artificial midair support to stop a player's fall.
- Vile/highway ending, fortress story sequences, left-facing doors and scripted
  deaths. Complex scripts currently use one world actor, while doors retry
  either living player. The single surviving player owns the world after a death.
- Ride armor ownership, boomerang-carried pickups, and overlapping native charged
  effects or moving weapon platforms. These share retail world resources and
  have not received a full pairwise audit.
- Physical controller assignment/hotplug and both players using the real pause
  screen. Port 2 is exposed in the shared launcher; automated checks inject both
  input streams but cannot establish physical-controller behavior.

Future netplay integration is separate. Deterministic snapshots include both
players and scene continuation, but this does not certify online compatibility.

## Owner playtest follow-ups: keyboard seats and pit deaths

The desktop host polled both keyboard maps regardless of the launcher's input
source assignment. With identical default maps, assigning P1 to Gamepad and P2
to Keyboard therefore drove both actors. Shared engine commit `b403ec5` reads
only the keyboard-assigned seats and replaces the entire keyboard word each
poll, also releasing held keys after a source change. Controller presence now
uses those same source assignments. The ROM-backed host harness covers source
changes and P2 joining/withdrawing through the real keyboard polling function.
Default Select is **Right Shift**; hold it for 90 gameplay frames (1.5 seconds) to withdraw.
Older saves with a partially completed 180-frame hold still deserialize.

The missing P2 death was reproduced as a **pit fall**, rather than ordinary
enemy damage. Retail's camera bottom clamp at `$00:E11E..E152` checks only the
projected actor. Once it clamps, `$E12D` compares `(player_y - 32)` to the bottom
edge in scratch `$0000`, then deals a lethal `$7F` hit through `$84:9F2F`.
The co-op hook applies the same signed comparison and fatal-hit state to the
other living actor. The normal controller subsequently supplies the death
pose, `$0A` sound, eight original orb objects, and the existing survivor/team
restart handling. `$00:DE40` is an authoritative interpreter boundary so its
embedded bottom-clamp hook also runs with generated dispatch enabled.

Natural-death regression checks start with live players: either a lethal enemy
contact or an airborne player below the floor. Both seats and both rosters
must reach zero HP, emit one sound and eight orbs, and leave the survivor's
world running without spending a life. Orbs are counted at emission because
retail culls a pit death's offscreen objects before the rendered frame.
These checks pass with generated dispatch enabled and disabled. The existing
co-op checks also pass after the fix, including either survivor's final death,
one-life checkpoint restart, both rosters, menus, pickups and withdrawal.

### Highway falling-platform freeze: reproduced and fixed

The owner reproduced the freeze with P1 X already fallen and P2 Zero walking
right while charging on Highway's collapsing platforms. Both read-only captures
had the first item slot at `$1628`, native item kind `$09`, with the platform's
`.2F` countdown just set to `$1E`. The CPU eventually reached the retail panic
loop at `$80:8097`; repeatedly trying to run that damaged task corrupted more
of its stack. Private captures remain under `_research/owner-softlock/` and
`_research/owner-softlock-2/`; none are distributed.

A bounded regression reconstructs the first captured scene's main task record
and stack from a healthy fixture, then rearms that platform's countdown. The
old generated-dispatch build freezes on the very first frame; interpretation
alone succeeds. This is diagnostic fixture setup, not a runtime recovery patch.
The second capture had accumulated additional corruption and could not serve
as this regression: the same reconstruction failed with either execution mode.

The native path is `$82:E777 -> JSR $E9ED`. Ground contact through `$84:9C0E`
sets the 30-frame countdown and calls positional sound `$80:88A2` at `$82:EA27`.
In the failing mixed execution, returning from that helper also interpreted the
caller's continuation and popped its JSR frame. The outer interpreter then ran
`$82:EA2B` a second time, over-popped, and continued in the wrong bank. Excluding
only the sound helper moved the same failure to effect allocation `$82:EA34`
(`$82:82D3`), so the sound itself was not the cause.

The co-op wrapper previously entered the interpreter before the generated
function's prologue consumed an inherited JMP/JML return context. The next
compiled helper could adopt that stale context as its own. `apply_coop_hooks.py`
now inserts the co-op boundary after the normal prologue, uses its inherited
`_entry_s`/`_hrv`, and balances its host stack entry on return. All 23 existing
co-op boundaries use the corrected placement. Native platform code, physics,
and charge behavior are unchanged; the shared engine needs no new patch.

Set `MMX_COOP_PLATFORM_CAPTURE` to the first private frozen snapshot alongside
the ordinary ROM-backed co-op test variables to run the regression. It verifies
the original 30-frame activation, 60 uninterrupted world ticks, the subsequent
falling phase, and restoration of the task's direct page. An explicit panic
hook fails immediately instead of waiting for the interpreter instruction cap.
The platform regression passes with generated dispatch enabled and disabled.
The full existing co-op suite plus focused keyboard routing and natural
enemy/pit-death checks also pass with generated dispatch enabled. The owner
still needs to play through this area and the rest of the campaign; a bounded
regression does not establish complete stage coverage.

### Stage change after finishing Highway as the surviving P2

The owner's slot 05 had native Chill Penguin stage `$1F7A=8`, but co-op's stored
stage remained Highway (`0`). P1's native body already had 16 HP; its stored
status was still `FALLEN`, causing the empty HUD and stale presentation. P2 was
`ABSENT` from native stage reset and never received the automatic arrival.

`MmxCoopFrameTick` included the stage-ID mismatch in its early return. That
made the subsequent stage-adoption block unreachable for every different
stage, including later checkpoint deaths. Stage changes now request pending
initialization independently of the native readiness guard. Once entry is
ready, the existing block adopts the native P1 body, clears fallen/scene state,
refills the partner, and performs the normal safe-ground arrival.

Stage/checkpoint entry also resets P2's native selection (`body+$33`) and
both players' imported selection/pause page to the X1 buster. Previously the
partner refill cleared charge and projectiles but retained the preceding
stage's weapon (reported with Speed Burner after Penguin). Voluntary Select
withdrawal/rejoin and cutscene transport still preserve selections. The stage
fixture and checkpoint regression check this distinction (`beads-8wg.1.51`).

The private `MMX_COOP_STAGE_FIXTURE` check loads the reported slot, verifies
both full HP pools and identities, P2's completed automatic return, then kills
both actors through the native death controller and requires a one-life
checkpoint restart with both players restored. The fix also repairs the
already-saved slot without editing its file or granting HP during ordinary
play. Save layout is unchanged. The accompanying Zero rescue pose adaptation
is documented in `zero-port.md`; `MMX_COOP_DIALOGUE_FIXTURE` exercises 120
idle dialogue frames and saves a private capture for visual review.

### Capsule acquisition and recorded-input demonstration

The owner's slot 8 (the local `save8.sav`, standing on Chill Penguin's capsule)
reproduced a missed scene handoff. The acquisition routine `$87:CCC9..CD23`
sets `$1F48` and deliberately clears the player body lock `$0C16`. The old
co-op trigger required that body lock, so P2 remained beside the capsule.
After the demonstration, shared-camera separation kept the leading actor
tethered to that old position.

Use the native `$1F48` lifetime to begin scene transport and defer the return.
`$87:CDC5` clears `$1F3B` before the demonstration begins; it is too early to
restore P2. `$87:CDED..CE3F` plays the stage-specific recorded input from
`$87:D36B`, with the player's button mapping temporarily saved at `$7F:F008`.
Only the terminator at `$87:CE02..CE1F` clears `$1F48` and restores that mapping.
The existing scene path now hides P2 throughout, leaves native camera/script
control intact, and finds a safe landing beside the actor's final position.
No timeout, new save fields, or changes to the original capsule script are used.

`MMX_COOP_CAPSULE_FULL_FIXTURE` accepts the private pre-acquisition save. The
bounded check runs the original boots grant and dash demo, requires P2 to stay
withdrawn after `$1F3B` clears, verifies HP and both native/imported weapon
reserves survive transport, then moves both actors and checks camera progress.
Hidden-demo and returned-partner captures were visually reviewed. The source
ROM, owner save, and captures stay private and are not distributed.

### P2 X pose/CHR mismatch (2026-10-01)

The owner's netplay test with host/P1 Zero and P2 X exposed garbled frames on
Highway. This also reproduces offline. The partner compositor uses the current
body's sprite arrangement, while native X CHR changes are queued until the
following NMI. In the reproduction, a pose's first frame reads the preceding
pose's tiles; subsequent frames are correct. Sparse still captures missed it.

Native `$84:8FCA` appends eight-byte records to WRAM `$0500`, with byte length
at `$A3`. NMI `$80:8332..8373` transfers those records and clears the length.
Each record contains VMAIN, VRAM word destination, byte count, source address,
and source bank. The renderer now previews pending contiguous OBJ transfers
(`VMAIN=$80`, VRAM `$6000..7FFF`) in a private raster used only for partner X.
ROM and captured `$7E/$7F` source data are supported. The anchor's latched OAM,
world raster, actual guest VRAM/WRAM, and native DMA timing are unchanged.
The preview is derived anew from each immutable frame, including when loading
an existing renderer capture; it adds no save or rollback state.

`MMX_COOP_X_GRAPHICS_TEST=1` alongside the ordinary co-op fixture variables
runs a bounded 96-frame walk/jump/turn/fire regression. An independent reference
decodes the current X pose's five-byte ROM CHR list rather than the pending
queue. The rendered X region must match on every frame, including 28 deferred
pose transitions. It also checks that drawing leaves native VRAM/WRAM intact
and that a captured transition restores with identical pixels. Before/after
walking and jumping captures were visually inspected. Tracking: `beads-8wg.1.59`.


### Thunder Slimer puddles and Mammoth conveyor returns (2026-10-02)

Tracking: `beads-8wg.1.65`; branch `fix/coop-mandrill-miniboss-camera`, based
on main `0f287a2` after the Zero dash-clearance fix. Hunter HQ remains parked.
These changes apply to the shared offline/netplay simulation.

Thunder Slimer's puddle is enemy projectile `$19`, native routine `$83:A8BD`,
in one of eight `$40`-byte slots at `$1428..1627`. `$83:A93D` calls the native
contact test; the co-op retry correctly found P2 but restored the world actor
before subsequent frames. Capture states `$0C/$0E` at `$83:AA6A/AAB5` then
pinned that world actor instead. `$83:AB38` also writes the victim's position.
The reproduction had Zero touched and X pinned (`body+$2C` bit 3).

Each puddle now retains its actual capture seat. Only the capture-state call
at `$83:A934..A939` projects that seat; the world actor is restored before the
ordinary collision/projectile scan. Native escape input, timeout, movement,
and pop animation still run. A successful first contact cannot also capture
the other actor. A lone survivor also records ownership. The formerly reserved
last word of `MmxCoopState` stores eight P2 bits, preserving its 4,648-byte
layout and including ownership in save/rollback state. Older saves load with
the old P1 default; they cannot reconstruct an already-misassigned victim.
The generated-dispatch wrapper additionally covers `$83:A8BD` (24 boundaries).

Mammoth's final door and scrolling intro completed normally, but partner return
rejected solid conveyor classes `$37/$38`. In the private stage-4 replay,
boss `$0C` reached combat state 4 and cleared `$0C16`, while co-op remained in
hidden scene phase 2 for the rest of the run. `MmxCoopFindLanding` now accepts
these solid conveyors through its existing floor-height, headroom, screen and
enemy-clearance checks. It still rejects spikes and transient/one-way floors.
This permits either X or Zero to return without a timeout or script rewrite.

`MMX_COOP_SLIME_TEST=1` with the ordinary private co-op fixture variables checks
both capture seats and lone survivors, continued pin ownership, and identical
snapshot replay. `MMX_COOP_SCENE_FIXTURE` also accepts the private Mammoth
final-door approach: both possible door drivers must finish the native intro,
return a visibly rendered partner on the conveyor and replay arrival exactly.
The original two-door Penguin regression remains available. Fixtures, ROMs,
extracted art and captures stay private.

A separate Storm Eagle checkpoint-2 replay reached native boss `$52` combat
with both players present. That checkpoint starts beyond the ship lift; it did
not cover the preceding door or lift/destruction sequence (see follow-up below).
The original report of a camera lock after Slimer's defeat remains open:
a private post-intro native defeat sequence restored the authored camera
limits and allowed both actors to leave. The suspected early-kill path was
ruled out for the native buster: Slimer's `$30` immunity stays set until
`$84:B255`, immediately before the intro unlocks the player at `$84:B25D`.
Camera limits are saved at `$84:AECA..AEE2` to `$7F:D384..D38A` and restored
to the camera targets at `$84:B3DD..B3F6`. No speculative camera bounds or
forced unlock patch is included. A capture of the reported stuck state would
allow that remaining condition to be traced directly.


The owner confirmed both the puddle-targeting and Mammoth partner-return fixes
in offline co-op using the prepared slots on 2026-10-02 and approved shipping
them in `2.0.1`. The separate Slimer camera-lock report above remains
unreproduced; that report is not claimed as fixed by this release.

### Storm Eagle platforms, airship door and lift (2026-10-02 follow-up)

Tracking: `beads-8wg.1.67`, branch `fix/coop-moving-platforms`, based on
`main`/`v2.0.1` (`4f385f4`). The owner's UI slots 01/02/03 were copied privately
as `save0.sav`, `save1.sav`, `save2.sav`; originals remain untouched.

Moving platforms are item `$0E` (`$80:F38C -> $83:EFFE`) in the 48-byte item
pool `$1628..1927`. Movement executes once per world tick. `$83:F120` calls
`$84:AB81`, which carries a remembered rider by the platform delta, resolves
solid contact and writes player external-contact flags `$0BD4`. `$83:F129`
calls `$84:AB56` for side contact. These helpers previously saw only the world
anchor: the private replay carried X while Zero fell and died within 50 frames.

Co-op repeats only those two contact helpers for the other living seat. Item
`.2C` retains two rider bits between calls, projected as the native boolean
inside `$84:AB81`. Initialization still clears the latch. CPU continuation and
the world actor are restored after contact; platform movement is never doubled.
The pending pass uses the existing serialized contact fields, including the
formerly reserved byte for rider bits. The 4,648-byte co-op snapshot ABI remains
unchanged. Generated-dispatch boundaries additionally cover `$84:AB81`,
`$84:AB56` and the lift's `$87:C07A` (27 boundaries total).

The airship door is item `$16` at world X `$17E8`. Native leftward door exit
leaves its driver at `$17D8`; the old endpoint-only search chose `$1808`, across
the shut door. Return now checks the terrain corridor between driver and landing.
Scripted returns can also use 16-pixel spacing or overlap the driver on a narrow
ledge; ordinary voluntary joins retain their wider spacing. Existing floor,
headroom, hazard and enemy checks still apply.

The ship lift is enemy `$48`, `$87:C07A`. Its waiting state `$87:C0AE` clears
`.2C`, queries rider contact at `$82:D7D7`, then enters the ascent when `.2C`
is set. It uses player action/body lock `$46`, without the usual boss/capsule
scene flags. Hooks at `$87:C0AE/$C0B4` try either seat's contact, adopt the
actual rider as world anchor, and begin normal co-op scene transport. The native
script retains control through ascent, destruction and Eagle's introduction;
the existing return guards wait for its body lock and intro flags to clear.

`MMX_COOP_EAGLE_FIXTURES=<private directory>` with `MMX_COOP_TEST=1` runs the
three copied saves. Coverage includes both world anchors riding together,
P2 jumping away independently, deterministic snapshot replay with two riders,
and each seat driving the door and full lift sequence. Both HP pools must survive,
both actors must return inside the door, and both must reach Eagle combat above
the destroyed ship. It uses the usual private X3 asset/test-ROM configuration.

### X1 co-op weapon presentation (2026-10-02 follow-up)

Tracking: `beads-8wg.1.68`, same branch. Charged Chameleon Sting's native
projectile `$11` owns its palette phase at `.39`. `$83:9C22` selects the eight
palette lists `$01A0..01AE` through `$82:8011`, advancing every six ticks.
Co-op's fixed weapon-color overlay had erased this sequence. The renderer now
reads the original palette list using each actor's own effect phase. X's armor
also follows its owner's visibility, rather than leaving reconstructed armor
pieces visible during an invisible frame. These are presentation changes;
duration, immunity and blink timing still come from the native controller.

Normal Electric Spark and its two wall fragments are projectile `$0C`, animation
group `$47` (`$83:965B`, split creation at `$83:9771`). Its selection loads two
512-byte CHR blocks into VRAM `$6200/$6300`. The other seat can replace those
shared weapon slots. Co-op draws this projectile's original ROM tiles/palette
privately, including both fragments, rather than relying on whichever weapon
last wrote VRAM. Charged Electric Spark and unrelated effects retain their own
paths. No game memory or projectile mechanics are changed by this repair.

`MMX_COOP_X1_EFFECTS=1` uses the private door fixture with X holding Sting. It
compares 40 charged-Sting frames against the native PPU (including armor and
invisible frames), then checks Spark's split against a reference PPU run while
P2 changes weapons during the split. ROMs, fixtures and captures remain private.

Validation on Windows: both optional ROM-backed suites passed, including a
Spark replay that confirms the underlying PPU graphics really change when P2
switches weapons while the repaired split remains pixel-identical to the retail
reference. The five ordinary CTest checks also pass. This is an offline local
validation with deterministic state replay; a two-machine netplay session has
not yet been repeated for these follow-ups. The owner subsequently requested
integration into `main` and publication as latest `2.0.2-alpha` (not marked prerelease), tracked in
`beads-8wg.1.69`. Implementation commit: `8931edd`.

### Diagnosing intermittent floor clipping (2026-10-03)

Owner report: with X and Zero both on screen, either player occasionally
sinks into the floor by a fixed amount, drops through thin one-way platforms,
misses moving platforms, or becomes stuck inside thick ground. Single-player
does not show it. The cause is not yet reproduced; this section describes
diagnostics only.

Reading the source suggests where to look. Both players share one native body at
`$0BA8`; co-op projects the partner into it for the controller (`$81:8136`)
and replays only selected passes, including the post-enemy terrain pass
`$81:9D67` and platform rider contact `$84:AB81/AB56`. If the body is still
projected when a world pass starts, or a pass stays open beyond its matching
return (`contact_pass`, `pickup_pass`, ...), one seat skips terrain
resolution for that frame. That seat then keeps the controller's fall
distance, which fits a fixed sink depth. The open
pass also disables second-seat platform contact (`platform_hook` returns while
`contact_pass` is set). Nothing currently resets an open pass at frame start.

`--coop-trace` (or `MMX_COOP_TRACE=1`, or `CoopTrace = 1` in `logging.ini`)
writes `logs/mmx-coop-trace-<time>.log` next to the executable, or under the
working directory when that location is read-only. `--coop-trace=all` also
prints one line per frame. The trace only observes: it never writes guest RAM
or co-op state, and it ignores run-ahead speculative frames. An anomaly line
is recorded when:

- a co-op pass is still open at frame start;
- the frame ends with the partner projected, or a world pass begins with it;
- the controller, terrain or platform pass ran for only one living seat;
- a platform rider pass was skipped because another pass was open;
- a seat's feet crossed a flat floor or one-way top, sit at least 6 px inside
  flat solid ground, or dropped more than 12 px in one frame.

Each first anomaly in a two-second window dumps the last 150 frames of both
seats' physics (position, native previous position, velocity, action,
`$0BD3`/`$0BD4`, passes run) and the last 90 frames of hook events. Outside
netplay, up to eight anomalies also save snapshots beside the log: `-after`
plus `-before1/2`, copied from rolling saves taken every two seconds. They need the
same build and mod set. To load one, copy it over a slot file such as
`saves/save1.sav` and load that slot. Slopes are excluded from the terrain checks. Ladder descents through
one-way tops may be reported; check the action value in the dump.

### Opt-in terrain/pickup diagnostics (2026-10-03)

The owner's completed online Chill Penguin trace caught two penetrations:
P2 at guest frame 8306 near X/Y 3786/826, and P1 at frame 12325 near
7009/585, eventually stuck at 7080/703. Both began during the native refill
pause: `$1F13..19=1`, the collector in action `$18`, and the other player
still integrating its airborne velocity against unchanged previous X/Y.
The owner did not notice a health pickup; the trace records HP increments
during these pauses. This is simulation behavior, not a network correction.

Source ROM inspection: `$00:D1F3..D201` does not update previous position
when `$1F19` is set; `$00:D263..D26C` then skips `$81:9D67`, the terrain
resolver. The one-player refill parks the collector at `$81:8B4D` (RTS),
but the second player had no corresponding parked action. Co-op now skips
both movement controllers during this specific refill pause, through their
existing balanced `$81:819C` epilogue. Native item tasks continue running,
so refill completes normally. Imported attack ages are paused too. Death
and scripted scene handling retain their existing paths.

A ROM-backed regression failed before the fix (the airborne player moved
101 pixels down during a 20-frame collision pause), then passed for both
rosters and both collector seats: no movement during refill and ordinary
landing after it ends. Private owner CSVs/ROMs/fixtures remain untracked.

The retained trace confirms the native refill collision pause as the trigger
for these two penetrations. Earlier incidents reported near the stage start
were outside the retained log window. A fresh full-stage playthrough is still
needed to check those incidents; the fix does not relocate actors or change
the underlying terrain resolver.

Tick **Co-op physics diagnostics** under **Mods > Developer** and play
normally, locally or online. The bundled declarative feature defaults off,
like Tier 2 diagnostics, and activates the trusted logging plugin. It creates
unique CSV filenames automatically. Unticking closes and flushes the trace.
No separate launcher or environment variable is needed.

The mod writes unique `logs/coop-physics-*.csv` files. Send the CSV,
its `.previous.csv` companion if present, and the corresponding
`logs/mmx-*.log`, plus approximate stage/location and which player fell.
Each trace keeps at most two 32 MiB segments, with buffered writes.

Rows record the projected actor at controller entry/return, context switches
and active pickup hooks, plus both actors at frame end, including frozen frames. Fields include
host frame and sequence, world tick, stage/camera, HP, input, fixed-point
velocities, previous positions, ground flags, terrain at/above feet, native
freeze bytes, pickup owners/passes, scheduler registers and raw body/scratch
bytes. Netplay rollback can repeat or rewind frame counters; the host sequence
keeps those events distinguishable. Terrain columns are observations, not
automatic assertions that a slope or platform contact is invalid.

Trace buffers/files are host-only, outside WRAM and save/rollback state.
Tracing does not modify physics or force save-state loads. Running sessions
and existing saves/configuration are not changed when producing the build.

A focused ROM-backed flat-floor check covered four cases: either player
collecting a health pickup while the counterpart falls, with both X/Zero
rosters. Both actors survived, only the collector healed, and the airborne
actor landed without penetrating the floor. These isolated checks validate
the refill fix; they do not replace a full Chill Penguin co-op playthrough
or a two-machine netplay session.

### Landing, Storm Eagle supports and weapon-get demonstration (2026-10-03)

Tracking: `beads-8wg.1.82`. X3 Behavior Zero's held buster charge was canceled
by native action `$0A`, the four-frame landing recovery at `$81:8609..865A`.
That action retains native firing/charging and now also retains Zero's charge.
Hurt, death and other combat cancellation paths are unchanged.

Storm Eagle's rising columns (item `$0F`, `$83:F137`) and flying platforms
(item `$10`, `$87:EE82`) also call `$84:AB81`. They were excluded from the
two-seat rider handling added for item `$0E`. They now share its per-seat
`.2C` rider bits and native movement/contact helpers. Flying-platform boarding
still launches through the original `$87:EEE3` state transition.

The original weapon-get demonstration runs from `$00:AB9A`, using the native
player and projectile pools during stage-clear phase `$0A`. Co-op projection
and the previous imported selection could interfere with its scripted actor
and replace its X1 shots. During this presentation, the native task now owns
the pools and firing input. Co-op initializes the roster again on stage entry.

`MMX_COOP_FOLLOWUP_TEST=1` checks a full held-charge jump/landing, both types of
Storm support with Zero alone and both riders, and the real Shotgun Ice demo
after an imported weapon selection. The demo regression observed no native ice
projectiles before the presentation guards, and 78 projectile frames afterwards.
Source ROMs, fixtures, captures and the owner's new PID 54208 logs stay private.

### Storm Eagle platform and capsule reports; trace additions (2026-10-03)

Owner report on 2.0.5-alpha netplay: Zero cannot land on Storm Eagle's
flying platforms or its rising column, and the helmet capsule did not appear
with X alive and Zero fallen. In the retained physics trace (host frames
34718..45398) X boards moving platforms about 40 times; Zero lands twice, both
on a flying platform X already rides. At host frame 34800 X waits on the column
at X 1371 while Zero, 13 px away, falls through its top (feet 897 -> 907
against a top rising 898 -> 893) and lands on the floor below. The co-op
second-seat platform pass runs on those frames (`contact_pass` 1 then 2), so
the remaining question is why the native contact at `$84:AB81` rejects Zero.
This is not yet reproduced against a ROM; no behavior change is included.
Full frame data and hypotheses for the ROM-side investigation are in
`docs/storm-eagle-collision-handoff.md`.

### Dash effect graphics and ownership (2026-10-03)

Tracking: `beads-8wg.1.90`. `$81:9C70` allocates small effect `$0B`;
`$81:F0D7` selects arrangement group `$60`. Its flame and ignition sparks
borrow tiles from the native dash body uploads. Co-op Zero suppresses body
DMA at `$84:8FCB`, while X continues uploading unrelated poses into the same
`$6000/$6100` pages. This made X's body fragments appear behind Zero.

The renderer decodes the original X1 five-byte DMA lists from `$85:A597`
privately: body pose `$37` supplies the startup sparks (effect pose `$04`),
and `$38` supplies the steady flame/sparks. Native arrangements, palette 1,
timing and guest VRAM remain unchanged; no generated artwork is used.

The effect also read the global projected player's position/action. The
allocator hook at `$81:9C86` now records the creator in unused effect parent
bytes `.0C` (seat + 1) and `.0D` (`$D5` marker). `$80:F478` projects that
creator while the original `$81:F0D7` updater runs; `$80:F47C` restores its caller's
seat. Withdrawn/dead owners retire their effects. The previous seat is saved
in `MmxCoopState.effect_return`, replacing an existing reserved byte without
changing the save layout. Ownership lives in WRAM and participates in saves
and rollback. Unmarked effects from older snapshots retain their old binding
until their short native animation expires.

Private ROM checks use `MMX_COOP_DASH_TEST=1` to cover both rosters and world
anchors, effect lifetime, deterministic replay and dash pixels while X's
live body CHR is overwritten. Source ROMs, fixtures and captures stay local.

The Co-op physics diagnostics mod now records what that needs:

- `upgrades`: `$1F99` armor bits on every row (capsules hide once collected).
- `items` on frame-end and platform rows: every live item slot as
  `slot:class:x:y:state0..2:2C:27`, separated by `;`. Platforms are classes
  `$0E..$10/$13/$14`; `.2C` is the rider latch.
- `platform-enter` / `platform-return` rows at `$84:AB81`/`$84:AB56` and their
  returns for any item slot, before co-op switches seats, with the current
  seat's body. `caller` on entry rows is the JSL return address, naming the
  item routine that asked for contact.
- `enemies` on frame-end and contact rows: every live enemy slot as
  `slot:class:x:y:state0..2:2C:hp` (`hp` is `.27`).
- `shots` on frame-end and contact rows: the current seat's live shots
  (`$0C98`, twelve 32-byte slots) in the same `slot:class:x:y:state:2C:27`
  form. Frame-end rows describe the world-anchor seat.
- `contact-enter` / `contact-return` rows at `$84:9B43` (shot contact) for
  each enemy within the widened screen while the current seat has a live shot.
  Rows come from both seats' passes (`current`), with the enemy's whole slot
  in `slot` and the routine's registers. They show whether a seat's shots
  reach a target such as Storm Eagle's flame canisters, and what A returns.

During netplay the mod also writes `logs/coop-netplay-*.csv`, sharing the
physics file's name stem and its two-segment 32 MiB limit. One row per
simulated frame records the host sequence (matching the physics rows),
world/simulation ticks, run-ahead and rollback state, slot/host role,
transport, remote lead, input delay, published inputs, input-desync report,
both seats' inputs, scene state, and FNV-1a hashes of WRAM and the co-op
state. `p1_hash`, `p2_hash` and `shared_hash` split the co-op hash into each
seat's stored copy and the rest of the state, and `p1_body`/`p2_body` hold
the two stored bodies, so a fork in co-op state names its seat and byte.
Rollback re-simulates ticks, so the last row per tick is the kept one;
the first tick whose hashes differ between the two players' files is where
the simulations forked. The file is observation only and is not written
outside netplay.

The subsequent source-ROM reproduction found that the later platforms are
items `$13/$14` (`$83:F27D/$F360`), which were omitted from `platform_hook`.
They share the existing `.2C` boolean rider contract, so they now use the same
per-seat projection and native contact retry as `$0E..$10`. A falling partner
could not board `$13` before the fix; both character arrangements now board
and ride `$0F/$10/$13/$14`. See the handoff's maintainer follow-up for callers,
collision boxes, and the still-unconfirmed helmet capsule report. The focused
regression is `MMX_COOP_STORM_LANDING_TEST=1`.

### Co-op buster and death graphics (2026-10-03)

Tracking: `beads-8wg.1.85`, `beads-8wg.1.88`. The Highway Vile restraint now
uses Zero's original X3 hurt pose rather than interpreting X1 sequence `$49`
as an ordinary X3 attack. See `zero-port.md` for the native state distinction.

Co-op buster shots retain X1 projectiles `$02/$03`, groups `$0E/$9E`.
`$83:89E3/$8C70` select the five-byte pose-DMA directories `$85:AAF3/$AB9A`,
consumed by `$84:8FCA`. Their transfers share VRAM `$6200/$6300`, with different top and
bottom row lengths by pose. One shot's growth/flight upload can replace the
other shot's lower tiles. The compositor now binds each traveling shot to
its own original ROM pose art for either seat, without changing guest DMA,
combat timing, or rollback state. Poses `$01/$06` inherit the preceding
growth transfers `$00/$05`; group `$0E` poses `$01..$03` reuse `$00`, and its
full-charge flight aliases `$11/$12/$14` reuse `$0C`. Palette selection stays
live so native colors and fades remain intact. Shared disappearance poses
`$08..$0B` (group `$0E`) and `$08..$0A` (group `$9E`) retain
their native binding.

Death circles also share X's body pages `$6000/$6100`. Their original literal
DMA list is now decoded separately, preserving native circle art while the
survivor moves. Zero's emitter marker still selects the red palette ramp;
X's particles remain blue. Neither change introduces new artwork or a save
format change. Private ROM-backed checks and captures use
`MMX_COOP_GRAPHICS_FOLLOWUP_TEST=1`; source ROMs and fixtures remain untracked.

Follow-up (2.0.6-alpha recording): the second seat also re-entered
`$84:AB81/AB56` with the first call's return registers rather than the
registers the item routine passed in. It now re-enters with the caller's
registers. This was the trace-only hypothesis before the ROM reproduction
below; it is kept as a defensive correction and is not covered by
`MMX_COOP_STORM_LANDING_TEST`. The physics trace adds a `regs` column and a
faster hex encoder; the netplay lag analysis is in
`docs/storm-eagle-collision-handoff.md`.

### Launch Octopus mid-boss missing after a checkpoint restart (2026-10-05)

Report: in co-op the large mid-boss at x `$111E`, y `$0279` in Launch Octopus's
stage never activated. `coop-physics-20261005-010928-226894-1.csv` (offline,
Unified; 8,341 host frames over three attempts at the same stretch) shows the
enemy (class `$21`, slot 0) allocated in the first attempt (host frame 5677,
camera 4034 scrolling right, P2 not yet joined) and then absent after each
death: the camera resets to 0,0 at host frames 7099 and 9204, the level
restarts at the `4032/287` checkpoint, and the enemy pool stays empty through
every later approach (frames 7200..8300 and 9300..11112). It reappears only
once, at 8696, when the camera sweeps far right (4568) and scrolls back left
over its column.

Cause: not co-op. Both of the stage's `$21` records (`$85:8640` x `$0BCE`,
`$85:86AD` x `$111E`) are kind 3 and `$21` is not in the native-owned list, so
`MmxWidePolicy_SpawnRecordAllowed` gives them to the early wide pass only, and
the native pass rejects them. The wide pass keeps its own host cursor
(`s_ws_spawn_cursor`), which was reset only on a state load or a change of
stage (`$1F7A`). A death restarts the level from its checkpoint in the *same*
stage: the guest rebuilds its event-list cursor, the host cursor keeps its old,
far-advanced value, and every kind-3 record between the checkpoint and the
place the player died is skipped by the wide pass for good, unless the camera
later scrolls back over it (the left-scroll scan walks the cursor backward).
It affects single player too, and any kind-3 enemy past a checkpoint.

Fix: `MmxWidePolicy_WideSpawnCursorPersists` (`$D1/$D2/$D3` = stage, play) is
false in every phase of the stage scene except play (death 6, setup 0,
arrival 2, clear 8/10). The widened cursor is dropped then, both at each wide
scan and at frame end, and re-synchronizes to the guest's cursor on the first
scan back in play. Boss rooms, doors and scripted scenes run inside phase 4, so
the cursor still persists across them, which is what keeps a controller the
wide pass rejected for its native pass. The test is derived from guest RAM
only, so it is deterministic across state loads and rollback.
`tests/mmx_wide_policy_test.c` covers the phases and the re-sync.

Not measured: a replay of the fix in the running game (the build compiles
with the project flags and the policy test passes). To confirm: die to the
mid-boss in the stage, take the checkpoint, and walk to its room; it should
spawn on the way in, solo and in co-op. `SNESRECOMP_WS_SPAWN=0` (authentic
4:3 spawn timing) is the fallback on builds without this change.

### Launch Octopus player interactions

Gulpfer (`$1D`, `$82:A341`) chooses the nearest eligible living player while
free. A player already hidden or frozen inside another fish is ineligible.
During capture, hold, escape and death, the fish retains its victim in the
native-unused `.3F` byte (seat plus one). This keeps release and motion tied
to the captured body even as the other player approaches. Older snapshots
without `.3F` recover ownership from the parked body. This applies in unified
and independent camera modes and survives ordinary snapshots and rollback.

The tall water vortex is effect `$16` (`$81:F493`), separate from enemy `$28`'s
upward currents. Its `$00:D359..D35C` update receives a speculative second
body pass, keeping that body's contact, action and motion while restoring
world state. The real pass advances animation, timers and bubbles once.
Shotgun Ice sled replays now keep BD4 as well as BD3: dropping its newly
written landing flag let the partner ride with a falling animation.

Charged-buster CHR isolation covers native projectile kinds `$01/$02/$03`.
Kind `$01` was omitted from the earlier renderer repair. Group `$0E` pose 6
retains tile `$31` from pose 4/5 because its short bottom-row DMA replaces
only tile `$30`; the isolated ROM asset must retain that same tile.

`MMX_COOP_OCTOPUS_TEST=<private-directory>` runs focused ROM checks using
`save3.sav` (fish) and `save6.sav` (vortex). It covers both character assignments,
returned players, separate captures and release, both world actors, vortex
entry/exit, standing and riding the ice sled, simultaneous native charged
busters, the retained ROM tile, and deterministic snapshot replay. Set
`MMX_COOP_ONLINE_FIXTURE=1` to exercise the fish and vortex in independent
camera paths without opening sockets or a lobby. Synthetic projectile
placement uses the unified view; independent views can cull that imposed
ice-sled fixture before landing. Fixtures and source ROM assets remain private.
