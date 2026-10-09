# Endless Boss Rush

Boss Rush adds a fourth entry to the USA game's title menu. It starts directly
in solo or co-op according to the launcher's existing X + Zero mod setting.
Co-op uses that mod's owner-supplied X3 assets.
The arena starts with full health, all native X1 weapons, maximum health and
armor, and four full **shared** Sub Tanks; Hadouken is not acquired. There are no automatic refills or
revivals. Two different Mavericks remain present, with each defeated boss
replaced after cleanup. A run ends when its last player dies.
The native player death animation finishes before results appear.

Select **Boss Rush** beneath **Option Mode**. Enable the launcher's existing
co-op mod and supply its X3 ROM to play together. Two native meters on the right
identify each boss by initials; the shared defeat
counter stays at the top. The black **Game Over** page uses native font tiles
with blue/cyan shading, shows the final defeat count and high score, and offers
**Retry** and **Main Menu**. The best score persists locally across runs and
launches in `mmx-boss-rush-score.dat` beside the executable. It is local results
metadata and does not affect gameplay snapshots or rollback.

## Runtime ownership

`MmxBossRushState` owns the deterministic shuffled queue, defeat counter,
encounter phases and boss-owned child objects. Unavailable bosses carry into
the next shuffle; a boss that survives a long time cannot block replacements
or produce a duplicate. Snapshot chunk v18 includes all run state, including
an interrupted actor call. Earlier saves load with Boss Rush inactive.
Renderer captures v18/v19 preserve the solo/co-op run and its HUD.

Native controllers advance once in the existing enemy loop. Entrances retain
their animation and native health-fill states; mode hooks suppress player
scripting and contain scene/camera flags within that actor's call. The native
global victory/death effect is replaced by a local cleanup interval. Each
boss's child actors belong to its encounter; cleanup preserves the other
boss, its children, and both players' projectiles.

The run loads Chill Penguin's last native checkpoint between the boss doors.
Players can prepare weapons there; crossing the second door scrolls into the
enclosed room and starts the encounter and boss music. Door, checkpoint and
camera events remain active during preparation; stock stage enemies are
filtered before allocation. Because Rush bypasses the stock boss's shared
player intro, the encounter requests battle song `$1E` through the native dispatcher
after the door releases; it uploads the SPC bank and supports the existing
MSU1 hook. The call stays inside the guest scheduler so an upload can yield
across frames. Its saved registers live on the guest stack and its pending
return is snapshot-owned, with the existing state layout preserved.
This matches Chill Penguin's health-fill controller at `$81:B617`; `$21`
is the victory song and must not be used for the encounter.
The room retains native scenery,
metatile collision data and its fixed camera. Kuwanger
starts higher to finish his native downward entrance above that floor. Mammoth
uses a fixed arrival position and bypasses the factory's player-distance gate;
his native entrance animation and health-fill states still run.

The title entry uses the native BG3 font and selection palettes. Confirmation
plays the native mini charged shot on the selected fourth row before the fade;
held confirmation buttons are consumed until released so they do not fire
again after arrival. Boss meters
reuse the game's original frame, overlapping energy strips and skull footer;
the counter, initials and results also use native font tiles. Weakness hits
that immediately advance an arriving boss into a hurt/death state still count
the defeat and schedule a replacement.
Graphics resources are decompressed once from the owner's ROM when it is
assigned to the renderer and cached in process memory. Owned bosses render
their own stage's resource, palette and pose DMA remapping privately. Their
native shared OBJ uploads and palette copies are suppressed so cross-stage
actors cannot overwrite X, weapons or another boss. No decoded graphics or
ROM-derived cache files are distributed with the build.

## Disassembly references

The imported names are useful discovery aids, not verified function contracts.
The authoritative class dispatch at `$80:F8DD` identifies these boss bodies:

| Boss | Class | Controller | Native resource stage |
| --- | --- | --- | --- |
| Chill Penguin | `$02` | `$81:B516` | 8 |
| Spark Mandrill | `$31` | `$88:9BE1` | 6 |
| Armored Armadillo | `$14` | `$83:B144` | 3 |
| Launch Octopus | `$07` | `$81:C433` | 1 |
| Boomer Kuwanger | `$05` | `$87:8A7E` | 7 |
| Sting Chameleon | `$0A` | `$88:853E` | 2 |
| Storm Eagle | `$52` | `$87:D85C` | 5 |
| Flame Mammoth | `$0C` | `$87:91A7` | 4 |

The imported Octopus/Kuwanger main labels name related actors. The common
entrance player script is `$84:9FEB`; script release is `$84:A003`; the
already-defeated guard is `$84:AADD`. Native death `$84:A677` starts global
freeze/palette/victory work and cannot run unchanged beside another boss.
The original HUD follows only the object pointer at `$7E:1F0E`.

## Arena authoring tools

- [TeheMan X Editor](https://github.com/Kuumba123/TeheManX_Editor): documents
  X1-X3 support on Windows/Linux/macOS. Its level, enemy, camera-trigger,
  checkpoint and graphics-setting code can help construct a more detailed
  arena and interpret native room data.
- [MegaEdX](https://github.com/Xeeynamo/MegaEdX): another SNES Mega Man X
  level editor and a reference for native stage editing.

Use a private ROM copy for editor experiments. Keep arena modifications
reproducible in source; editor-produced ROMs are not repository artifacts.
Neither editor establishes that native boss controllers can coexist safely.

## Validation

`mmx_boss_rush_test` exercises long replacement runs, duplicate exclusion,
shuffle fairness, deterministic state restore, invalid state rejection and overlay
drawing without a ROM. Build `mmx_state_tests` with `MMX_STATE_TESTS=ON`, then
set `MMX_BOSS_RUSH_TEST=1` and pass the verified USA ROM to run the title-menu,
28-pair entrance, replacement, 600-frame combat and snapshot-replay checks in
an empty private working directory. Add `MMX_BOSS_RUSH_COOP=<private Zero asset
file>` to run those checks in local co-op, including one-player and team deaths.
The same checks verify the native title projectile, second-door controller and
actual boss music bank upload against the command in the original Penguin
controller, then exercise Retry and Main Menu. If an upload
spans frames, the fixture also checks snapshot replay while it is suspended. Portable
checks also cover results-page isolation from death flashes/HUD and high-score
persistence, monotonic records and rejection of malformed files. The compatibility pass holds
player HP full to isolate boss behavior. Each pair must create a new boss
generation after cleanup. Additional checks use native Fire Wave and Homing
Torpedo finishing hits against Penguin and Kuwanger (fixtures start at 1 HP),
and cover entrance-to-hurt/death transitions. These checks do not establish
gameplay balance or long-session stability.

Online uses the existing co-op transport and serialized game state. Per the
owner's direction, this feature relies on the engine's existing netplay
validation; no new two-peer test is required for this change.

Owner playtest priorities: try every native weapon, spend shared Sub Tanks,
fight repeated replacements, and check both players can move and attack while
a replacement enters. Watch boss grabs, rolling/airborne attacks and child
objects near walls. Check pause/menu behavior, boss art and hit feedback, and
death followed by Retry/Main Menu. The flat arena and spawn positions are the
initial layout; balance and presentation can be tuned after that playtest.
