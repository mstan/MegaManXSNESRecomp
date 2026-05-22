#include "mmx_spc_player.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "types.h"
#include "snes/spc.h"
#include "snes/dsp_regs.h"

// MmxSpcPlayer is now a minimal SPC-image loader: it allocates a 64KB
// buffer for APU RAM, resets DSP state to sane defaults, and uploads
// SMW's ROM-packed audio image into that buffer. The HLE SPC executor
// that used to live here was ripped (Tier 3g); real audio now runs
// through the framework's SPC core in snesrecomp/runner/src/snes/apu.c.
typedef struct MmxSpcPlayer {
  SpcPlayer base;
  uint8 ram[65536];
} MmxSpcPlayer;

static void Dsp_Write(MmxSpcPlayer *p, uint8_t reg, uint8 value) {
  if (p->base.dsp)
    dsp_write(p->base.dsp, reg, value);
}

static const uint8 kDefDspRegs[12] = { MVOLL,MVOLR,EVOLL,EVOLR,FLG,EFB,PMON,NON,EON,DIR,ESA,EDL };
static const uint8 kDefDspValues[12] = { 0x7F, 0x7F,  0,  0, 0x2F, 0x60,  0,  0,  0, 0x80, 0x60, 2 };

static void Spc_Reset(MmxSpcPlayer *p) {
  memset(p->ram, 0, 0x500);
  memset(p->base.input_ports, 0, sizeof(p->base.input_ports));
  for (int i = 11; i >= 0; i--)
    Dsp_Write(p, kDefDspRegs[i], kDefDspValues[i]);
}

static void SmwSpcPlayer_Initialize(SpcPlayer *p_in) {
  MmxSpcPlayer *p = (MmxSpcPlayer *)p_in;
  dsp_reset(p->base.dsp);
  Spc_Reset(p);
}

static void SmwSpcPlayer_Upload(SpcPlayer *p_in, const uint8_t *data) {
  MmxSpcPlayer *p = (MmxSpcPlayer *)p_in;
  Dsp_Write(p, FLG, 0x60);
  Dsp_Write(p, KOF, 0xff);
  for (;;) {
    int numbytes = *(uint16 *)(data);
    if (numbytes == 0) {
      break;
    }
    int target = *(uint16 *)(data + 2);
    data += 4;
    do {
      p->ram[target++ & 0xffff] = *data++;
    } while (--numbytes);
  }
  p->base.port_to_snes[0] = p->base.port_to_snes[1] = p->base.port_to_snes[2] = p->base.port_to_snes[3] = 0;
  memset(p->base.input_ports, 0, sizeof(p->base.input_ports));
  Dsp_Write(p, FLG, 0x20);
}

SpcPlayer *SmwSpcPlayer_Create(void) {
  MmxSpcPlayer *p = (MmxSpcPlayer *)malloc(sizeof(MmxSpcPlayer));
  memset(p, 0, sizeof(MmxSpcPlayer));
  p->base.dsp = dsp_init(p->ram);
  p->base.initialize = &SmwSpcPlayer_Initialize;
  p->base.upload = &SmwSpcPlayer_Upload;
  return &p->base;
}
