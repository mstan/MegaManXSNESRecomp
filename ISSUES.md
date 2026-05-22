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

Plan: write a C-host scheduler in `mmx_rtl.c` that walks the four
slots once per host frame, dispatching cfg-declared task entry PCs
directly. Every distinct task body (initial 4 + spawned) needs its
own `func` cfg entry. The 6 JMP-indirect-dispatch warnings v2_regen
emits will resolve once the cfg lists every handler PC seen at the
`$0032,X` site across all callers.

## SHA-256 of `mmx.sfc` was computed locally only

The expected hash in `src/main.c`
(`b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32`)
was computed from one local ROM dump and not cross-checked against
another canonical source the way `66871d66...` for ALttP was from
snesrev/zelda3. Verify before publishing.
