/* gen_stubs.c — hand-written HLE bodies the recompiler couldn't (or
 * shouldn't) synthesise. For MMX these are the cooperative-task-
 * scheduler yield primitives: the asm wrappers at $00:8100 and
 * $00:810C set the slot's state to "delayed" and BRA into the
 * scheduler's next-slot loop. From the calling task's frame they
 * never return, so each stub picks one of two host models at runtime:
 *
 *   HLE scheduler (MmxSchedulerTick, explicit legacy override): suspend
 *   the hosting fiber via mmx_host_yield (SwitchToFiber back to the
 *   C-host scheduler; the fiber resumes here on the next dispatch).
 *
 *   LLE scheduler (interp_bridge_run_scheduler; fiber-free, docs/
 *   LLE_SCHEDULER.md): there are no fibers. The stub arms an NLR
 *   unwind (interp_bridge_lle_yield_unwind) whose sentinel return
 *   propagates through every emitted callsite back to the scheduler
 *   frame's bounce site, which resumes INTERPRETING at the primitive's
 *   REAL ROM entry. The interpreter then executes the actual coroutine
 *   switch — LDX $A0 ; state/countdown -> $30/$31,X ; TSC -> $34/$35,X
 *   ; re-enter the slot walk at $80B9 — byte-exact by construction.
 *   The compiled JSR callsite already pushed the guest return frame,
 *   which the real resume path (LDA $34,X ; TCS ; PLP ; PLY ; PLX ;
 *   RTS at $80E9) pops when the slot's countdown expires; the interp
 *   then continues the task's tail from the post-JSR guest PC,
 *   bouncing its subsequent calls back into compiled bodies.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu_state.h"
#include "common_rtl.h"
#include "cpu_trace.h"
#include "variables.h"

/* Yield primitives bridge from recompiled task bodies to the host
 * fiber-based scheduler in mmx_rtl.c. mmx_host_yield switches to the
 * scheduler fiber; the fiber resumes here on the next dispatch. */
extern void mmx_host_yield(uint8_t countdown);
extern uint8_t g_mmx_task_slot_x;  /* asm $A0 mirror, current slot offset */
extern int snes_frame_counter;
extern void RecompStackDump(void);

/* Fiber-free LLE unwind target: the primitive's real ROM entry in the
 * caller's bank. The yield primitives live in bank $00; every compiled
 * callsite executes in the $00/$80 LoROM mirror, so PB:addr16 always
 * lands on the real bytes (the interp bus mirrors banks anyway). */
static RecompReturn mmx_lle_unwind_to(CpuState *cpu, uint16 addr16) {
  return interp_bridge_lle_yield_unwind(
      cpu, ((uint32)cpu->PB << 16) | (uint32)addr16);
}

static int mmx_hle_diag_enabled(void) {
  static int s_init = 0;
  static int s_enabled = 0;
  if (!s_init) {
    const char *v = getenv("MMX_RTL_DIAG");
    s_enabled = (v && v[0] && v[0] != '0');
    s_init = 1;
  }
  return s_enabled;
}

RecompReturn HleMmxYieldOneFrame(CpuState *cpu) {
  /* $00:8100: yield, wake next frame. */
  if (interp_bridge_in_lle_scheduler())
    return mmx_lle_unwind_to(cpu, 0x8100);
  static int s_logged_bad_slot0_yield = 0;
  if (mmx_hle_diag_enabled() &&
      !s_logged_bad_slot0_yield && g_mmx_task_slot_x == 0 && cpu->D != 0) {
    s_logged_bad_slot0_yield = 1;
    fprintf(stderr,
            "[mmx_yield_bad_state] frame=%d slot=0 A=$%04X X=$%04X Y=$%04X S=$%04X D=$%04X DB=$%02X PB=$%02X P=$%02X m=%u x=%u\n",
            snes_frame_counter, cpu->A, cpu->X, cpu->Y, cpu->S, cpu->D,
            cpu->DB, cpu->PB, cpu->P, cpu->m_flag, cpu->x_flag);
    RecompStackDump();
    fflush(stderr);
#if SNESRECOMP_TRACE
    g_stack_drift_tripwire.triggered = 1;
#endif
  }
  mmx_host_yield(1);
  /* On resume (next dispatch), fall through and return normally —
   * the caller (asm's JSR $8100) continues at the post-JSR site.
   *
   * Option-1 cpu->S ABI: the JSR $8100 site pushed a 2-byte return
   * frame onto cpu->S. On hardware the coroutine RESUME pops it via
   * its RTS (REP #$30 ; TCS ; PLP ; PLY ; PLX ; RTS). This fiber HLE
   * collapses yield+resume into a single C return, so it must pop that
   * frame here to model the resume RTS. Without it cpu->S leaks 2
   * bytes per yield, the leak is persisted across frames by the
   * per-slot CpuState save (mmx_save_cpu saves cpu->S), and cpu->S
   * drifts down into zero page — eventually a JSR push lands on the
   * DMA queue tail ($00A5/A6), wedging the 82C8 -> B9F3 -> BA48
   * walker. (Always exactly one frame, whether $8100 was JSR'd
   * directly or tail-reached with an inherited caller frame.) */
  cpu->S = (uint16)(cpu->S + 2);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxYieldNFrames(CpuState *cpu) {
  /* $00:810C: caller passes the countdown in A's low byte (the real
   * code XBAs it into $31,X). LLE: interpret the real routine — its
   * countdown/width semantics (including n==0 -> 256-frame wrap via
   * DEC $31,X) apply byte-exact. The fiber HLE keeps its historical
   * n==0 -> 1 coercion. */
  if (interp_bridge_in_lle_scheduler())
    return mmx_lle_unwind_to(cpu, 0x810C);
  uint8_t n = (uint8_t)(cpu->A & 0xFF);
  if (n == 0) n = 1;
  mmx_host_yield(n);
  /* Option-1 cpu->S ABI: pop the JSR return frame (see
   * HleMmxYieldOneFrame for the full rationale). */
  cpu->S = (uint16)(cpu->S + 2);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxYieldVblank(CpuState *cpu) {
  /* $00:8121 conditional yield: `BIT $0B9D ; BMI $8127(yield) ; RTS`.
   * Returns without yielding when $0B9D bit 7 is clear; otherwise
   * yields with cd=1, just like $8100. Caller's PHX/PHY/PHP/PHB at the
   * JSR site (e.g. Task_B25B at $B2CC-$B2CF) is popped by the caller's
   * own post-JSR PLB/PLP/PLY/PLX — those caller-saved regs are separate
   * from the JSR return frame handled below.
   *
   * LLE: only the YIELD path unwinds to the real routine (which redoes
   * the BIT — idempotent — and takes the $8127 coroutine switch). The
   * no-yield path is the decompressor's hot inner check (every 32
   * decompressed units); keeping it compiled is the point of bounce,
   * so model the real `BIT $0B9D ; RTS` here byte-exact: BIT's N/V/Z
   * at the live M width, then pop the JSR frame like the RTS. */
  if (interp_bridge_in_lle_scheduler()) {
    if (g_ram[0x0B9D] & 0x80)
      return mmx_lle_unwind_to(cpu, 0x8121);
    if (cpu->m_flag) {
      uint8 v8 = g_ram[0x0B9D];
      cpu->_flag_N = (v8 & 0x80) ? 1 : 0;
      cpu->_flag_V = (v8 & 0x40) ? 1 : 0;
      cpu->_flag_Z = (((uint8)(cpu->A & 0xFF) & v8) == 0) ? 1 : 0;
    } else {
      uint16 v16 = (uint16)(g_ram[0x0B9D] | ((uint16)g_ram[0x0B9E] << 8));
      cpu->_flag_N = (v16 & 0x8000) ? 1 : 0;
      cpu->_flag_V = (v16 & 0x4000) ? 1 : 0;
      cpu->_flag_Z = ((cpu->A & v16) == 0) ? 1 : 0;
    }
    cpu->P = (uint8)((cpu->P & ~0xC2) |
                     (cpu->_flag_N ? 0x80 : 0) |
                     (cpu->_flag_V ? 0x40 : 0) |
                     (cpu->_flag_Z ? 0x02 : 0));
    cpu->S = (uint16)(cpu->S + 2);
    return RECOMP_RETURN_NORMAL;
  }
  /* Option-1 cpu->S ABI: BOTH paths consume one JSR return frame — the
   * RTS path directly, the yield path via the resume RTS — so pop the
   * 2-byte frame here (see HleMmxYieldOneFrame for full rationale). */
  uint8_t v = g_ram[0x0B9D];
  if (v & 0x80) {
    mmx_host_yield(1);
  }
  cpu->S = (uint16)(cpu->S + 2);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxMainLoopReturn(CpuState *cpu) {
  /* $00:8099 — asm main loop entry. HLE: replaced wholesale by C-host
   * MmxSchedulerTick in mmx_rtl.c; ResetHandler's JMP $8099 tail-calls
   * here so ResetHandler becomes a returnable init-only fn. That boot
   * arrival happens OUTSIDE any scheduler frame, so it returns NORMAL
   * under both tiers. If a compiled body ever re-enters the loop from
   * INSIDE the LLE scheduler frame, hand it back to the interpreter at
   * the real entry (stack reset + vblank spin run byte-exact). */
  if (interp_bridge_in_lle_scheduler())
    return mmx_lle_unwind_to(cpu, 0x8099);
  (void)cpu;
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxSchedulerDispatch(CpuState *cpu) {
  /* $00:80E6 — asm scheduler's state-1 fresh-dispatch site
   * `JMP ($0032,X)`. The decoder inlines this dispatch into every
   * caller-body that BRAs through the slot loop. Each inlined copy
   * tail-calls into here.
   *
   * LLE: hand control back to the interpreter at the real dispatch
   * site — X carries whatever the guest flow left (matching hardware),
   * and the interpreted JMP ($0032,X) enters the slot natively.
   *
   * HLE: treat as "task wants to be re-scheduled" — yield with
   * countdown=1 so the C-host scheduler picks up the next iteration.
   * No JSR-frame contract at this yield, so the serializable resume
   * capture is skipped (a state saved in this suspension restarts the
   * task at entry on load). */
  if (interp_bridge_in_lle_scheduler())
    return mmx_lle_unwind_to(cpu, 0x80E6);
  (void)cpu;
  extern uint8_t g_yield_captures_resume;
  g_yield_captures_resume = 0;
  mmx_host_yield(1);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxTaskDie(CpuState *cpu) {
  /* $00:80F8 — asm yield-and-die path. A task JMP $80F8s to mark its
   * slot empty and hand off to the scheduler.
   *
   * LLE: interpret the real tail (SEP #$30 ; LDX $A0 ; STZ $30,X ;
   * BRA $80B9) — the slot clears and the walk continues natively; the
   * dead task's abandoned stack is reclaimed by the next dispatch's
   * TCS / the loop's TXS, exactly as on hardware.
   *
   * HLE: the asm tail iterates slots and inlines an asm coroutine-
   * resume (TCS+PLP+PLY+PLX+RTS) that's incompatible with our C-host
   * fiber model. HLE the whole tail with a clean "task done" signal:
   * mark this slot's state=0 directly (mirroring the asm `STZ $30,X`
   * it begins with) then just return — the recompiled body returns
   * into mmx_fiber_entry which sets g_slot_done and switches back to
   * the scheduler. */
  if (interp_bridge_in_lle_scheduler())
    return mmx_lle_unwind_to(cpu, 0x80F8);
  (void)cpu;
  uint8_t x = g_mmx_task_slot_x;
  /* Mirror the asm: STZ $30,X clears state for the current slot. */
  g_ram[(0x30 + x) & 0xFFFF] = 0x00;
  return RECOMP_RETURN_NORMAL;
}
