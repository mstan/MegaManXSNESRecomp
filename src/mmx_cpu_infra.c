#include "common_cpu_infra.h"
#include "mmx_rtl.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &MmxRunOneFrameOfGame,
  .draw_ppu_frame = &MmxDrawPpuFrame,
  .save_name_prefix = "save",
};
