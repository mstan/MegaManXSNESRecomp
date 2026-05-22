#include "cpu_state.h"
#include "funcs.h"
#include "mmx_rtl.h"
#include "variables.h"


void SmwRunOneFrameOfGame_Internal() {
  assert(waiting_for_vblank != 0);
  ++counter_global_frames;
  InitAndMainLoop_ProcessGameMode(&g_cpu);
  waiting_for_vblank = 0;
}

void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
