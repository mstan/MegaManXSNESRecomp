# X / Zero netplay

Tracking: `beads-8wg.1.54`. Initial implementation: `feat/netplay-coop`,
first stable release 1.7.0.
USA Rev 1 only; the Japanese edition remains unchanged.

## Playing

1. Both players use the same build and supply their own X1 USA Rev 1 and X3
   USA ROMs. Select the X3 ROM in Mods; co-op, Zero, and X3 weapons share that
   local path. Extraction runs automatically.
2. Configure your **local Player 1** controller or keyboard on each computer.
   The network assigns that local input to your room seat. The guest does not
   need a second local controller.
3. Choose **Netplay**, then online or LAN / Direct IP. Online uses the shared
   recomp-net sign-in, lobby, and ICE/STUN/TURN transport. LAN uses the existing
   direct UDP room. There are exactly two player seats, no spectator controls,
   and no competitive matchmaking.
4. The host selects the co-op P1 character and optional gameplay mods. P2 gets
   the counterpart. X2/X3 weapons are optional; each peer supplies the required
   source ROM locally. The room adopts the host's mod versions and options,
   and refuses missing or incompatible selections.
   **Netplay cameras** defaults to **Unified**, with a shared view and separation
   limit. **Independent (Experimental)** follows each peer's character; it is
   opt-in while graphics glitches and camera shaking are investigated.
   Offline play always uses Unified. Dead or cutscene-transported players
   watch their partner's camera while Independent is active.
5. Leave widescreen disabled for the original view, or select **16:9, 21:9,
   or 32:9**. These use the existing renderer. Adaptive is unavailable online.
   Resizing scales/letterboxes the agreed view; it cannot reveal a larger
   playfield. Online pixel proportions use the original CRT presentation
   regardless of offline settings.
6. Start the match from the room. Both characters enroll at a safe stage
   entrance, using the existing co-op health, weapon, pickup, death, shared
   subtank, camera, and cutscene rules. Either player can use **Start** for
   their equipment menu. P2 Select withdrawal/rejoin still works.

Netplay forces co-op and excludes single-player Select exchange. Leaving the
netplay flow restores offline mod selections. ROM paths selected while
preparing a room remain local preferences; they are never sent to the peer.
Neither source ROMs nor extracted graphics are distributed.

Matches start from a cold boot. Automatic state/SRAM loading and saving,
password-file prefilling/capture, local reset, rewind, and emulator pause are
inactive online.

The host can open the save-state menu with **Select+R**. Every player pauses on
the same frame and sees the menu; only the host's controller drives it, and the
other players see the host's cursor move. Saving writes the state on every
player's machine and checks the copies against the host's; a player whose copy
differs is sent the host's state and thumbnail. Loading checks (and if needed
sends) the host's slot first, then every player loads it together. Closing the
menu resumes everyone on the same frame. Other players' copies are kept in
`saves/netplay/`, never over their own saves. Manual password entry still works through synchronized game
inputs. Display, screenshot, and volume shortcuts remain available. A departing
peer cannot write the online match into their offline autosave.

Escape or a peer disconnect returns to a fresh launcher. Closing the game
window exits. This first shared-desktop-host integration does **not** retain
the waiting room for a rematch: create/join a new room for the next match.
No save state is loaded on return or on a normal relaunch.

## Diagnostics

Enabling the **Tier 2 diagnostics** mod (Developer group) also turns on
snesrecomp's netplay diagnostics (`SNES_NET_DIAG`, see snesrecomp
`docs/RECOMP_NET.md`). Each match writes `saves/netplay/net_diag.jsonl`: a
summary line with the transport, lobby, ICE state and NAT path (`host`, `stun`,
`turn` or `lan`) and the selected candidates, then twice-a-second samples of the
path, admit stalls and packet counters. The setting is read when the first
match starts and stays on for the rest of that run, even if the room's mod plan
drops the developer mod. An explicit `SNES_NET_DIAG` environment value wins.

## Implementation and reference

Gundam Wing: Endless Duel's `CMakeLists.txt` and `src/main.c` are the reference
for enabling ICE, lobby identity, launch settings, input admission, and
transport shutdown. MMX keeps its shared desktop host instead of copying
Gundam's game loop or adding a second network implementation.

| Component | Responsibility |
| --- | --- |
| `src/mmx_netplay.c` | Two-seat policy, required co-op, hidden exchange/spectators, fixed view choices, pre-launch asset checks |
| `src/main.c` | Room-stable viewport/pixel proportions and host callbacks |
| `src/mods/mmx_password_plugin.c` | Keep local password files out of the synchronized match |
| `src/mmx_source_assets.cpp` | Per-process temporary filenames for concurrent extraction |
| snesrecomp desktop host | Local input, network pump, cold boot/state isolation, teardown/launcher return |
| snesrecomp mod runtime | Temporary session selections; offline choices survive every commit |
| recomp-ui | Netplay entry/exit policy hook, LAN host option adoption and launch validation |
| recomp-net | Canonical mod-set text in LAN registry, JOIN_OK, CAPS and START |
| retcomm-rbengine | Existing prediction, snapshots, rollback, and digest checks |

The LAN UI backend previously settled neither mod options nor rollback mode,
despite the transport carrying timing fields. It now carries the same bounded
canonical plan as an online room. Invalid/oversized plans fail instead of
silently launching a truncated or old local selection.

MMX raster capture executes native HDMA/IRQ work that can change guest state.
Rollback must run both `RtlRunFrame` and this post-frame raster work. The new
optional `snes_netplay_rb_set_replay_frame` callback runs the same simulation
and capture sequence during replay, without presenting or sampling new input.
Replaying only the CPU frame would omit part of the simulation.

The camera retains the existing 224-native-pixel co-op separation limit.
Wider output does not grant either peer a larger movement allowance.

## Validation (2026-09-30)

- Real catalog/model: co-op required, exchange excluded, spectators absent,
  all three fixed ratios invariant under window size and offline pixel shape,
  Adaptive rejected, local state shortcuts blocked, and offline selections
  restored in memory and after disk reload.
- Two independent installations on UDP: room create/join, host P1=Zero,
  21:9 and X2+X3 selections adopted by guest, 480 gameplay frames with movement,
  jumps, independent weapons/charging, and P2 pause/menu. A deliberate 110 ms
  delay causes an actual prediction correction/replay on both peers. No digest
  forks; final positions and health match.
- Shared UI netplay-host tests, recomp-net LAN registry/packet/rematch tests,
  and the existing ROM-backed co-op regression suite pass.
- The actual desktop executable cold-boots both peers to the same boot digest,
  runs 180 frames through the shared host with autosave configured on, and
  leaves no autosave behind. Guest speed remains capped without VSync; the
  host clock, not monitor refresh, limits netplay speed. Launcher screens were
  captured and visually inspected.

The internet path is compiled and uses the existing shared backend. A real
two-machine internet session, controller playtest, and full online campaign
remain playtest work. Local UDP success is not WAN or campaign certification.

Reproduce the two-peer test after building with `MMX_STATE_TESTS=ON`:

```text
python tools/test_netplay_pair.py --exe build-netplay/mmx_state_tests.exe \
  --rom /path/to/mmx.sfc --x2 /path/to/mmx2.sfc --x3 /path/to/mmx3.sfc \
  --fixture /path/to/grounded-highway.sav --output /tmp/mmx-netplay-check
```

The fixture and ROMs are owner-supplied test inputs, never committed. The
harness copies the executable/catalog into private installations and requires
matching final state plus evidence that rollback actually ran. Owner playtest
launches never implicitly load this fixture or any save slot.

The desktop startup/pacing check needs no fixture and uses a fresh output
directory:

```text
python tools/test_netplay_desktop.py --exe build-netplay/MegaManXSNESRecomp.exe \
  --rom /path/to/mmx.sfc --x3 /path/to/mmx3.sfc --output /tmp/mmx-netplay-boot
```

Remote playtesting is tracked separately in `beads-8wg.1.55`.

Online co-op's [independent camera implementation](netplay-independent-cameras.md)
documents per-seat views, shared world activation, scene handoff and focused
rollback checks. Couch co-op keeps its shared camera.

## Startup and frame delivery follow-up (2026-09-30)

The shared host now batches diagnostic formatting. On this Windows machine,
unbuffered MinGW logs to PowerShell write-through handles took 3–4 seconds
per line. The same launcher/config reached GUI initialization in 0.115 seconds
after the fix, versus 13.232 seconds before. Startup breadcrumbs still flush
immediately. This is the reproduced logging stall; confirmation of the owner's
double-click experience remains useful.

An optional `SNESRECOMP_FRAME_TIMING=<absolute CSV path>` records up to 36,000
presentations in memory and writes them on normal exit. It works offline and
online; regular launches do not enable it. It measures completed host presents,
not physical monitor scanout. See the pinned engine's
[investigation and capture notes](../snesrecomp/docs/WINDOWS_STARTUP_TIMING.md).

Two real desktop/audio UDP peers completed 1,200 frames each with matching boot
digests and steady mean intervals of 16.64 ms. A 165 Hz fixed-refresh display
cannot show native 60.0988 Hz content with equal refresh holds, so display
cadence is one plausible source of uneven motion despite a stable FPS counter.
Offline traces also measured occasional longer host intervals. A trial of a
different wait primitive gave mixed results and was reverted. The remaining
player-observed microstutter is tracked in `beads-8wg.1.56`; it is not declared
resolved by the startup fix.

Subsequent [headed Highway checks](highway-pacing-2026-09-30.md) reproduce the
same long updates in solo and co-op with VSync both on and off. They supersede
the earlier boot/title-only samples for gameplay assessment and identify shared
simulation/audio-time accounting as another investigation target. The follow-up
localized and fixed a missing cooperative yield in X1's graphics streaming:
the matched co-op walking maximum dropped from about 35 ms to 20 ms, with the
same decoded graphics. See that document for final measurements and remaining
display/audio-rollback limits.
