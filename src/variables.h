/* Game-specific RAM variable declarations for Mega Man X.
 *
 * Scaffold: just the framework-protocol variables the runner expects.
 * Populate with named MMX RAM regions as they're identified (DP-slot
 * vars, sprite-table bases, etc.) — for now the recompiled gen code
 * refers to memory by raw address.
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include "types.h"

/* g_ram is declared by snesrecomp/runner/src/common_rtl.h. */

/* Host-protocol frame counters, populated by the orchestration in
 * mmx_rtl.c. Framework-shaped, not game-specific. */
extern uint16 counter_global_frames;

/* MMX uses $7E:001F as the NMI flag (the asm boots with the same
 * `LDA $1F ; BEQ -4` spinlock pattern as the other Capcom titles
 * branded under the original Mega Man X engineers). Host-side
 * orchestration mirrors I_NMI's `STZ $1F` to clear it. Verify the
 * address by inspecting the asm spinlock during first-boot diagnose
 * if symptoms suggest otherwise. */
#define waiting_for_vblank (*(uint8*)(g_ram + 0x1F))

#endif /* VARIABLES_H */
