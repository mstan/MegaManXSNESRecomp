# Maverick VS

Tracking: `beads-vcvk`. Development branch: `feat/maverick-vs`.

VS is a separate, opt-in Game Modes package (`megaman-x.mode.vs`). Select
**VS Mode** on the title screen after enabling it. It uses the owner's X1
USA ROM and does not require X3 character assets.

Each round starts at character selection. P1 chooses first in round one;
the previous winner chooses first thereafter. Mirror matches are allowed.
Online presentation hides the opponent's selection until both confirm.
Local players share a screen and can see the active picker. A draw awards
no point and retains the previous pick order. Scores last for the session.

Both fighters have 32 HP. P1's meter is on the left and P2's is on the right.
There is no ammunition consumption, healing, Sub Tank use or mid-fight
character exchange. Attacks still have animation commitments and recovery.
Start pauses; the pause menu offers Resume and Main Menu.

Enable **Mods → Game Modes → Maverick VS**, then select **VS Mode** on
the game's title screen. Use two configured local controllers for couch play.
For online play, enable the mod before opening Netplay; each computer uses
its local Player 1 bindings. The current build is an initial playable version
whose move timings and balance are intended for playtest tuning.

Round selection uses X's original pause-menu task and artwork. The original
eight weapon cells contain the boss names and a native sprite preview of the
current choice. The heading identifies **P1 Boss Select** or **P2 Boss Select**.
Buster, Escape, lives and Sub Tanks are removed. Player 1
chooses first in round one; later rounds give the winner first choice.
Checkpoint, door and boss entrance scripts finish in an unpresented batch:
the arena opens with both fighters ready, without the hidden X walk or its
sound effects. Both players start on separate sides; Eagle starts grounded.
X fires the original title selection shot before the selector
opens. Results wait at **PRESS START FOR NEXT ROUND**.

## Controls

Default actions are B Jump, Y Primary, X Special and A Ability. Native
shoot/jump/dash remapping also maps Primary/Jump/Ability; X is independently
bindable in the host Controls page. Directions face the boss and guide its
movement action. Left/Right can start, reverse and stop horizontal movement
after jump takeoff, including during airborne attacks and falls. These bosses
do not acquire synthetic walking animations.
Penguin slides with Special (SNES X, the top face button on the default Xbox
layout), independently of Jump and his ice shot.

| Boss | Movement | Primary | Special | Ability |
| --- | --- | --- | --- | --- |
| Chill Penguin | Directional jump, stomach slide | Ice shot | Slide | Ice statues; Up+Ability requests the handle/blizzard sequence |
| Spark Mandrill | Jump, dash punch, ceiling movement | Electric spark | Dash punch | Ceiling sequence |
| Armored Armadillo | Fast diagonal roll with wall/floor/ceiling ricochets; jump | Energy shot | Roll; press again to cancel | Hold to guard |
| Launch Octopus | Hop/jump | Missiles | Homing fish | Vortex/drain |
| Boomer Kuwanger | Direction+Ability teleport | Boomerang | Horn grab/throw | Teleport |
| Sting Chameleon | Tap Jump for a short jump; hold for height | Stings | Tongue | Hold to cling at the current position; release to fall |
| Storm Eagle | Jump toggles flight/landing; D-pad steers in all four directions | Tornado | One full dive and return | Egg |
| Flame Mammoth | Directional jump; hold Jump for more height | Fire | Oil | Unassigned |

Kuwanger teleports 96 pixels on an axis (68 on each diagonal), clamped to the
arena; releasing the direction after pressing Ability keeps that destination.
Mandrill can traverse the ceiling with Left/Right and release with Ability.
He turns toward movement and holds his still hanging pose when stationary.
Chameleon remains visible and can attack while holding his cling position.
Eagle retains directional flight control while casting; Up+Primary uses his
native wind flap. Special takes off through the ceiling, performs the native
diagonal dive across the opponent, leaves below the screen and returns to
controllable flight. The attack's native path can leave the arena bounds;
normal flight remains bounded. Grounded idle has no wing-flap animation or
sound. Armadillo guards only while Ability is held: standing,
jumping and rolling do not provide passive armor protection. Special again
cancels his roll immediately, including in midair.
Mammoth takes off immediately: tap for a small jump, hold for a higher jump
whose landing briefly stuns grounded opponents. Grabs have a finite escape
bound, and every round clears attacks, captures, effects and damaged armor.

The original combat action dispatches are `$81:B638`, `$88:9D78`,
`$83:B2EA`, `$81:C540`, `$87:8C20`, `$88:8710`, `$87:D9A7` and `$87:9309`
in the roster order above. Native action initialization, animation and owned
children remain guest execution. The VS adapter replaces autonomous choice.

## Attack identity and state ownership

Weaknesses belong to particular abilities, not to all contact from a boss.
Kuwanger's boomerang uses Cutter damage and reactions. His horn grab also
uses Cutter weakness damage against Chameleon, while retaining grab behavior.
Armadillo's roll uses Rolling Shield weakness damage; his energy shot is
ordinary damage. Teleport arrival, Penguin's slide, Mandrill's punch,
Chameleon's tongue, Eagle's dive/eggs and Mammoth's jump use ordinary damage.
Idle body overlap is harmless.

`MmxVsState` owns picks, selection order, scores, input edges, action and hurt
timers, and any suspended target/damage projection. Boss Rush supplies arena,
actor/child ownership, private art, sound and native health-meter rendering;
VS bypasses its shuffled queue, reward drops and endless defeat score.
Mirror fighters have separate ownership generations even when their boss IDs
match. Camera/peer identity is presentation-only.

Boss damage uses a temporary player-projectile proxy through `$84:9E15`.
The source actor retains its identity; native damage rows, immunity and boss
reactions remain responsible for accepted hits. The original proxy storage
and hidden player target are snapshot-owned until the actor returns. New
game snapshots use chunk 25. Old build snapshots are disposable; no migration
is required. Native SRAM is unaffected.

## Development validation

Build `mmx_vs_test` for portable round, all 64 selection combinations, pick
privacy, and ability-class checks. Build `mmx_state_tests` with
`MMX_STATE_TESTS=ON`; run it with `MMX_VS_TEST=1`, `MMX_REPLAY_RECORD=0`,
`SDL_AUDIODRIVER=dummy` and the owner's ROM from an empty private directory.
Keep TEMP/TMP, application-data and cache overrides on F: or G:.

The ROM fixture boots the title entry, runs native entrances, checks snapshot
replay and actual next-round selection, then exercises all 64 ordered pairs
and four action buttons on both seats of each mirror. It writes private BMPs
and diagnostics. These fixtures distinguish input response/entrance coverage
from attack damage, full moveset feel and real two-peer qualification; passing
an entrance or input response alone does not certify those other behaviors.

Windows x64 validation for the initial playtest:

The selector/movement follow-up additionally checks the original menu's
unchanged pixels against an unmodified native pause frame, all eight previews,
both seats' midair start/reverse/stop and Penguin slides, and diagnostic replay
across native selection and the atomic setup batch. Native guest audio clocks
continue during setup; setup SFX are suppressed and only its output PCM is
discarded under the audio lock. The host's normal volume is preserved.

- All 64 native roster pairings and entrances, including mirrors; idle contact
  remains harmless. All assigned action buttons reach native controllers on
  both seats. Directional jumps, fixed teleport, ceiling traversal/release,
  four-direction flight during attacks, Chameleon's jump heights/held cling,
  and Mammoth's two jump heights/quake pass focused checks on both seats.
  Armadillo's roll reaches 8 horizontal pixels per frame, ricochets at arena
  boundaries, deals contact damage and cancels on a second Special press.
  Rolling Armadillo accepts actual opposing energy-shot damage.
  Both Eagles' full native dives leave above/below the screen, hit the
  opponent and return to directional control. Idle ground/landing audio
  checks distinguish silence from retained airborne flap cues. Both Mandrills
  turn during ceiling movement and hold pose `$12` while stationary.
  A sustained Armadillo guard blocks actual opposing shots; release restores
  vulnerability and the native standing animation.
- All eight signature weakness pairings produce 3 HP per accepted hit.
  Kuwanger's horn captures Chameleon, deals 3 HP and throws him. Freeze and
  armor-break reactions recover and accept the next attack.
- A real attack KO awards one point; the winner chooses first next round.
  Selection privacy passes an actual rendered-pixel comparison. Pause exit
  returns through the complete original title opening.
- Combat snapshot replay and rewind restore byte-exact state.
- Two separate LAN installations pass 720 combat frames and a second
  1,800-frame selection/entrance/combat session, including forced rollback
  and resimulation without a digest mismatch. These are same-build Windows
  checks; other operating systems and internet latency are unqualified.
- Eleven stock, renderer, save/password, reset and Boss Rush regression checks
  pass. These checks establish implementation behavior; competitive balance
  and controller feel remain playtest work.
