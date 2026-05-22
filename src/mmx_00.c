#include "cpu_state.h"
#include "funcs.h"
#include "mmx_rtl.h"
#include "variables.h"

/* Host-protocol frame counter. Bumped by the per-frame orchestration
 * in mmx_rtl.c once an MMX-side equivalent of the asm main loop is
 * identified and wired up. Until then, just a counter the framework
 * expects to exist. */
uint16 counter_global_frames = 0;

/* Sprite OAM y-coord reset helper; identical to the SMW / Zelda glue.
 * Writes 0xF0 to slots [wh..127], hiding them off-screen. */
void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
