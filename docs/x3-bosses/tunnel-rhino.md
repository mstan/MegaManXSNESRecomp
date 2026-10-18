# Tunnel Rhino (X3) — source trace for Boss Rush

Pilot boss for bringing X2/X3 Mavericks into Boss Rush. This is a static trace
of the original USA X3 ROM (normalized SHA-256
`65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7`). Addresses
are SNES LoROM addresses in that ROM. Nothing here is ROM data; the port must
read art, palettes and sound from the user's own ROM at activation, per
[user-supplied source ROMs](../mod-source-roms.md).

Confidence labels: **confirmed** = read directly from code/data and cross-checked;
**inferred** = the most likely meaning of a field or helper, not yet observed
running. Verify inferred items in the X3 recomp before relying on them for
balance.

## How X3 finds its bosses

| Item | Value | Confidence |
| --- | --- | --- |
| Enemy object list | `$0D18`–`$1318`, 64-byte slots (`$1318`–`$1518` is the enemy-projectile list) | confirmed |
| Enemy class byte | object `+$0A` | confirmed |
| Enemy dispatch | `$80:DD24` `JMP ($FA2F,x)`; 105 classes, each a `JSL controller; RTS` stub | confirmed |
| Enemy-projectile dispatch | `$80:DCBD` `JMP ($F8D8,x)` | confirmed |
| Per-class init row | `$86:E28E + (class-1)*5`: sprite set `+$16`, graphics slot, `+$26`, HP `+$27`, `+$28`; loaded by `$02:E15C` | confirmed (field meanings of `+$26`/`+$28` inferred) |

The eight Mavericks are the only classes that call `$04:D8CF`, `$00:850F` and
`$04:D8E1` and register themselves as the HUD boss (`TDC; STA $1F1F`). Each
init checks its own weapon-owned byte at `$1FBC + 2*(weapon-1)`:

| Maverick | Class | Controller | Weapon byte | `+$28` profile |
| --- | --- | --- | --- | --- |
| Toxic Seahorse | `$57` | `$13:E493` | `$1FBC` Acid Burst | `$15` |
| Blast Hornet | `$53` | `$39:9BA9` | `$1FBE` Parasitic Bomb | `$12` |
| Volt Catfish | `$58` | `$13:EA67` | `$1FC0` Triad Thunder | `$16` |
| Crush Crawfish | `$54` | `$03:D03D` | `$1FC2` Spinning Blade | `$17` |
| Neon Tiger | `$56` | `$13:DCA0` | `$1FC4` Ray Splasher | `$19` |
| Gravity Beetle | `$59` | `$13:F239` | `$1FC6` Gravity Well | `$14` |
| Blizzard Buffalo | `$52` | `$03:C861` | `$1FC8` Frost Shield | `$13` |
| **Tunnel Rhino** | **`$55`** | **`$3F:E5E4`** | **`$1FCA` Tornado Fang** | **`$18`** |

Class `$15` (`$07:BC3A`) also tests `$1FBE` but has 5 HP; it is not a Maverick
body. All eight Mavericks have 32 HP.

## Object fields Rhino uses

X3 objects mirror X1's layout closely. Fields below are as Rhino uses them.

| Field | Use | Confidence |
| --- | --- | --- |
| `+$01` / `+$02` / `+$03` | state / sub-state / step (all even, jump-table indices) | confirmed |
| `+$04..$06` | X position, `$04` sub-pixel, `$05-$06` pixels | confirmed (`$02:D7F3`) |
| `+$07..$09` | Y position, same layout | confirmed (`$02:D78E`) |
| `+$0B` | variant / spawn index (children) | confirmed |
| `+$0E` | on-screen flag (children delete when 0) | inferred |
| `+$0F` | animation status: bit 7 = sequence finished, bit 6 = keyframe event | confirmed by use |
| `+$11` | OBJ attributes; bit 6 = facing right | confirmed (`$02:E13F`) |
| `+$16` | sprite set (`$25` for Rhino); animation directory `$3F:8000 + set*3` → `$3F:B8D8` | confirmed |
| `+$18` | graphics-slot derived tile base (from `$7F8200`) | confirmed |
| `+$1A/$1B` | X velocity, 8.8 fixed point, signed | confirmed |
| `+$1C/$1D` | Y velocity, 8.8 fixed point, **positive = up** (`Y -= $1C`) | confirmed |
| `+$1E/$1F` | velocity targets / per-frame acceleration (children) | confirmed by use |
| `+$20` | pointer to hitbox record in bank `$86` | confirmed |
| `+$26` | contact damage to the player | inferred |
| `+$27` | HP (bit 7 = HUD refill flag) | confirmed |
| `+$28` | damage/defense profile id | inferred |
| `+$2B` | terrain contact: bits 0-1 walls, bit 2 floor | confirmed by use |
| `+$33` | hurt/freeze reaction request (`$02:DB3F`) | inferred |
| `+$35..$3D` | locals (see below) | confirmed |
| `+$37` | invulnerability/flash timer (`$02:DF64`) | confirmed |
| `+$38` | saved palette attribute bits | confirmed |

Rhino locals: `$35` drill-volley-fired flag, `$36` "player pinned" flag, `$39`
timer, `$3A` drills left to spawn, `$3B` "stop mid-charge" coin flip (state 0)
or drill-charge marker (state 1), `$3D` live drill count (children decrement
it through parent pointer `+$38` at `$08:AE02`).

Globals: player object `$09D8` (state `$09DA`, X `$09DD`, Y `$09E0`, HP
`$09FF`); camera `$1E5D`/`$1E60`; RNG `$09D6`; HUD boss pointer `$1F1F`;
Rhino mirrors `$01/$02/$03` to `$1F54..$1F56` and `$28` to `$1F57` for his
children; stage `$1FAE` (`$0C` = Doppler refights), boss room id `$1FE0`.

## Controller layout

`$3F:E5E4` dispatches on `+$01` through `$3F:E5E9`:

| `+$01` | Handler | Meaning |
| --- | --- | --- |
| 0 | `$E617` | Intro (3 steps, below) |
| 2 | `$E793` | Combat |
| 4 | `$EAE4` | Death |
| 6 | `$E5F5` | Generic hurt reaction `$02:DB3F`, then resume combat |
| 8 | `$E601` | Return to combat |
| 10 | `$E607` | Weapon freeze/stun via `$02:DDFE`, death if HP ran out |

Code footprint: about 1.7 KB in bank `$3F` (`$E5E4`–`$ECA1`), plus two
palette helpers in bank `$13` and the shared projectile class below.

### Intro (`+$01 = 0`)

1. `$E622`: if Tornado Fang is owned (`$1FCA` bit 6) and this is not a
   refight, Rhino is removed (`$EB0A`). Otherwise lock the player
   (`$04:D1CA`), upload SPC bank `$5D` (`$00:850F`, his sound effects), start
   boss music `$27` (`$00:84BC`).
2. `$E675`: wait until the screen settles and the player is grounded
   (`$0A03` bit 2). Snapshot OBJ palette `$0480`→`$04A0`, run class init,
   HP=0, contact damage 3, position at camera+(`$D0`, `-$20`), Y velocity
   −3.0 px/frame (falling), body hitbox `$86:EC87`, register as HUD boss.
3. `$E6E6` steps: copy his palette to `$7F:CAC0` (restored on death), fall
   until camera Y + `$98` and floor contact, 20-frame landing pause, play
   animation `$0B`, then refill HP to 32 one point every 2 frames with sound
   `$15`. Finally release the player (`$04:D1EF`), request music `$1A` and start combat.

### Combat (`+$01 = 2`, `$E793`)

Every frame: player-death guard (`$04:D8D5`), flash timer (`$02:DF64`),
behavior for `+$02`, release-player check (`$EB88`), shot damage
(`$04:CB74`; negative = dead → `$E7C8`), weakness check, contact damage
(`$04:CB31`, skipped while the player is in state `$74` or `$0A43` is set),
mirror state to `$1F54..$1F56`, screen cull (`$02:D636`), animate/draw
(`$04:B94A`).

**Weakness reaction** (`$E7EF`/`$E838`): if the hit came from weapon kind
`$1F2F` ∈ {`$07`, `$10`, `$18`, `$21`} (inferred to be Acid Burst's damaging
parts) Rhino flinches: 30-frame timer, sound `$21`, 60 frames of flashing,
defense profile 1, palette bits cleared. From the drill charge he goes to
behavior 5 (drill wind-down), otherwise behavior 4.

**AI decision** (`$EBA1`), after every behavior:

- HP ≥ 12: with 3/4 chance charge (`+$02=0`), coin flip `$3B` decides
  whether the charge stops in the middle to fire a spread. With 1/4 chance
  fire the drill volley (`+$02=4`) unless drills are still alive (`$3D`), in
  which case charge.
- HP < 12: if `$3B` was set, choose as above (volley if no live drills);
  otherwise the **drill charge** (`+$02=2`).

| `+$02` | Handler | Behavior |
| --- | --- | --- |
| 0 | `$E85D` | **Charge.** Face player (or turn away from a wall), 4.0 px/frame, dust every 4 frames (`$EC7D`→`$02:DF8A`). On wall contact: screen shake (`$04:D580`, A=`$32`, X=2; A is inferred to be the duration), sound `$49`, 60-frame recoil, then decide. If `$3B` is set and he crosses the room's half-screen mark, anim 5 + sound `$72`, decelerate 0.25 px/frame², then fire the spread (`+$02=6`) if no drills are alive. |
| 2 | `$E926` | **Drill charge.** Palette cycling every frame (`$13:F7D6`), 5.0 px/frame, contact damage 5, defense profile `$0D`. Hitting the player spawns the pin carrier (variant 2) at the player. On wall contact: stronger shake (A=`$3C`, X=3), sound `$49`, 60-frame recoil, palette fade back (`$13:F7FC`), contact damage 3, profile `$18`. |
| 4 | `$E9F4` | **Drill volley.** Face player, anim 6→7, on keyframes spawn one then two drills (variant 0, `+$0B` = 3, 2, 1), wait for the animation, then charge. |
| 6 | `$EA62` | **Spread.** On keyframe spawn three angled drills (variant 4), sound `$70`, then idle. |
| 8 | `$EA9A` | Flinch countdown, then 90 frames of flashing and decide. |
| 10 | `$EAAA` | Drill wind-down after a weakness hit: finish palette fade, restore damage/profile, decide. |
| 12 | `$EABE` | Idle: wait for the player to be free, 30 frames, decide. |

**Pinning** (`$EB66`/`$EB88`): during drill recoil, if the player state
becomes `$12` (inferred: caught by the drill), Rhino sets `$36` and locks the
player (`$04:D1CA`, state `$1E`); the lock is released when recoil ends, on
a weakness hit, or when the player is already in `$1E`.

### Death (`+$01 = 4`, `$EAE4`)

`$E7C8` enters death with animation `$0A`. Step 0 restores his palette from
`$7F:CAC0` and runs the shared boss explosion `$02:E0BA`; step 1 continues
`$02:E0E6`; step 2 (`$EB0A`) awards Tornado Fang (`$1FCA=$FF`, `$1F36++`) or
the refight flag (`$1FDA |= $40`) and deletes the object. **Boss Rush must
replace step 2**, as it does with X1's native global death.

## Projectile class `$26` (`$08:AB9B`)

Spawned by Rhino with `$02:D8DE` in the enemy-projectile list. Shared init
`$EC48`: flags `$41`, class `$26`, Rhino's sprite set and tile base, palette
attributes + 2, parent pointer in `+$38`. Variant in `+$3C`. Every child dies
with Rhino (`$1F54 == 4` → explosion `$04:D6E0`, delete `$02:D933`).
Drills have **their own HP** and can be shot down; contact damage 4 (inferred).

| Variant | Spawned by | Behavior |
| --- | --- | --- |
| 0 — volley drill | `$EC02` | Starts ±46 px in front, 8 px up; HP 18, anim `$0F`. Per index (`+$0B`−1): hover (delays 16/16/0 frames, Y speed −1/+1/0), wait 96/96/160 frames, then anim `$0C` and accelerate 0.25 px/frame² to 4.0 px/frame toward his facing. On a wall: slows to +0.31 or −0.69 px/frame and grinds with sound `$73` every 32 frames until off-screen. |
| 2 — pin carrier | `$EBE1` | Invisible; falls 4.0 px/frame with ±1.0 px/frame drift and writes its position into the player's X/Y each frame until it lands. |
| 4 — spread drill | `$EC1A` (×3) | HP 16. Directions from `$86:EFD6`/`$86:E18E`: forward (2.0 px/frame), 45° up-forward and 45° up-back (1.41, 1.41). Offsets `$86:EFE7`. On terrain contact speed drops to 1/4; grinds with `$73` until off-screen. |

Hitbox records (bank `$86`, 10 bytes: interaction box then terrain box,
inferred as offset X/Y and half-extents): body `$EC87` (0,−3, 30×26),
volley drill `$EFC2` (12×8), angled drill `$EFB8` (8×8), carrier `$EFCC`
(8×16).

## Shared engine helpers he calls

The port needs X1-side equivalents for each. Existing X1 counterparts are
noted where the Boss Rush code already uses one.

| X3 helper | Purpose | Confidence |
| --- | --- | --- |
| `$04:B967` | Start animation A from sprite set directory | confirmed |
| `$04:B94A` | Advance animation and draw | confirmed |
| `$02:D636` | Off-screen cull against camera | confirmed |
| `$02:D7F3` | Apply X velocity | confirmed |
| `$02:D78E` | Apply X and Y velocity | confirmed |
| `$02:D6F8` | Velocity approach toward `$1E/$1F` | inferred |
| `$04:C0F7` | Terrain collision → `$2B` | confirmed |
| `$04:CC5C` | Hitbox overlap with object X | confirmed |
| `$02:E13F` | Face the player | confirmed |
| `$04:BE35` | RNG step (`$09D6`) | confirmed |
| `$01:802B` / `$01:8000` | Sound effect / positional sound effect | confirmed |
| `$04:D580` | Screen shake | confirmed |
| `$04:CB74` / `$04:CB31` | Take player-shot damage / deal contact damage | confirmed |
| `$04:D1CA` / `$04:D1EF` | Lock / release player control | confirmed |
| `$02:DF64` | Damage flash timer | confirmed |
| `$02:DF8A` | Spawn dust effect (effect class `$10`) | confirmed |
| `$02:D8DE` / `$02:D859` | Allocate enemy-projectile / effect slot | confirmed |
| `$00:850F` / `$00:84BC` | SPC bank upload / music request | confirmed |
| `$13:F7D6` / `$13:F7FC` | Rotate palette colors 1–15 / fade back | confirmed |
| `$02:DB3F`, `$02:DDFE`, `$02:E0BA`, `$02:E0E6` | Hurt reaction, freeze/stun, boss explosion | inferred |

Rhino's own code never touches the Cx4 window.

## Art, palette and sound sources

- **Sprite set `$25`**, animation directory `$3F:B8D8` (16-bit offsets).
  Animations used: body `0`–`7`, `$0A`, `$0B`; drills `$0B`–`$0F`.
  Pose layouts: `$8D:8000 + $25*3` → pose table `$8D:9035` (3-byte pointers
  to piece lists, the same format `tools/extract_x_weapons.py` `pose_art`
  reads). Body poses use tiles `$00`–`$97` of the sheet.
- **Graphics slot `$1D`.** Stage lists at `$88:8623` (indexed by `$1FAE`,
  then `$1F18`; 6-byte entries: slot, VRAM word address, palette list index,
  CGRAM index; `$FF` ends) load slot `$1D` in Rhino's stage (`$07`, setting
  `$0B`) and in the Doppler refights (`$0C`/`$0A`, `$0F`/`$06`): VRAM `$1000`,
  palette list `$3E`, CGRAM `$40`. Its VRAM DMA chunk list is at `$88:DA00`.
- **Tiles: compressed resource `$1D`** (confirmed). The slot number is the
  resource number: the decompressor task `$80:B730` reads record
  `$86:F732 + slot*5` (3-byte source, 2-byte decoded length) and records the
  output address in `$7F8000[slot]`, which the slot loader `$80:B2DB` then
  DMAs to VRAM. Record `$1D`: source `$9E:C284`, 5,231 compressed bytes,
  **6,144 bytes decoded (192 4bpp tiles)**. It is the same LZ format the weapon
  extractor already decodes (`menu_graphics(rom, 3, 0x1d)`). The sheet holds
  the body, head, fists, drill arm, drill projectiles and dust/spark effects.
- **Palette** (confirmed): palette list `$86:8180 + $3E` → `$86:8419`, one
  record of 16 colors from `$8C:E628`. Its destination works out to color
  `$C0` (OBJ palette 4), which matches the `$0480` buffer his intro copies and
  his drill-charge effect rotates (inferred from that match).
- **Verification:** decoding resource `$1D` with that palette and assembling
  poses 0–11 from `$8D:9035` produces complete, correctly colored Tunnel Rhino
  poses (idle, drill thrust, fist, walking dust). Images were reviewed
  privately and are not distributed.
- **Sound:** SPC bank `$5D`. Effects `$21` (weakness hit), `$49` (wall
  crash), `$70` (spread), `$72` (mid-charge stop), `$73` (drill grind);
  `$15` HP fill and music `$27`/`$1A` are shared. Boss Rush already renders
  boss effects through an isolated SPC copy; Rhino needs bank `$5D` loaded
  from the X3 ROM into that copy.

## Port implications

1. **Two private palette effects.** Drill charge rotates and fades OBJ
   palette `$0480` directly, and death restores it from `$7F:CAC0`. Like
   Chameleon's camouflage, Rush must keep this private to Rhino's seat.
2. **Player lock.** The intro and pinning freeze the shared player. In Rush
   the intro lock should be skipped (bosses enter during play), and pinning
   must target the specific player that was hit in co-op.
3. **Arena.** He charges into walls and spawns drills that grind along them;
   the Mammoth room's walls work. The "half-screen" stop uses the low byte of
   his X position, so it fires at a fixed screen phase; re-anchor it to the
   arena center.
4. **Defense profiles** `$18` / `$0D` / `1` need an X1-side weakness table
   covering X1, X2 and X3 weapons (`src/mmx_weapon_weakness.inc`). X3's
   original weakness is Acid Burst, which is in the X3 pack.
5. **Death:** use the Rush cleanup path, never `$EB0A`'s weapon award.

## Reproducing this trace

A small recursive 65816 disassembler with M/X tracking that follows jump
tables reproduces every listing used here from the controller entry points
`$3F:E5E4`, `$08:AB9B`, `$13:F7D6` and `$13:F7FC`. Data tables were read with
data bank `$86` (verified by all eight Mavericks reading 32 HP from
`$86:E28E`).
