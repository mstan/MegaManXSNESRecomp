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
extern RecompReturn Task_B091_M1X1(CpuState *cpu);
extern RecompReturn Task_B25B_M1X1(CpuState *cpu);
extern RecompReturn Task_B38D_M1X1(CpuState *cpu);
extern RecompReturn Task_B436_M1X1(CpuState *cpu);
extern RecompReturn Task_E6B1_M1X1(CpuState *cpu);

static RecompReturn mmx_dispatch_task_pc(CpuState *cpu, uint16_t pc) {
  switch (pc) {
    case 0x852C: return Task0_M1X1(cpu);
    case 0xB091: return Task_B091_M1X1(cpu);
    case 0xB25B: return Task_B25B_M1X1(cpu);
    case 0xB38D: return Task_B38D_M1X1(cpu);
    case 0xB436: return Task_B436_M1X1(cpu);
    case 0xE6B1: return Task_E6B1_M1X1(cpu);
    default:
      fprintf(stderr, "[mmx_sched] unknown task PC $00:%04X (slot=$%02X)\n",
              pc, g_mmx_task_slot_x);
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
static uint8_t g_slot_done[MMX_NSLOTS] = {0};
static uint8_t g_slot_yield_cd[MMX_NSLOTS] = {0};
/* Saved cpu->S per slot. Restored at resume time so each fiber sees
 * its own 65816 emulated stack window ($013F, $017F, $01BF, $01FF,
 * $023F, $027F, $02BF per the asm $8067 table). Without this, all
 * fibers share the global cpu->S and emulated PHA/PHX writes from
 * one task corrupt other tasks' DP slot fields ($30-$9F). */
static uint16_t g_slot_saved_s[MMX_NSLOTS] = {0};
static uint16_t g_saved_scheduler_s = 0x02FF;
static uint8_t g_current_slot_idx = 0xFF;

static void CALLBACK mmx_fiber_entry(void *param) {
  uint8_t slot_idx = (uint8_t)(uintptr_t)param;
  uint16_t pc = g_slot_fiber_pc[slot_idx];
  /* Run the recomp'd task body to completion. The body MUST yield via
   * HleMmxYieldOneFrame/HleMmxYieldNFrames (which SwitchToFiber back
   * to the scheduler); when it ultimately RTSes here, the task is
   * done and the slot becomes empty. */
  g_mmx_task_slot_x = (uint8_t)(slot_idx << 4);
  mmx_dispatch_task_pc(&g_cpu, pc);
  /* Body returned: task is finished. Mark slot empty, then bounce
   * back to the scheduler. The scheduler will see g_slot_done and
   * destroy the fiber. */
  g_slot_done[slot_idx] = 1;
  for (;;) {
    SwitchToFiber(g_scheduler_fiber);
  }
}

void mmx_host_yield(uint8_t countdown) {
  /* Called from HleMmxYield* (via gen_stubs.c). We're inside a slot
   * fiber. Mark the slot's WRAM state to "delayed cd=N" so the
   * scheduler knows when to re-dispatch, then switch back. The
   * fiber's stack is preserved across the switch, so the next
   * SwitchToFiber-back resumes right after the yield call.
   *
   * Save cpu->S so the scheduler can restore each slot's own
   * emulated 65816 stack window on resume — without this all
   * fibers share one S and emulated PHA writes from one task
   * trample other tasks' DP $30-$9F slot fields. */
  uint8_t slot_idx = g_current_slot_idx;
  uint8_t x = (uint8_t)(slot_idx << 4);
  g_ram[(0x30 + x) & 0xFFFF] = 0x02;
  g_ram[(0x31 + x) & 0xFFFF] = countdown;
  g_slot_yield_cd[slot_idx] = countdown;
  g_slot_saved_s[slot_idx] = g_cpu.S;
  SwitchToFiber(g_scheduler_fiber);
  /* Resume: restore cpu->S for this slot. */
  g_cpu.S = g_slot_saved_s[slot_idx];
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
  bool dbg = (s_tick_n < 8);
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
    if (state == 0x00 || state == 0x03) continue;  /* empty / running */
    if (state == 0x02) {
      uint8_t cd = g_ram[(0x31 + x) & 0xFFFF];
      if (cd > 1) {
        g_ram[(0x31 + x) & 0xFFFF] = cd - 1;
        continue;
      }
      /* countdown hit zero: fall through to resume */
    }
    /* state == 1 (initial) or state == 2 + cd==0 (resume) */
    uint16_t handler = (uint16_t)g_ram[(0x32 + x) & 0xFFFF]
                     | ((uint16_t)g_ram[(0x33 + x) & 0xFFFF] << 8);
    if (g_slot_fiber[slot_idx] == NULL || g_slot_fiber_pc[slot_idx] != handler) {
      /* Tear down stale fiber (if any) — handler PC changed, this is
       * a fresh install rather than a resume. */
      if (g_slot_fiber[slot_idx] != NULL) {
        DeleteFiber(g_slot_fiber[slot_idx]);
        g_slot_fiber[slot_idx] = NULL;
      }
      g_slot_fiber_pc[slot_idx] = handler;
      g_slot_done[slot_idx] = 0;
      /* 1 MiB stack per fiber — recomp'd bodies can recurse deeply. */
      g_slot_fiber[slot_idx] = CreateFiber(
          1024 * 1024, mmx_fiber_entry, (void*)(uintptr_t)slot_idx);
      if (g_slot_fiber[slot_idx] == NULL) {
        fprintf(stderr, "[mmx_sched] CreateFiber slot=%u failed gle=%lu\n",
                slot_idx, GetLastError());
        abort();
      }
      if (dbg) fprintf(stderr, "  -> CREATE+enter slot=%u pc=$%04X\n",
                       slot_idx, handler);
    } else {
      if (dbg) fprintf(stderr, "  -> RESUME slot=%u pc=$%04X\n",
                       slot_idx, handler);
    }
    g_ram[(0x30 + x) & 0xFFFF] = 0x03;  /* mark running */
    g_ram[0x00A0] = x;
    /* Save scheduler S; install slot S. Each slot keeps its own
     * emulated 65816 stack window (the asm $8067 table seeds entry-S
     * = $013F, $017F, $01BF, $01FF, $023F, $027F, $02BF for slots 0..6). */
    g_saved_scheduler_s = g_cpu.S;
    if (g_slot_saved_s[slot_idx] == 0) {
      /* Fresh fiber: load entry-S from slot's $36/$37 (asm convention). */
      g_slot_saved_s[slot_idx] = (uint16_t)g_ram[(0x36 + x) & 0xFFFF]
                               | ((uint16_t)g_ram[(0x37 + x) & 0xFFFF] << 8);
    }
    g_cpu.S = g_slot_saved_s[slot_idx];
    SwitchToFiber(g_slot_fiber[slot_idx]);
    /* Slot suspended or done — save its S, restore scheduler's. */
    g_slot_saved_s[slot_idx] = g_cpu.S;
    g_cpu.S = g_saved_scheduler_s;
    if (g_slot_done[slot_idx]) {
      DeleteFiber(g_slot_fiber[slot_idx]);
      g_slot_fiber[slot_idx] = NULL;
      g_slot_fiber_pc[slot_idx] = 0;
      g_slot_done[slot_idx] = 0;
      g_slot_saved_s[slot_idx] = 0;
      g_ram[(0x30 + x) & 0xFFFF] = 0x00;
      if (dbg) fprintf(stderr, "  <- slot=%u DONE\n", slot_idx);
    } else {
      if (dbg) fprintf(stderr, "  <- slot=%u yielded cd=%u\n",
                       slot_idx, g_slot_yield_cd[slot_idx]);
    }
  }
  g_current_slot_idx = 0xFF;
}

void MmxDrawPpuFrame(void) {
  SimpleHdma hdma_chans[3];

  Dma *dma = g_dma;

  /* Scaffold: kick HDMA with the SNES WRAM mirror byte of HDMAEN.
   * MMX writes its desired channel mask to $7E:0033 during the NMI
   * handler before STA $420C; tap the same value here. Tighten this
   * once the real WRAM mirror address is identified from the asm. */
  dma_startDma(dma, *(uint8*)(g_ram + 0x0033), true);

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
    if (s_diag_frames < 5) {
      fprintf(stderr, "  [pre-NMI ] slot0 state=$%02X cd=$%02X pc=$%02X%02X\n",
              g_ram[0x30], g_ram[0x31], g_ram[0x33], g_ram[0x32]);
    }
    g_snes->inNmi = true;
    I_NMI(&g_cpu);
    cpu_trace_px_breadcrumb(&g_cpu, 0x2001, "after_I_NMI");
    if (s_diag_frames < 5) {
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
}

