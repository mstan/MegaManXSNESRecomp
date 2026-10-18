# X2/X3 Mavericks in Boss Rush — foundation

Goal: all 24 Mavericks (X1, X2, X3) in Boss Rush, then later the full X2 and
X3 stages inside the X1 recomp, with L/R on the stage-select map switching
between the three games' maps so every Maverick can be fought in any order.
This document covers the Boss Rush foundation and the execution plan that
should carry over to full stages. Rhino's source trace:
[tunnel-rhino.md](tunnel-rhino.md).

## Roster (implemented)

- Roster ids: `0..7` X1 (unchanged order), `8..15` X2, `16..23` X3, each game
  ordered by its native weapon id (`kMmxBossRushBosses` in
  `src/mmx_boss_rush.c`). X2 controllers are not traced yet.
- `MmxBossRushRosterState` holds the shuffle queue for up to 24 bosses and
  the run's **available** set. The available set is copied from the offered
  set when a run starts, so launcher edits cannot change a run in progress
  or make netplay peers diverge.
- The original `MmxBossRushState` layout is unchanged; its legacy 8-entry
  queue stays empty for new runs. Older snapshots and render captures load
  as before.
- Game snapshot chunk **v23** appends the roster after the v22 view bounds.
  Loading v18–v22 imports the legacy X1 queue as an X1-only roster. A loaded
  run must contain both present bosses in its roster.
- HUD initials exist for all 24 bosses.

## Packs and source ROMs (implemented)

- Package `megaman-x.boss-rush.x3-bosses` (Game Modes, off by default) asks
  for the X3 USA ROM using the shared `megaman-x.source.x3` selection.
  An X2 package uses the same plugin (`megaman-x.boss-rush.x2-bosses`) and
  will be added with the first X2 boss.
- `src/mods/mmx_boss_rush_foreign_plugin.c` validates the ROM with the same
  checksum rules as the other packs (`MmxSourceRomLoad`) and keeps the image
  **in memory only**.
- `src/mmx_boss_rush_foreign.c` offers a boss to new runs only when its pack
  is loaded **and** its body is implemented. No X2/X3 body exists yet, so
  runs stay X1-only even with the pack enabled.
- Netplay refuses to start when an enabled Maverick pack failed to load.

## Running X2/X3 boss code (next)

X1 and X3 share one engine generation. Every helper Rhino calls has a direct
X1 counterpart with the same object layout:

| X3 | X1 | Purpose |
| --- | --- | --- |
| `$04:B967` | `$84:8F07` | start animation |
| `$04:B94A` | `$84:8EEA` | animate and draw |
| `$04:C0F7` | `$84:91BE` | terrain collision |
| `$04:CB74` | `$84:9B43` | take player-shot damage |
| `$04:CB31` | `$84:9B03` | contact damage |
| `$04:BE35` | `$84:9086` | RNG |
| `$02:D636` | `$82:80B4` | off-screen cull |
| `$02:D8DE` | `$82:8358` | allocate projectile slot |

Two ways to bring a Maverick over:

1. **Relocated original code (recommended).** At activation, build an
   extension bank from the user's ROM: copy the boss controller, children
   and data tables, and rewrite JSL/JML targets, RAM addresses and data-bank
   references through a per-boss, address-only relocation table. The X1
   interpreter then runs the boss natively. Damage, weapon weaknesses, co-op,
   snapshots, rollback and netplay keep using X1's own paths. Per-boss work
   is a relocation table plus shims for X3-only helpers, instead of a full
   rewrite. The same machinery is what full X2/X3 stages would need for
   hundreds of enemies, so it is the only approach that scales to that goal.
2. **Hand-written C bodies.** No cartridge changes, but each boss's AI,
   physics and damage handling is reimplemented, roughly 2–4 KB of 65816 per
   boss, with drift risk and duplicated engine logic.

## Feasibility spike (passed, 2026-10-17)

Approach (1) is viable. `src/mmx_cart_extension.c` and the Rush dispatch hook
implement it; `MMX_BOSS_RUSH_FOREIGN_SPIKE_TEST=1` with `MMX_BOSS_RUSH_TEST=1`
checks it against the USA ROM.

- **Cart extension.** `snes_loadRom` pads X1's 1.5 MB image to 2 MB, and
  LoROM banks `$C0`-`$FF` map to image offsets 2-4 MB (`bank & $7F`), which
  X1 never reads. `MmxCartExtensionEnsure` grows the image to 4 MB with the
  upper half mirroring the original 2 MB, so every existing read is
  unchanged, and replaces both `cart->rom` and `g_rom`. `romImageSize`, and
  therefore the image identity hash, is unchanged. Writes are limited to
  banks `$F0`-`$FD`, `$8000`-`$FFFF`, and invalidate the interpreter's
  decode cache.
- **Dispatch.** X1's enemy loop (`$80:D4EA`/`$D507`) already runs in the
  interpreter while Rush is active, so a pre-opcode hook at the class
  dispatch `$80:D544` (`JMP ($F8DD,x)`) fires without regenerating code.
  For a seat whose roster id has a registered controller, the hook pushes a
  JSL frame returning to an existing native `RTS` (`$80:F8C3`) and redirects
  into the extension bank, which is exactly what a stock `JSL; RTS` stub does.
- **Measured:** a bank-`$F0` controller (`INC $3E; JSL $84:9086; STA $3F;
  RTL`) for roster id 23 in seat 1 ran exactly once per frame for 60 frames.
  Its native RNG call returned into the extension bank every frame. A
  native Penguin kept running in seat 0. A 30-frame snapshot replay
  matched all 128 KB of WRAM byte for byte, and clearing restored the
  mirrored bank. The full 28-pair Rush suite still passes.
- **Not yet covered:** netplay between two peers with an extended image
  (both peers build identical bytes from the same verified ROMs, and the
  image identity is unchanged, but this is untested); renderer captures of
  foreign bodies; any real relocated X3 code.

## Tunnel Rhino relocation (running, 2026-10-17)

Rhino's original X3 code runs inside X1's engine. `tools/relocate_boss.py`
traces his body, his drill projectile and the X3 helpers they need, then
writes `src/mmx_boss_rush_x3_rhino.inc`: 1,326 traced instructions and 183
rewritten operands, as addresses plus the original operand values.
`src/mmx_boss_rush_relocate.c` rebuilds him from the user's X3 ROM when the
pack loads.

**Layout.** X1 uses data bank `$86` for its enemy loop, the same convention
as X3, and X1's damage, terrain and overlap helpers read hitboxes through
that bank. Rhino therefore runs with X1's `$86`:

| Where | What |
| --- | --- |
| `$3F` (X3 `$3F`, same canonical bank) | Rhino's controller, X3 animation sets/records |
| `$33`, `$38`, `$34`, `$32` (X3 `$13`, `$08`, `$04`, `$02`) | palette helpers, drill class `$26`, X3 animation/motion/overlap helpers |
| `$86:FA80`-`$FAFF` | shims (`RTL`, finished fade, resume combat, delete, class init) |
| `$86:FB00`-`$FBB2` | his `$86` tables: body/drill hitboxes, drill spawn tables, angle table |
| `$86:FD40`-`$FF3F` | zero/sink area for X3-only globals (`$86:FD40` + low 9 bits) |
| `$86:FF40`-`$FFFF` | not ours: air saber `$FF40`, weapon boxes `$FF80`, saber wave `$FFA0`, Zero bounds `$FFB0`, ground saber `$FFD8` |

All copied banks sit in X1's unused `$30`-`$3F` padding; the `$86` tail is
`$FF` padding in the USA ROM. Clearing restores both. The patch window
stops at `$86:FF3F`. Overlapping that tail broke three things: the saber
disables a collision window it does not recognize (ground slash did 0, then
the air/dash slash lost its hitbox), and the weapon code's `$FF` fill
reached Rhino's X3 globals as X (he froze after one frame).

**X3 globals that are read back** need real storage instead of the sink:
`$1F57` (his profile, restored by the flash timer `$02:DF64` when a hit's
invulnerable flash ends) maps to X1 `$1FD5`, unused by X1 and swapped per
Rush seat with the other boss globals. In the sink it read 0, X1's deflect
row, so after his first flash nothing could hurt him again. `$1F54`-`$1F56`
are only read by his drills to delete themselves when he dies, which Rush
already does, and stay in the sink.

**Room midline.** X3 boss rooms start on 256-pixel boundaries, so Rhino
tests bit 7 of his X low byte for which half of the room he is in
(`LDA $05`/`EOR $05` at `$3F:E8C1`, `$E8C7`, `$EB30`, `$EB50`). His
stop-and-fire charge skips the wall check and stops at that midline, so in
the Rush arena he slid through the right wall. Those four reads are hooked
and answered relative to the arena's center.

**Delete shim.** X3's delete (`$02:D928`, same as X1 `$82:8398`) clears the
object's first words. The shim used to clear only byte 0, and X1's slot
allocator tests the first word, so each dead drill kept its projectile slot:
after two volleys he could not spawn drills, his live-drill count `$3D`
stayed at 3, and he stopped firing volleys and the stop-and-fire spread.

**Shared projectile pool.** Rhino sets his live-drill count before his
volley spawns and after his spread loop, assuming every spawn succeeds. In
Rush he shares X1's eight enemy-projectile slots with the other boss
(Penguin's ice shots and statues can fill them all). Spawn guards take
unspawned drills back out of `$3D`: after a failed allocation in the volley
(`$3F:EC08`) and at the spread's count store (`$EA85`, minus the `$3A`
drills its loop could not spawn).

**Pin and caught state.** X3 has a caught player state (`$12`) that X1 lacks;
it maps to X1's ordinary hurt `$0E`. Child guards tie his catch code to his
pin carrier (drill variant 2): the pin test (`$3F:EB78`) counts the player as
caught only while his carrier is alive, so an ordinary hit during his recoil
no longer locks the player; and the drill charge spawns a carrier
(`JSR $EBE1` at `$E994`) only when none is alive. X3's caught player stops
overlapping him; X1's does not, so he spawned one carrier per frame of
contact, filling the projectile pool (the leftover drill graphics).

**Hurt latches after a lock.** A lock taken while the player is hurt skips
the end of X1's hurt state, which starts the 60-frame countdown (`$0C13`)
that clears the hurt latches `$0BD8`/`$0C32`. The release shim starts that
countdown when it frees a latched player; otherwise the player could never be
hurt again (found by replaying the playtest capture with `MMX_REPLAY_WATCH`).

**Spread drill offsets.** The spread drills read their start offsets from
`$86:EFE7` + 4 × index (index 0-3, up to `$EFF6`); the copied table used to
end at `$EFEC`, so the shoulder drills and the left-facing hand drill read
the angle table instead and started in the wrong places (or off screen,
deleting themselves). The copy now covers `$86:EFB8`-`$EFF6`.

**Classification** (anything else fails generation):

- *X1 helpers:* cull `$82:80B4`, terrain `$84:91BE`, RNG `$84:9086`, shot
  damage `$84:9B43`, contact `$84:9B03`, projectile slot `$82:8358`, face
  player `$84:ACA5`.
- *Relocated X3 helpers:* animation start/advance (`$04:B967`/`$B94A`, so he
  reads X3's own animation sets), velocity (`$02:D7F3`/`$D78E`/`$D6F8`),
  hitbox overlap `$04:CC5C`, flash timer `$02:DF64`, player-death guard
  `$04:D8D5`, his palette helpers `$13:F7D6`/`$F7FC`.
- *Shims:* music, SPC upload, sound effects, intro/pin player lock, screen
  shake, dust, stock explosion and weapon award, stun, Doppler graphics; the
  generic hurt reaction resumes combat; deletes clear the object; class
  init writes his sprite set, HP, contact damage and defense profile from
  the ROM's class row.
- *RAM:* X3's `$09xx`/`$0Axx` player block is X1's `+$1D0`; camera
  `$1E5D/$1E60`→`$1E4D/$1E50`; HUD boss `$1F1F`→`$1F0E`; the player object
  immediate `$09D8`→`$0BA8`. X3-only flags read zero and ignore writes:
  stage/defeated/music/last-weapon checks, state mirrors for children,
  OBJ palette buffer `$0480`-`$04BF`, palette backup `$7F:CAC0`.

**Dispatch.** Hooks at X1's enemy (`$80:D544`) and enemy-projectile
(`$80:D4E7`) class dispatches enter registered controllers for objects owned
by a foreign seat. Rush treats a foreign body as fighting at state `$01=2`
and dying at `$01=4`; defeat and cleanup use the normal Rush path.

**Measured** (`MMX_BOSS_RUSH_RHINO_TEST=1` with `MMX_BOSS_RUSH_TEST=1` and
`MMX_BOSS_RUSH_X3_ROM`): his intro drops in and fills his meter to 32, then
he chooses among his original charge, drill volley and idle behaviors,
charges wall to wall at 4 px/frame, and fires all three volley drills as
relocated projectiles. A native Penguin fights beside him. Replay is
byte-exact, Rush counts his defeat, and cleanup removes him and his drills.
Unloading the pack removes him.

**Art (done).** `src/mmx_boss_rush_foreign_art.c` decodes his compressed
OBJ resource (`$1D`, record `$86:F732`), his palette (list `$3E` at
`$86:8180`, 16 colors from `$8C:E628`) and rasterizes poses from the X3
layout table (`$8D:8000` + sprite set) once, from the in-memory ROM. The
renderer drops the pieces X1's sprite builder makes for foreign-owned
objects (X1 tables cannot describe an X3 set) and draws each foreign body
and projectile whole from its object's `+$16` set and `+$17` pose: bit 6 of
`+$11` faces right, bits 4-5 give OBJ priority, and palette 0 in bits 1-3 is
the native hit flash on live CGRAM. Foreign sprites draw before native
pieces, so players and their shots stay in front. The Rhino fixture checks
that his body and drills draw in his own palette and saves private
`rush-rhino-combat.ppm` / `rush-rhino-drills.ppm` captures.

X1's enemy-projectile actor (`$80:D4D3`/`$D4DD`, dispatch `$80:D4E7`) has a
compiled body, so `tools/apply_boss_rush_hooks.py` now routes it through the
interpreter during Rush; without that, X1's own projectile class `$26` ran
his drills.

**Sound (done).** X3's SPC driver uses X1's upload table format
(`$09:8000`, `[length, destination, data]` blocks) and boot protocol, so
the isolated Rush SPC renderer loads X3 banks 0-2 plus the boss's own bank
(`$5D`: 10 bytes at `$1952`, 586 at `$3400`) and pre-renders his effects
when the pack builds. The relocation tool finds the bank from his
`LDA #$5D; JSL $00:850F` and his effects from every `LDA #fx` before
`$01:802B`/`$01:8000`: `$15` meter fill, `$21` weakness hit, `$49` wall
crash, `$70` spread, `$72` mid-charge stop, `$73` drill grind. Those two
calls become separate shims (`$86:FABE` plain, `$86:FABF` positional) that
the runtime hooks to queue his cached clip; the positional one pans by
half his distance from the player, as X3's routine does. Cues keep the
existing snapshot layout and name the roster id in their spare field.
X1's `$1C00` signature and `$C3` activity byte are not used for X3; an X3
clip ends after 4,096 silent samples. `MMX_BOSS_RUSH_RHINO_WAV=1` writes
private listening copies of the rendered clips.

**Weakness (done: his original X3 weakness only).** X1's damage helper
indexes `$86:EF37` by the target's profile (`+$28`) and the shot's class;
rows 6-13 are the Mavericks, row 0 deflects every shot and row 5 takes no
damage. The relocation maps Rhino's X3 profiles to X1 rows at each
`LDA #x; STA $28` and in his class-init shim: normal `$18` → row 10
(Kuwanger's), drill charge `$0D` → row 0 (deflect, body and drills), flinch
`$01` → row 5. X3's last-hit class `$1F2F` is X1's `$1F1D`, and the compare
chain after it (Acid Burst parts `$07/$10/$18/$21`) becomes Torpedo's
reaction classes `$0E/$17`.

Rush hooks the damage helper at `$84:9E1D` (damage column) and `$84:9E3A`
(reaction class) for relocated bodies: X3 Acid Burst takes row 10's
weakness column (3 normal, 4 charged) and reports the Torpedo reaction, so
his own code flinches; X1 Homing Torpedo is moved to an ordinary column so
the borrowed row adds no weakness. The X2/X3 weapon pack no longer applies
its X1-class table to relocated bodies (Rhino keeps Penguin's class byte
for range safety) and always defers to their native row. Rules live per
boss in `src/mmx_boss_rush_relocate.c` (`MmxBossRushWeakness`, keyed by
page/weapon from `MmxWeaponsShotIdentity`).

Measured with a real Acid Burst from the X3 pack built from the same ROM:
buster 1, Homing Torpedo 1 (no flinch), Acid Burst 3 with flinch into the
invulnerable row, drill-charge row deflects.

**Weakness plan (owner, 2026-10-17).** Once all 24 bosses exist, every boss
gets three weaknesses: its original one plus one from each other game. This
also applies to mini-bosses that have a weakness in X1-X3. The owner will
assign the extra weapons for all 24; until then relocated bosses keep only
their original weakness. Weak points (Flame Mammoth's trunk, Launch
Octopus's arms with Boomerang Cutter) will extend to X2/X3 weapons of a
similar nature. A later optional mod will add configurable weakness-hit
cues: Mega Man ZX weak-point graphic, sound and red health-bar flash
together, only the ZX sound, only the ZX graphic, or the enemy flashing red
instead of white.

**Pinning (done).** X1 uses X3's player-state numbers: hurt `$0E`, death
`$0C` and the scripted lock `$1E` (X1's entrance lock `$84:9FEB` sets `$1E`,
`$84:A003` releases). X3's lock/release (`$04:D1CA`/`$D1EF`) become shims:
lock a living player through `$84:9FEB`, release through `$84:A003` only
while the player is still in `$1E`; Rush already skips that lock during a
boss's entrance. X3's "caught" state `$12` has no X1 counterpart; the drill
hit leaves X1's player hurt, so the pin test compares `$0E`. Together with
the relocated carrier, which drags X, his drill charge catches, carries,
locks and releases X as in X3. Caveat: any hurt state during his drill
recoil (not only his drill) counts as caught. `$74` (player state, also
tested by X3 contact code) is unidentified and kept.

**Drill-charge palette (done).** His X3 helpers (`$33:F7D6` rotate colors
1-15, `$33:F7FC` fade back) are hooked and run on the seat's private
colors (`MmxBossRushVisualRotate` / `MmxBossRushVisualFade`, the state
Chameleon's camouflage already snapshots); the fade returns carry while
colors still differ, and the renderer draws foreign bodies and their
projectiles with those colors.

**Frame counters (verified).** X3 increments `$09CB` at one NMI site and
`$09CC` at six scene-loop sites; X1 does the same with `$0B9B` and `$0B9C`,
so the `+$1D0` mapping holds.

**Unlocked (2026-10-17).** Rhino is offered to normal Boss Rush runs once
the X3 Mavericks pack has loaded the X3 ROM; `MMX_BOSS_RUSH_PREVIEW` is no
longer needed for him.

**Every playable character.** The launcher-driven fixture
(`MMX_BOSS_RUSH_RHINO_CHARACTER` = `x`, `zero`, `modern` or `saber`) passes
for X, Zero, Modern Zero and Saber Zero: buster and slash damage, his
Acid Burst flinch, drill deflects and the drill-charge pin. Relocated bosses
stay character-agnostic by relying only on what every character shares:

- the player object at `$0BA8` (X3 `$09D8` is patched to it), its position,
  HP and the states `$0C` death, `$0E` hurt and `$1E` scripted lock;
- attacks that reach enemies through X1's collision and the `$86:EF37`
  damage rows (Zero's buster and saber both tag `$1228` shots);
- nothing in `$86:FF40`-`$FFFF`, which the characters' collision tables use.

A new character that keeps these works with every relocated boss; one that
does not needs its own entry in this list and a run of the fixture.

**Not done:**

- Extra X1/X2 weaknesses (assigned once all 24 bosses exist; see below).
- Netplay with relocated bodies is untested.

**MSVC plugin registration (framework issue).** In Visual Studio builds no
mod plugin registered (the launcher reported "The executable does not
provide this trusted plugin"): MSVC drops snesrecomp's unreferenced *static*
`.CRT$XCU` pointer. `src/mods/mmx_mod_constructor.h` redefines
`SNES_MOD_CONSTRUCTOR` with an external pointer plus a `/include` linker
directive; the proper fix belongs in snesrecomp's `mod_runtime.h`.

## Remaining foundation pieces

- Foreign body spawn in a Rush seat: done for registered controllers (X1
  boss class byte `$02`, so class-indexed native tables stay in range).
- Private art for foreign bodies: done for relocated bodies (pose-level;
  bosses whose source art streams per-pose DMA will need that path).
- Foreign sound effects: done for relocated X3 bodies (X2's driver still
  needs checking). The shared `$86` shim and table area is laid out per
  boss today; a second relocated boss needs its own region.
- Weaknesses for relocated bodies: original weakness done for Rhino; the
  owner's X1/X2/X3 assignments come after all 24 bosses.
