/* gen_stubs.c — hand-written HLE bodies the recompiler couldn't (or
 * shouldn't) synthesise. For MMX these are the cooperative-task-
 * scheduler yield primitives: the asm wrappers at $00:8100 and
 * $00:810C set the slot's state to "delayed" and BRA into the
 * scheduler's next-slot loop. From the calling task's frame they
 * never return, so the C HLE longjmps back to the C-host scheduler
 * in mmx_rtl.c.
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
   * the caller (asm's JSR $8100) continues at the post-JSR site. */
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxYieldNFrames(CpuState *cpu) {
  /* $00:810C: caller passes a 16-bit countdown in A. */
  uint8_t n = (uint8_t)(cpu->A & 0xFF);
  if (n == 0) n = 1;
  mmx_host_yield(n);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxYieldVblank(CpuState *cpu) {
  /* $00:8121 conditional yield: `BIT $0B9D ; BMI yield ; RTS`. Returns
   * without yielding when $0B9D bit 7 is clear; otherwise yields with
   * cd=1, just like $8100. Caller's PHX/PHY/PHP/PHB at the JSR site
   * (e.g. Task_B25B at $B2CC-$B2CF) is popped by the caller's own
   * post-JSR PLB/PLP/PLY/PLX — we don't touch the emulated stack. */
  (void)cpu;
  uint8_t v = g_ram[0x0B9D];
  if (v & 0x80) {
    mmx_host_yield(1);
  }
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxMainLoopReturn(CpuState *cpu) {
  /* $00:8099 — asm main loop entry. Replaced wholesale by C-host
   * MmxSchedulerTick in mmx_rtl.c. ResetHandler's JMP $8099 tail-
   * calls here so ResetHandler becomes a returnable init-only fn. */
  (void)cpu;
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxSchedulerDispatch(CpuState *cpu) {
  /* $00:80E6 — asm scheduler's state-1 fresh-dispatch site
   * `JMP ($0032,X)`. The decoder inlines this dispatch into every
   * caller-body that BRAs through the slot loop. Each inlined copy
   * tail-calls into here; treat as "task wants to be re-scheduled"
   * — yield with countdown=1 so the C-host scheduler picks up
   * the next iteration. */
  (void)cpu;
  mmx_host_yield(1);
  return RECOMP_RETURN_NORMAL;
}

RecompReturn HleMmxTaskDie(CpuState *cpu) {
  /* $00:80F8 — asm yield-and-die path. A task JMP $80F8s to mark its
   * slot empty and hand off to the scheduler. The asm tail iterates
   * slots and inlines an asm coroutine-resume (TCS+PLP+PLY+PLX+RTS)
   * that's incompatible with our C-host fiber model. HLE the whole
   * tail with a clean "task done" signal: mark this slot's state=0
   * directly (mirroring the asm `STZ $30,X` it begins with) then
   * just return — the recompiled body returns into mmx_fiber_entry
   * which sets g_slot_done and switches back to the scheduler. */
  (void)cpu;
  uint8_t x = g_mmx_task_slot_x;
  /* Mirror the asm: STZ $30,X clears state for the current slot. */
  g_ram[(0x30 + x) & 0xFFFF] = 0x00;
  return RECOMP_RETURN_NORMAL;
}
