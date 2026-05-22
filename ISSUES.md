# Open issues — MegamanXRecomp

## Status — 2026-05-22

The runtime no longer hangs or crashes. The C-host fiber scheduler is
operating correctly: Task0 ($852C) and Task_B25B ($B25B) are both
dispatched per host frame, both yield (state=2 cd=1) and resume, and
the slot-walking + per-slot S restoration is preserving each task's
emulated 65816 stack window across ticks.

End-to-end, the recompiled MMX:

- Boots through ResetHandler without crashing
- Loads CGRAM (palette data verified non-zero at $00-$3F)
- Fires DMA-to-VRAM uploads from bank-0 routines around $B9F3
- Holds inidisp = $C9 (forced blank + brightness $9) — Task0 is
  parked in its attract fade-wait loop at $00:$867F-$8693

No visible screen content yet: bit 7 of $2100 (forced blank) is set
on every frame because Task0 hasn't reached the unblank step.

## The two recompiler-level fixes that unblocked the scheduler

1. **Decoder sibling-jump tail-call rule generalised (2026-05-22).**
   The earlier form refused inline-import only when the sibling entry
   sat ABOVE the current function in PC space (`pc >= end` +
   `pred_pc < end`). MMX's TaskDie at $00:$80F8 sits BELOW its callers
   ($852C, $B091, $B25B, ...) and slipped through — every task body's
   JMP $80F8 inlined the asm scheduler walk into the calling task,
   which overwrote the current slot's $30,X state byte and then
   walked into the asm coroutine-resume at $80E9. Now any JUMP edge
   into a named sibling function entry is a tail-call.

2. **$00:$8121 conditional yield wrapper HLE'd (2026-05-22).**
   `BIT $0B9D ; BMI yield ; RTS`. The BMI-taken path PHX/PHY/PHP, sets
   slot state=$02/cd=$01, TSCs the saved-S into $34,X, and JMPs to
   the asm scheduler — to be resumed via $80E9's TCS/PLP/PLY/PLX/RTS.
   Under the C-host fiber model the asm coroutine-resume doesn't run,
   so the JMP $8099 (HLE-replaced by MainLoopReturn) just returns
   NORMAL — leaving 3 emulated-stack bytes pushed that the asm
   scheduler was supposed to pop. The caller's post-JSR
   PLB/PLP/PLY/PLX then reads the wrong bytes and the next iteration
   of Task_B25B's decompressor runs with corrupted state ($F8
   especially), then trips the asm's CPX #$8000 / BCS-self panic at
   $B2E2. HleMmxYieldVblank reads $0B9D, yields iff bit 7 is set,
   and otherwise returns without touching the stack.

## Remaining: Task0 progress past the attract fade-wait loop

Task0 reaches the fade-wait loop at $$00:$867F-$8693 (writes Y |
$E0 to $CB/$CC/$CD then yields, up to $1F iterations or until
$00:$86AC's input-check returns Z=0). With no controller input the
loop should exit naturally after $1F yields and continue past
$8695. In our run, scene-index byte at DP $C0 stays at $17 for
thousands of frames — Task0 is not progressing past this point.

## Concrete next-session lead — wrong DB at Task_B25B dispatch

Task_B25B's decompressor reads its per-iteration count table from
`LDA $F6F7,Y` (absolute,Y with DB providing bank). Y is derived
from DP $98 (`5*$98`). The trace shows cpu->DB = $F6 when Task_B25B
runs. In LoROM with a 1.5 MiB ROM, bank $F6 reads at addr
$F7AB (= $F6F7 + Y for Y = $B4 from $98 = $24) are out-of-ROM →
snes9x core returns open bus → `(open_bus + 7) >> 3` produces a
huge $FA, the outer loop runs many thousand times, X overflows
past the $8000 destination bound, and STX $F8 at $B2E4 writes a
corrupted $F8/$F9 pair (observed: $00 $E0 → 16-bit value $E000).
On the NEXT dispatch of Task_B25B, the corrupted $F8 is read into
X and the decompressor spins forever at $B2E2 again.

The PLB chain leading into the run was $00 → $86 → $E3 → $BB →
$F6 (visible in DB-WATCH events in `_run.log`). The recomp is
faithfully executing the asm PLBs — the question is whether the
asm at Task_B25B's caller is supposed to have DB = $86 at the
moment of dispatch (real ROM data at $86:F7AB is a valid count
table) and our C-host scheduler is failing to restore it, or
whether the asm legitimately runs with DB = $F6 and there's a
SEPARATE init path that sets DP $98 to a value that makes
`F6F7 + 5*$98` land back inside valid ROM.

The first thing to do next session: dump the asm at $00:$B238
(the install wrapper called via `JSL $00:B238`) and trace the
specific call site that installs Task_B25B + sets DP $98 (or
sets DB ahead of the install). The wrapper itself at $B23C-$B25A
does NOT PHB — so DB inherits from the install's caller. Find that
caller.

## Spurious $E0 every-4-bytes WRAM pattern (red herring)

Dumps of arbitrary WRAM addresses show $E0 at every offset 4N+1
(e.g. $0001, $0005, $0009, ..., $00FD, $0101, $0105, ...). This is
NOT corruption — it's the OAM-Y-coord "hide sprite" idiom MMX uses
across many tables. Confirmed by tracking the writes: STA abs,X
with base $0701, $0801, $0700, etc. and A = $E0 (off-screen Y).
Recompiler is faithfully emitting these stores. They land in the
OAM shadow tables and look noisy in DP dumps because some MMX
tables alias DP-range addresses via the LoROM bank-mirror.

## Pre-existing items (not yet revisited)

### SHA-256 of `mmx.sfc` was computed locally only

The expected hash in `src/main.c`
(`b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32`)
was computed from one local ROM dump and not cross-checked against
another canonical source the way `66871d66...` for ALttP was from
snesrev/zelda3. Verify before publishing.
