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
- [ ] Validate every partial armor combination in built solo/co-op/native/LLE views.
- [ ] Match X2/X3 air-dash timing, recovery, dash-jump restrictions and collisions.
- [ ] Implement original X3 upward dash and its separate body/armor animation groups.
- [ ] Implement X2 double charge and X3 cross charge, with donor projectiles/effects.
- [ ] Implement X2 Giga Crush, projectile energy conversion and usable meter/input.
- [ ] Implement X3 reactive barrier graphics and validate its damage lifecycle.
- [ ] Implement X2 Item Tracer and X3 radar against actual uncollected X1 content.
- [ ] Validate Gold recovery, enhanced barrier, Hyper Charge and second air dash.
- [ ] Validate real capsule progression, death/reset/stage transition and native saves.
- [ ] Validate current-build snapshots/rewind, co-op X + Saber Zero, Boss Rush.
- [ ] Prepare a lightweight owner playtest selector with reproducible ability checks.

This is unfinished development work, not a release or completed feature.

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
it moves for 17 ticks, followed by an eight-tick recovery. The prototype's
18-tick native ground-dash adaptation still needs correction and collision QA.
X3 upward dash starts with VY `$0078` and accelerates; a constant `$0375`
vertical dash is not equivalent and must not be accepted.

Source charge thresholds are 20, 80, 140 and 200 held ticks. The arm upgrade
unlocks tiers after 140/200. A fully charged X2 first shot is class 3/group
`$9E`; its second is class `$1D`/group `$54` with trailing components.
X3 reverses the pattern: the first is class `$1D`/group `$3A` with three
satellites; the following green shot intersects it to create the cross charge
(group `$54`, then four class `$20` fragments).

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
Initial source extraction, existing Zero tests and native renderer tests passed.
Those checks do not establish the unfinished mechanics above.
