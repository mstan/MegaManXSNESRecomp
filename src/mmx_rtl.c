#include "mmx_rtl.h"
#include "variables.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "funcs.h"
#include "debug_server.h"
#include "cpu_trace.h"
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <stdlib.h>

/* C-host implementation of the MMX cooperative task scheduler at
 * $00:8099. Replaces the asm dispatch loop with a C function that:
 *   - walks the 7 task slots ($30/40/50/60/70/80/90) once per host
 *     frame;
 *   - for state-1 slots: marks state=3, sets cpu->S to the slot's
 *     entry-S value at $36/$37, and calls the task's entry PC
 *     (looked up against the cfg-named function table);
 *   - for state-2 slots: decrements countdown at $31; on zero, marks
 *     state=3 and re-enters the task via its entry PC (restart-from-
 *     entry semantics; the task's own progress flag at $7E:FFFF
 *     drives forward progress across restarts).
 *
 * Yields ($00:8100 / $810C) longjmp back via g_mmx_task_jmp into
 * the per-slot setjmp below. */
jmp_buf g_mmx_task_jmp;             /* legacy — kept for build compat */
uint8_t g_mmx_task_slot_x;
uint8_t g_mmx_task_yield_countdown;

/* Task-entry handler table. Map handler PC (16-bit) -> C function ptr
 * for the recomp'd task body. */
extern RecompReturn Task0_M1X1(CpuState *cpu);
extern RecompReturn Task_89C9_M1X1(CpuState *cpu);
extern RecompReturn Task_B091_M1X1(CpuState *cpu);
extern RecompReturn Task_B25B_M1X1(CpuState *cpu);
extern RecompReturn Task_B38D_M1X1(CpuState *cpu);
extern RecompReturn Task_B436_M1X1(CpuState *cpu);
extern RecompReturn Task_E6B1_M1X1(CpuState *cpu);

static int mmx_rtl_diag_enabled(void);

static RecompReturn mmx_dispatch_task_pc(CpuState *cpu, uint16_t pc) {
  switch (pc) {
    case 0x852C: return Task0_M1X1(cpu);
    case 0x89C9: return Task_89C9_M1X1(cpu);
    case 0xB091: return Task_B091_M1X1(cpu);
    case 0xB25B: return Task_B25B_M1X1(cpu);
    case 0xB38D: return Task_B38D_M1X1(cpu);
    case 0xB436: return Task_B436_M1X1(cpu);
    case 0xE6B1: return Task_E6B1_M1X1(cpu);
    default:
      if (mmx_rtl_diag_enabled()) {
        fprintf(stderr, "[mmx_sched] unknown task PC $00:%04X (slot=$%02X)\n",
                pc, g_mmx_task_slot_x);
      }
      return RECOMP_RETURN_NORMAL;
  }
}

/* ── Host-fiber-based cooperative scheduler ──────────────────────────
 *
 * The asm cooperative scheduler at $00:8099 uses 65816 TCS+RTS tricks
 * to save/restore each task's CPU state at yield points. The recomp'd
 * task bodies are normal C functions — when YieldOneFrame longjmps
 * out, the host C stack is destroyed and the next dispatch restarts
 * the task from entry, which loses progress.
 *
 * Solution: each task slot runs on its own Windows fiber. The host
 * C stack is preserved per fiber, so yielding (SwitchToFiber back to
 * the scheduler) and later resuming (SwitchToFiber back to the slot)
 * picks up exactly after the yield call site in the recompiled body.
 *
 * Lifecycle: a fiber is born the first time a slot dispatches at a
 * given (pc, slot) pair. It dies when the recompiled body RTSes back
 * through the fiber-entry thunk; the thunk then marks the slot empty
 * and switches back to the scheduler.
 *
 * Mid-fiber the recomp'd body may install OTHER tasks (via the asm
 * $813B installer, which runs as ordinary recompiled C). Those new
 * slots get their own fiber on first scheduler iteration. */
#define MMX_NSLOTS 7

static void *g_scheduler_fiber = NULL;
static void *g_slot_fiber[MMX_NSLOTS] = {0};
static uint16_t g_slot_fiber_pc[MMX_NSLOTS] = {0};
static uint8_t g_slot_prev_state[MMX_NSLOTS] = {0};
static uint8_t g_slot_done[MMX_NSLOTS] = {0};
static uint8_t g_slot_yield_cd[MMX_NSLOTS] = {0};
/* Saved CpuState per slot. The C-host fiber preserves the C stack but
 * g_cpu is global — NMI/IRQ/other tasks mutate cpu->m_flag, cpu->x_flag,
 * cpu->A, cpu->X, cpu->Y, cpu->DB, cpu->D, cpu->P, etc. between
 * SwitchToFibers. Without per-slot save/restore, a task that yielded
 * with (m=1, x=1) resumes seeing whatever the LAST runner left
 * (typically m=0, x=0 from NMI's REP #$38 entry), and runtime-flag-
 * gated code (DEX width, push/pop width, branch widths) executes with
 * WRONG widths. Concrete bite: Task0's $8A4F OAM-Y init loop expected
 * x=1 (64 iterations writing $E0 to $0701/$0801 only) but ran with x=0
 * (16K iterations wrapping through all of WRAM, corrupting DP $39 to
 * $E0 and breaking the $8C64 state machine that reads it).
 *
 * `saved` is set on first save; before that the slot's CpuState is
 * uninitialised and entry-S is seeded from asm $86:8067 table at
 * first dispatch. */
typedef struct MmxSlotCpuSave {
    uint8_t  saved;
    uint16_t A, X, Y, S, D;
    uint8_t  DB, PB, P;
    uint8_t  m_flag, x_flag, emulation;
    uint8_t  _flag_N, _flag_V, _flag_Z, _flag_C, _flag_I, _flag_D;
} MmxSlotCpuSave;

static MmxSlotCpuSave g_slot_saved_state[MMX_NSLOTS] = {0};
static MmxSlotCpuSave g_saved_scheduler_state = {0};
static uint8_t g_current_slot_idx = 0xFF;

static int mmx_rtl_diag_enabled(void) {
  static int s_init = 0;
  static int s_enabled = 0;
  if (!s_init) {
    const char *v = getenv("MMX_RTL_DIAG");
    s_enabled = (v && v[0] && v[0] != '0');
    s_init = 1;
  }
  return s_enabled;
}

static inline uint16_t mmx_slot_handler_from_ram(uint8_t x) {
    return (uint16_t)g_ram[(0x32 + x) & 0xFFFF]
         | ((uint16_t)g_ram[(0x33 + x) & 0xFFFF] << 8);
}

static inline void mmx_save_cpu(MmxSlotCpuSave *s, const CpuState *c) {
    s->A = c->A; s->X = c->X; s->Y = c->Y; s->S = c->S; s->D = c->D;
    s->DB = c->DB; s->PB = c->PB; s->P = c->P;
    s->m_flag = c->m_flag; s->x_flag = c->x_flag;
    s->emulation = c->emulation;
    s->_flag_N = c->_flag_N; s->_flag_V = c->_flag_V;
    s->_flag_Z = c->_flag_Z; s->_flag_C = c->_flag_C;
    s->_flag_I = c->_flag_I; s->_flag_D = c->_flag_D;
    s->saved = 1;
}
static inline void mmx_restore_cpu(CpuState *c, const MmxSlotCpuSave *s) {
    c->A = s->A; c->X = s->X; c->Y = s->Y; c->S = s->S; c->D = s->D;
    c->DB = s->DB; c->PB = s->PB; c->P = s->P;
    c->m_flag = s->m_flag; c->x_flag = s->x_flag;
    c->emulation = s->emulation;
    c->_flag_N = s->_flag_N; c->_flag_V = s->_flag_V;
    c->_flag_Z = s->_flag_Z; c->_flag_C = s->_flag_C;
    c->_flag_I = s->_flag_I; c->_flag_D = s->_flag_D;
}

extern const char *g_last_recomp_func;
extern int snes_frame_counter;
#if SNESRECOMP_TRACE
extern uint8_t g_boundary_frozen;
extern StackDriftTripwire g_stack_drift_tripwire;  /* declared in cpu_trace.h (already included) */
#endif

static void CALLBACK mmx_fiber_entry(void *param) {
  uint8_t slot_idx = (uint8_t)(uintptr_t)param;
  uint16_t pc = g_slot_fiber_pc[slot_idx];
  /* Run the recomp'd task body to completion. The body MUST yield via
   * HleMmxYieldOneFrame/HleMmxYieldNFrames (which SwitchToFiber back
   * to the scheduler); when it ultimately RTSes here, the task is
   * done and the slot becomes empty. */
  g_mmx_task_slot_x = (uint8_t)(slot_idx << 4);
  mmx_dispatch_task_pc(&g_cpu, pc);
  /* Body returned: task is finished. Diagnostic: log the death so we
   * can correlate which task PC died at which frame and what the last
   * recompiled function on the stack was. Freeze both rings ONCE for
   * slot 0 so we can inspect the call path leading up to Task0's
   * unexpected return — the trace freeze is auto-triggered via the
   * stack-drift mechanism (which also freezes cpu_trace via
   * capture()'s check). */
  if (mmx_rtl_diag_enabled()) {
    fprintf(stderr, "[fiber_die] frame=%d slot=%u pc=$%04X last_func=%s A=$%04X X=$%04X Y=$%04X S=$%04X D=$%04X DB=$%02X PB=$%02X m=%d x=%d\n",
            snes_frame_counter, slot_idx, pc,
            g_last_recomp_func ? g_last_recomp_func : "?",
            g_cpu.A, g_cpu.X, g_cpu.Y, g_cpu.S, g_cpu.D,
            g_cpu.DB, g_cpu.PB, g_cpu.m_flag, g_cpu.x_flag);
    fflush(stderr);
  }
#if SNESRECOMP_TRACE
  if (mmx_rtl_diag_enabled() && slot_idx == 0 && !g_stack_drift_tripwire.triggered) {
    g_stack_drift_tripwire.triggered = 1;  /* freezes cpu_trace */
    g_boundary_frozen = 1;                 /* freezes boundary ring */
    fprintf(stderr, "[fiber_die] froze rings for inspection\n");
    fflush(stderr);
  }
#endif
  /* Mark slot empty, then bounce back to the scheduler. The scheduler
   * will see g_slot_done and destroy the fiber. */
  g_slot_done[slot_idx] = 1;
  for (;;) {
    SwitchToFiber(g_scheduler_fiber);
  }
}

void mmx_host_yield(uint8_t countdown) {
  /* Called from HleMmxYield* (via gen_stubs.c). We're inside a slot
   * fiber. Save the full CpuState before SwitchToFiber so the scheduler
   * (and other tasks / NMI / IRQ) can mutate g_cpu freely without
   * trampling this slot's emulated 65816 state. Restore on resume. */
  uint8_t slot_idx = g_current_slot_idx;
  uint8_t x = (uint8_t)(slot_idx << 4);
  g_ram[(0x30 + x) & 0xFFFF] = 0x02;
  g_ram[(0x31 + x) & 0xFFFF] = countdown;
  g_slot_yield_cd[slot_idx] = countdown;
  mmx_save_cpu(&g_slot_saved_state[slot_idx], &g_cpu);
#if SNESRECOMP_TRACE
  /* DIAG: track slot-0 yield-time cpu->S across frames. A persistent
   * downward drift = a per-frame stack leak in slot-0's foreground run
   * (the in-stage heavy-load softlock root). Freeze the boundary ring at
   * the first CLEAN drift onset (S still high in page 1) so the leaking
   * frame's complete events are captured for offline analysis. */
  if (slot_idx == 0 && mmx_rtl_diag_enabled()) {
    static uint16_t s_prev_s0 = 0; static int s_prev_frame = -1;
    if (s_prev_frame >= 0 && g_cpu.S != s_prev_s0) {
      int dd = (int)g_cpu.S - (int)s_prev_s0;
      fprintf(stderr, "[s0drift] frame=%d S=$%04X prevS=$%04X d=%+d\n",
              snes_frame_counter, g_cpu.S, s_prev_s0, dd);
      fflush(stderr);
      if (dd < 0 && g_cpu.S > 0x0150 && !g_boundary_frozen) {
        g_boundary_frozen = 1;
        fprintf(stderr, "[s0drift] froze boundary ring at clean drift onset (frame %d)\n",
                snes_frame_counter);
        fflush(stderr);
      }
    }
    s_prev_s0 = g_cpu.S; s_prev_frame = snes_frame_counter;
  }
#endif
  SwitchToFiber(g_scheduler_fiber);
  /* Resume: restore the full CpuState for this slot. */
  mmx_restore_cpu(&g_cpu, &g_slot_saved_state[slot_idx]);
}

void MmxSchedulerTick(void) {
  /* Promote main thread to a fiber on first call. */
  if (g_scheduler_fiber == NULL) {
    g_scheduler_fiber = ConvertThreadToFiber(NULL);
    if (g_scheduler_fiber == NULL) {
      fprintf(stderr, "[mmx_sched] ConvertThreadToFiber failed gle=%lu\n",
              GetLastError());
      abort();
    }
  }
  /* NMI handler has already run; $0B9D is $FF. Bump frame counter. */
  ++(*(uint8_t*)(g_ram + 0x0B9B));
  static uint32_t s_tick_n = 0;
  bool dbg = mmx_rtl_diag_enabled() && (s_tick_n < 8);
  if (dbg) {
    fprintf(stderr, "[tick %u] slots: ", s_tick_n);
    for (uint8_t x = 0x00; x < 0x70; x += 0x10) {
      uint8_t st = g_ram[(0x30 + x) & 0xFFFF];
      uint8_t cd = g_ram[(0x31 + x) & 0xFFFF];
      uint16_t pc = g_ram[(0x32 + x) & 0xFFFF] | ((uint16_t)g_ram[(0x33 + x) & 0xFFFF] << 8);
      fprintf(stderr, "s%u(st=%u cd=%u pc=$%04X%s) ", x >> 4, st, cd, pc,
              g_slot_fiber[x >> 4] ? "*" : "");
    }
    fprintf(stderr, "\n");
  }
  s_tick_n++;
  for (uint8_t x = 0x00; x < 0x70; x += 0x10) {
    uint8_t slot_idx = x >> 4;
    g_mmx_task_slot_x = x;
    g_current_slot_idx = slot_idx;
    uint8_t state = g_ram[(0x30 + x) & 0xFFFF];
    if (state == 0x00 || state == 0x03) {  /* empty / running */
      g_slot_prev_state[slot_idx] = state;
      continue;
    }
    if (state == 0x02) {
      uint8_t cd = g_ram[(0x31 + x) & 0xFFFF];
      if (cd > 1) {
        g_ram[(0x31 + x) & 0xFFFF] = cd - 1;
        g_slot_prev_state[slot_idx] = state;
        continue;
      }
      /* countdown hit zero: fall through to resume */
    }
    /* state == 1 (initial) or state == 2 + cd==0 (resume) */
    bool fresh_install = (state == 0x01 && g_slot_prev_state[slot_idx] != 0x01);
    uint16_t handler = fresh_install ? mmx_slot_handler_from_ram(x)
                                     : g_slot_fiber_pc[slot_idx];
    if (fresh_install) {
      /* $00:813B just installed this slot. This is the scheduler's
       * only authoritative read of the handler bytes; between
       * dispatches MMX reuses $32/$33 as scratch. */
      if (g_slot_fiber[slot_idx] != NULL) {
        DeleteFiber(g_slot_fiber[slot_idx]);
        g_slot_fiber[slot_idx] = NULL;
      }
      g_slot_fiber_pc[slot_idx] = handler;
      g_slot_done[slot_idx] = 0;
      g_slot_saved_state[slot_idx].saved = 0;
    }
    if (g_slot_fiber[slot_idx] == NULL) {
      if (handler == 0) {
        handler = mmx_slot_handler_from_ram(x);
        g_slot_fiber_pc[slot_idx] = handler;
        if (mmx_rtl_diag_enabled()) {
          fprintf(stderr, "[mmx_sched] slot=%u state=$%02X missing cached handler; using ram pc=$%04X\n",
                  slot_idx, state, handler);
          fflush(stderr);
        }
      }
      /* 1 MiB stack per fiber — recomp'd bodies can recurse deeply. */
      g_slot_fiber[slot_idx] = CreateFiber(
          1024 * 1024, mmx_fiber_entry, (void*)(uintptr_t)slot_idx);
      if (g_slot_fiber[slot_idx] == NULL) {
        fprintf(stderr, "[mmx_sched] CreateFiber slot=%u failed gle=%lu\n",
                slot_idx, GetLastError());
        abort();
      }
      if (mmx_rtl_diag_enabled()) {
        fprintf(stderr, "[fiber_new] frame=%d slot=%u pc=$%04X\n",
                snes_frame_counter, slot_idx, handler);
        fflush(stderr);
      }
    } else {
      if (dbg) fprintf(stderr, "  -> RESUME slot=%u pc=$%04X\n",
                       slot_idx, handler);
    }
    g_ram[(0x30 + x) & 0xFFFF] = 0x03;  /* mark running */
    g_ram[0x00A0] = x;
    /* Save scheduler's full CpuState, install slot's. On first
     * dispatch the slot's saved state is uninitialised — seed S from
     * the asm $86:8067 table ($013F, $017F, $01BF, $01FF, $023F,
     * $027F, $02BF for slots 0..6) and carry the rest of the current
     * CpuState forward (ResetHandler's m=1, x=1, DB=$86, PB=$80 etc.
     * are the post-reset baseline). */
    mmx_save_cpu(&g_saved_scheduler_state, &g_cpu);
    if (!g_slot_saved_state[slot_idx].saved) {
      mmx_save_cpu(&g_slot_saved_state[slot_idx], &g_cpu);
      g_slot_saved_state[slot_idx].S = (uint16_t)g_ram[(0x36 + x) & 0xFFFF]
                                     | ((uint16_t)g_ram[(0x37 + x) & 0xFFFF] << 8);
    }
    mmx_restore_cpu(&g_cpu, &g_slot_saved_state[slot_idx]);
    SwitchToFiber(g_slot_fiber[slot_idx]);
    /* Slot suspended or done — save its full CpuState, restore
     * scheduler's. */
    mmx_save_cpu(&g_slot_saved_state[slot_idx], &g_cpu);
    mmx_restore_cpu(&g_cpu, &g_saved_scheduler_state);
    if (g_slot_done[slot_idx]) {
      DeleteFiber(g_slot_fiber[slot_idx]);
      g_slot_fiber[slot_idx] = NULL;
      g_slot_fiber_pc[slot_idx] = 0;
      g_slot_done[slot_idx] = 0;
      g_slot_saved_state[slot_idx].saved = 0;
      g_ram[(0x30 + x) & 0xFFFF] = 0x00;
      if (dbg) fprintf(stderr, "  <- slot=%u DONE\n", slot_idx);
    } else {
      if (dbg) fprintf(stderr, "  <- slot=%u yielded cd=%u\n",
                       slot_idx, g_slot_yield_cd[slot_idx]);
    }
    g_slot_prev_state[slot_idx] = g_ram[(0x30 + x) & 0xFFFF];
  }
  g_current_slot_idx = 0xFF;
  /* Ack-clear NMI handshake flags so NMI's gated DMA path runs.
   *
   * Asm scheduler at $00:80C6 (and $80CC, slot-6 special) STZs $0B9D
   * after walking all 7 slots, then JMP $8099 back to the spinlock-
   * on-$0B9D. NMI sets $0B9D = $FF at $8193-$8195; tasks/IRQ are
   * expected to clear it before the next NMI's $8188 LDA $0B9D ;
   * ORA $0BA0 ; BNE $8193 check (which gates the JSR $8822 call) and
   * its mirror at $83FC LDA $0B9D ; ORA $0BA0 ; BNE $8421 inside the
   * $83F1 dispatch (which gates JSR $81E3 → CGRAM/OAM DMA + JSR $82C8
   * → VRAM DMA).
   *
   * $0BA0 is the IRQ-side ack flag — NMI also sets it $FF at $8198;
   * IRQ handler paths $84C6 / $84F5 / $851F STZ $0BA0. But IRQ enable
   * depends on $4200 = $B1 which is only set by NMI's $8458 path
   * ($8460 STA $4200) — and $8458 only runs if `$0B9D | $0BA0 == 0`.
   * Chicken-and-egg: without bootstrap ack-clearing both flags, IRQ
   * never enables, $0BA0 never clears, NMI's gated DMA never fires,
   * CGRAM stays empty, and the Capcom logo renders to a black screen.
   *
   * Bootstrap both in the C-host scheduler so the NMI/IRQ handshake
   * gets off the ground. Once IRQ is enabled (via NMI's $8460), the
   * asm IRQ handler resumes clearing $0BA0 naturally each frame. */
  g_ram[0x0B9D] = 0x00;
  g_ram[0x0BA0] = 0x00;
}

void MmxDrawPpuFrame(void) {
  SimpleHdma hdma_chans[3];

  Dma *dma = g_dma;

  /* Reinitialize HDMA from the actual $420C value written during NMI.
   * $7E:0033 is task-slot/sprite scratch in MMX and is not a stable
   * HDMAEN mirror. */
  dma_startDma(dma, g_snesrecomp_last_hdmaen, true);

  SimpleHdma_Init(&hdma_chans[0], &dma->channel[5]);
  SimpleHdma_Init(&hdma_chans[1], &dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[2], &dma->channel[7]);

  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;

  for (int i = 0; i <= 224; i++) {
    ppu_runLine(g_ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0]);
    SimpleHdma_DoLine(&hdma_chans[1]);
    SimpleHdma_DoLine(&hdma_chans[2]);
    //    dma_doHdma(snes->dma);
    if (i == trigger) {
      // Simulate hardware IRQ latch: I_IRQ's first instruction reads HW_TIMEUP
      // ($4211) and branches on the N flag to distinguish timer-IRQ from
      // other sources. recomp_hw.c's ReadReg(0x4211) returns g_snes->inIrq<<7
      // and clears the flag; assert it here so the handler takes the
      // timer-IRQ path instead of exiting immediately.
      g_snes->inIrq = true;
      /* Option-1 cpu->S ABI: model the hardware IRQ-entry push so the
       * handler's RTI has a frame to pop (paired with cpu_state.h's RTI
       * pop). Without it cpu->S drifts on every interrupt. */
      cpu_push_interrupt_frame(&g_cpu);
      I_IRQ(&g_cpu);
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;
    }
  }
}

void MmxRunOneFrameOfGame(void) {
  // First-call reset gate. Was previously `if (*(uint16*)$7F8000 == 0) I_RESET()`,
  // which silently relied on WRAM being zero-initialized at power-on. Real hardware
  // (and snes9x) power-on WRAM is 0x55, so that check would never fire and I_RESET
  // would be skipped, leaving $0100 (GameMode) at 0x55 — out-of-bounds for the
  // 42-entry dispatch table at PC 0x009329. Use a host-side bool instead so the
  // gate is independent of WRAM contents.
  static bool g_did_reset = false;
  static bool g_first_frame_done = false;
  if (!g_did_reset) {
    cpu_state_init(&g_cpu, g_ram);
    cpu_trace_px_breadcrumb(&g_cpu, 0x1000, "after_cpu_state_init");
    I_RESET(&g_cpu);
    cpu_trace_px_breadcrumb(&g_cpu, 0x1001, "after_I_RESET");
    g_did_reset = true;
  }
  cpu_trace_px_breadcrumb(&g_cpu, 0x2000, "before_NMI_or_Internal");
  // NMI handler runs BEFORE the main-loop game code each frame.
  //
  // On real hardware NMI fires at vblank start (between frames).
  // Its handler polls HW_JOY ($4218/$4219) into the $15-$18 mirror;
  // the next frame's game logic reads that mirror. Demo inputs are
  // applied INSIDE the main loop by overwriting $15/$16; if NMI's
  // poll runs LAST it clobbers the demo bytes with the empty
  // controller state ($00) and the end-of-frame mirror reads as 0.
  //
  // Per snes9x oracle trace at GM=07: emu's per-frame writer order
  // is poll($86B2/$86C1) → DamagePlayer($F62F/$F631) → GameMode07
  // demo-override($9C93/$9C9C); demo bytes are LAST and stick. With
  // recomp's prior `Internal(); auto_00_816A()` order, PollJoypad
  // ran last instead, leaving $15/$16=$00. End-of-frame snapshot
  // diverges from oracle, and demo timing skews because the
  // VariousPromptTimer / TitleInputIndex tick keys off observable
  // input state.
  //
  // Frame 0 is special: real hardware fires the first NMI AFTER
  // I_RESET completes AND the main loop has had time to set up flags
  // (notably SEP #$10 → x=1). If we run I_NMI before Internal on the
  // very first frame, I_NMI's PHP captures I_RESET-end's P (x=0); its
  // RTI then restores x=0 to the main loop. Subsequent ProcessGameMode
  // → UploadGraphicsFiles_Layer3 → TAY at $00:A9A5 then runs as 16-bit,
  // copying A's polluted high byte into Y, indexing past the GFX bank
  // table and writing $7E (instead of $0B) to $7E:008C. Skip I_NMI on
  // frame 0 so the order matches hardware: I_RESET → main loop →
  // (vblank) → I_NMI → main loop → ...
  // Assert NMI-pending so the recompiled NMI handler's read of $4210
  // (RDNMI) returns bit 7 = 1, matching real hardware. snes_readReg
  // clears the latch on read.
  if (g_first_frame_done) {
    static int s_diag_frames = 0;
    if (mmx_rtl_diag_enabled() && s_diag_frames < 5) {
      fprintf(stderr, "  [pre-NMI ] slot0 state=$%02X cd=$%02X pc=$%02X%02X\n",
              g_ram[0x30], g_ram[0x31], g_ram[0x33], g_ram[0x32]);
    }
#if SNESRECOMP_TRACE
    /* OAM-dropout detector (task #7): count on-screen sprites in the OAM
     * shadow ($0700 low table; Y is byte 1 of each 4-byte entry, off-screen
     * park = $E0). The transient dropout = sustained gameplay -> sudden
     * collapse to ~0 -> RECOVERY within a few frames. That recovery is what
     * distinguishes it from a scene transition (which stays low / changes
     * scene). Freeze the boundary ring only on a confirmed transient
     * collapse-then-recover, so the object-engine perturbation that built the
     * empty OAM (a frame or two before the collapse) is captured. */
    if (mmx_rtl_diag_enabled() && !g_boundary_frozen && snes_frame_counter > 1500) {
      static int s_prev = -1;       /* on-screen count last frame */
      static int s_drop_frame = -1; /* frame the collapse began (-1 = idle) */
      static int s_drop_prev = 0;   /* on-screen count just before collapse */
      int onscreen = 0;
      for (int i = 0; i < 128; i++)
        if (g_ram[(0x0700 + i * 4 + 1) & 0x1FFFF] < 0xE0) onscreen++;
      /* Snapshot the OAM-build gate vars so we can diff dropout vs normal:
       * $1F11 = OAM-build mode (JMP ($D7D8,X) index), $0BCF = build flag,
       * $1F9A = sprite limit, $E4/$E5 = build counters. */
      #define OAMDBG_VARS snes_frame_counter, onscreen, \
        g_ram[0x1F11], g_ram[0x0BCF], g_ram[0x1F9A], g_ram[0x00E4], g_ram[0x00E5]
      if (s_drop_frame < 0) {
        if (s_prev >= 70 && s_prev <= 128 && onscreen <= 8) {
          s_drop_frame = snes_frame_counter; s_drop_prev = s_prev;
          fprintf(stderr, "[oamvar] PRE  f=%d on=%d 1F11=%02X 0BCF=%02X 1F9A=%02X E4=%02X E5=%02X\n", OAMDBG_VARS);
          /* Dump the 128 OAM Y bytes so we see WHICH slots parked (X/HUD-
           * specific = low slots vs pool-overflow = high slots). */
          fprintf(stderr, "[oamY] DROP f=%d:", snes_frame_counter);
          for (int i = 0; i < 128; i++)
            fprintf(stderr, "%02X", g_ram[(0x0700 + i * 4 + 1) & 0x1FFFF]);
          fprintf(stderr, "\n");
          fflush(stderr);
        }
      } else if (onscreen >= 12) {
        fprintf(stderr, "[oamY] REC  f=%d:", snes_frame_counter);
        for (int i = 0; i < 128; i++)
          fprintf(stderr, "%02X", g_ram[(0x0700 + i * 4 + 1) & 0x1FFFF]);
        fprintf(stderr, "\n");
        fprintf(stderr, "[oamvar] REC  f=%d on=%d 1F11=%02X 0BCF=%02X 1F9A=%02X E4=%02X E5=%02X\n", OAMDBG_VARS);
        fprintf(stderr, "[oamdrop] TRANSIENT collapse@%d (%d->~0) recovered@%d ; froze ring\n",
                s_drop_frame, s_drop_prev, snes_frame_counter);
        fflush(stderr);
        g_boundary_frozen = 1;
      } else {
        fprintf(stderr, "[oamvar] DROP f=%d on=%d 1F11=%02X 0BCF=%02X 1F9A=%02X E4=%02X E5=%02X\n", OAMDBG_VARS);
        fflush(stderr);
        if (snes_frame_counter - s_drop_frame > 12) s_drop_frame = -1;
      }
      s_prev = onscreen;
    }
#endif
    g_snes->inNmi = true;
    /* Option-1 cpu->S ABI: model the hardware NMI-entry push so the
     * handler's RTI has a frame to pop (paired with cpu_state.h's RTI
     * pop). Without it cpu->S drifts on every interrupt. */
    cpu_push_interrupt_frame(&g_cpu);
    I_NMI(&g_cpu);
    cpu_trace_px_breadcrumb(&g_cpu, 0x2001, "after_I_NMI");
    if (mmx_rtl_diag_enabled() && s_diag_frames < 5) {
      fprintf(stderr, "  [post-NMI] slot0 state=$%02X cd=$%02X pc=$%02X%02X\n",
              g_ram[0x30], g_ram[0x31], g_ram[0x33], g_ram[0x32]);
      s_diag_frames++;
    }
  }
  cpu_trace_px_breadcrumb(&g_cpu, 0x2002, "before_Internal");
  /* Rearm the P.X tripwire here so the first x=1→0 transition INSIDE
   * Internal() (the main game loop) is captured fresh. The earlier
   * boot-time REP #$38 in I_RESET is expected and intentional; we only
   * want to know where x flips during ProcessGameMode dispatch. */
  /* Drive one frame of the cooperative task scheduler. NMI already
   * set $0B9D=$FF; the asm spinlock at $80A1 would short-circuit
   * but we skip it entirely because MmxSchedulerTick is the C
   * replacement for the entire $8099 main loop. */
  cpu_trace_arm_px_tripwire();
  waiting_for_vblank = 0xFF;
  MmxSchedulerTick();
  cpu_trace_px_breadcrumb(&g_cpu, 0x2003, "after_Internal");
  g_first_frame_done = true;
  /* Per-frame stage-load tracer snapshot (diagnostics; no-op in Production). */
  {
    extern void debug_server_loadin_tick(void);
    debug_server_loadin_tick();
  }
}
