#include "common_cpu_infra.h"
#include "mmx_rtl.h"

/* MMX_VARIANT_JP is a per-target compile-def (RockmanXSNESRecomp sets =1;
 * the USA MegaManX target leaves it undefined). Guard so this file compiles
 * standalone and defaults to the USA identity. */
#ifndef MMX_VARIANT_JP
#define MMX_VARIANT_JP 0
#endif

/* Per-variant game identity. Title drives coverage-artifact naming
 * (tier2_<title>_*.json) so Mega Man X (USA) and Rockman X (JP) never
 * collide — was previously mislabeled "smw" (a clone-from-SMW leftover). */
const RtlGameInfo kMmxGameInfo = {
#if MMX_VARIANT_JP
  .title = "rockmanx",
#else
  .title = "mmx",
#endif
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &MmxDrawPpuFrame,
  .save_name_prefix = "save",
  /* .sav v5: persist the fiber scheduler's task-resume contexts and rebuild
   * the fibers after load — states become loadable from any game mode and
   * from a fresh process. */
  .state_save_extra = &MmxStateSaveExtra,
  .state_load_extra = &MmxStateLoadExtra,
  .on_state_loaded = &MmxOnStateLoaded,
};
