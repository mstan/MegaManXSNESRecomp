# Open issues — MegamanXRecomp

## Cooperative task scheduler at $00:8099 — HLE pending

The asm "main loop" at `$00:8099` is a cooperative-coroutine task
scheduler, not a normal frame-tick function. It cannot be called from
the C host as-is because it terminates with `JMP $8099` (infinite),
and the inner spinlock on `$7E:0B9D` deadlocks without an NMI
interrupting the host thread.

Confirmed structure (cross-referenced against the public disassembly
at [MarkAlarm/MMX1-Disassembly](https://github.com/MarkAlarm/MMX1-Disassembly)):

```
Slot fields, 16 bytes per slot at $0030-$006F (4 slots at $30/$40/$50/$60):
  $30,X = state (0=empty, 1=ready, 2=delayed, 3=suspended)
  $31,X = delay countdown (state 2)
  $32,X / $33,X = handler PC (used for JMP ($0032,X) at $80E6)
  $34,X / $35,X = saved S (resume state-3 tasks via TCS + PLP/PLY/PLX/RTS)
  $36,X / $37,X = entry-task S setup (state-1 start path)
```

Yield site: `$00:80F8` (`SEP #$30 ; LDX $A0 ; STZ $30,X ; BRA $80B9`).

### Concrete next-session plan

Sub-tasks, in dependency order:

1. **Framework: generic `hle_func` cfg directive.** The existing
   `hle_spc_upload` directive is hard-coded to emit one specific C
   helper call. The task-scheduler HLE needs the same shape but
   targeting different C functions per cfg entry (yield wrappers,
   the dispatcher itself, etc.). Add a `hle_func <pc> <c_func_name>`
   directive that emits a stub forwarding to the named C helper.
   This is the unblocker — without it, hand-written HLE bodies in
   `gen_stubs.c` collide with recompiler-emitted bodies at link time.

2. **Initial task entry-PC enumeration.** From the disassembly:
   - Task 0 (slot $30) is installed at boot with handler PC `$00:852C`
     via `LDA #$852C / JSR $00:813B` at `$00:807F-$8082`.
   - Task slot entry-S values come from the table `DATA16_868067`
     (= `$00:8067` via LoROM mirror): `$013F, $017F, $01BF, $01FF,
     $023F, $027F, $02BF` (slots 0-6).
   - Task 0's body at `$00:852C` JSRs through many helpers and yields
     via wrappers at `$00:8100`, `$00:810C`, `$00:8A45`, `$00:8995`,
     `$00:8121`. Tasks 1-6 are spawned dynamically.

3. **Yield wrappers as `hle_func` entries.** `$8100` / `$810C` are
   the canonical yields. They `BRA $80B9` (scheduler next-slot) and
   never return to the caller. C HLE for these = `longjmp` to a
   per-task `jmp_buf` saved by the C scheduler before invoking the
   task body. The other "yield-ish" entries at `$8121` and similar
   conditionally yield based on `BIT $0B9D`.

4. **C-host scheduler in `mmx_rtl.c`.** One iteration per host
   frame. For state=1 slots, set `cpu->S` to slot's entry-S (from
   `g_ram[$36+X]`), look up the handler PC from `g_ram[$32+X]`,
   `setjmp(per_slot_jmp_buf)`, call the cfg-declared `func` for that
   PC. For state=2 slots, decrement countdown; on zero, the same
   resume path via the saved-S at `g_ram[$34+X]` — this needs a
   coroutine that can re-enter mid-function, which only works if the
   yield site preserved enough state to restart from the post-yield
   PC (asm intent) OR we accept restart-from-entry semantics (lossy
   but might progress task-0 via the `$7E:FFFF` progress flag the
   task body already uses as a state machine).

5. **Variant discovery.** Each `func` declared above needs the
   correct (M, X) entry variant. Scheduler invokes task at `$852C`
   with M=1 X=1 (verified from `$80DA` flow: previous `SEP #$30` at
   `$80E9` is for state-2 path; `$80DA` enters with whatever M/X the
   dispatch loop had, which is M=1 X=1 from `$809F SEP #$30`).

## SHA-256 of `mmx.sfc` was computed locally only

The expected hash in `src/main.c`
(`b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32`)
was computed from one local ROM dump and not cross-checked against
another canonical source the way `66871d66...` for ALttP was from
snesrev/zelda3. Verify before publishing.
