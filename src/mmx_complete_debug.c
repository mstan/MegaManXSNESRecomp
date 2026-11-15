/* Native practice menu based on CF's Complete Edition / MottZilla's debugger.
 * The native frame driver calls the stock editor's bounded editing routines. */
#include "mmx_complete.h"
#include "mmx_zero.h"
#include "mmx_coop.h"
#include "snes_overlay_draw.h"
#include "snes/interp_bridge.h"
#include "common_rtl.h"
#include <stdio.h>
#include <string.h>

static MmxCompleteDebugState state;
MmxCompleteDebugState MmxCompleteDebugGetState(void) { return state; }
bool MmxCompleteDebugValidState(const MmxCompleteDebugState *s) {
  return s && s->held < 4096 && s->open < 2 && s->selection < 11 &&
      s->map_pending < 2 && s->map_active < 2 && s->suppress < 2 && !s->reserved &&
      !(s->map_pending && s->map_active) && !(s->open && (s->map_pending || s->map_active));
}
void MmxCompleteDebugSetState(const MmxCompleteDebugState *s) {
  if (MmxCompleteDebugValidState(s)) state = *s;
  else memset(&state, 0, sizeof(state));
}
void MmxCompleteDebugReset(void) { memset(&state, 0, sizeof(state)); }
bool MmxCompleteDebugMapActive(void) {
  return MmxCompleteEnabled(MMX_COMPLETE_DEBUG) && (state.map_pending || state.map_active);
}

extern CpuState g_cpu;
/* The original editor is an interrupt-resident infinite loop. Drive its
 * bounded operations from the host frame boundary instead. Each call has a
 * private scratch stack, preserves the paused scheduler's registers/stack,
 * and commits its ordinary guest RAM/PPU changes and elapsed clocks. */
static bool editor_call(unsigned pc, unsigned d, unsigned y, bool long_call) {
  extern uint8_t g_ram[0x20000];
  uint8_t stack[256];memcpy(stack,g_ram+0x100,sizeof(stack));
  CpuState cpu=g_cpu;
  cpu.S=0x1ff;cpu.D=(uint16_t)d;cpu.DB=0x86;cpu.PB=(uint8_t)(pc>>16);
  cpu.X=0;cpu.Y=(uint16_t)y;cpu.P=0x30;cpu.emulation=0;cpu_p_to_mirrors(&cpu);
  if(long_call)cpu_push_jsl_return_frame(&cpu);else cpu_push_jsr_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  int ok=interp_bridge_run(&cpu,pc);
  memcpy(g_ram+0x100,stack,sizeof(stack));
  g_cpu.cycles=cpu.cycles;g_cpu.master_cycles=cpu.master_cycles;
  g_cpu.coprocessor_master_cycles=cpu.coprocessor_master_cycles;
  g_cpu.open_bus=cpu.open_bus;
  if(!ok || cpu.S!=0x1ff) {
    fprintf(stderr,"[complete-edition] Map editor operation $%06x did not return cleanly\n",pc);
    return false;
  }
  return true;
}
static void word(uint8_t *r,unsigned at,unsigned value) {r[at]=(uint8_t)value;r[at+1]=(uint8_t)(value>>8);}
static bool editor_frame(uint8_t *r,uint16_t input) {
  if(state.map_pending) {
    if(!editor_call(0x828011,0,0x12,true))return false;
    memset(r+0x1e08,0,4);word(r,0x1e1c,0);word(r,0x1e29,0);word(r,0x1e08,1);
    memcpy(r+0x1e0c,r+0x1e4d,2);memcpy(r+0x1e18,r+0x1e4d,2);
    memcpy(r+0x1e0f,r+0x1e50,2);memcpy(r+0x1e1a,r+0x1e50,2);
    memcpy(r+0x1e1e,r+0xb92,2);word(r,0x1e27,0x7080);r[0x1e20]=r[0xb94];
    r[0x1e16]=r[0xc0];r[0x1e17]=r[0xc1];r[0xc0]=0x17;r[0xc1]=0;r[0xa2]=4;
    cpu_write8(&g_cpu,0,0x2105,9);
    if(!editor_call(0x80d1db,0x1e08,0,true))return false;
    state.map_pending=0;state.map_active=1;
    return true; /* Let NMI publish the queued palette/tilemap before editing. */
  }
  static const unsigned handlers[]={0x8483d7,0x84845e,0x8484ab,0x848516,0x848500,0x8484ab,0x8485c2,0x848500,0x8483d7};
  unsigned mode=r[0x1e09];
  if((mode&1) || mode/2>=sizeof(handlers)/sizeof(handlers[0]))return false;
  if(!editor_call(handlers[mode/2],0x1e08,0,false) || !editor_call(0x84880f,0x1e08,0,false))return false;
  const unsigned exit=SNES_PAD_L|SNES_PAD_R|SNES_PAD_X;
  if((input&exit)==exit) {
    r[0xc0]=r[0x1e16];r[0xc1]=r[0x1e17];r[0xa2]=4;
    state.map_active=0;state.open=1;
  }
  return true;
}

/* Mirror the original upgrade refresh, retaining Zero's own body/animations.
 * Shared X1 upgrade flags remain the authority for the alternate character. */
static void upgrades(uint8_t *r) {
  if (MmxZeroActive()) return;
  static const unsigned clear[] = {0xbbe,0xc43,0xc38,0xc39,0xc4e,0xc63,0xc58,0xc59,0xc6e,0xc83,0xc78,0xc79,0xc8e};
  for (unsigned i = 0; i < sizeof(clear)/sizeof(clear[0]); ++i) r[clear[i]] = 0;
  if (r[0x1f99] & 1) r[0xbbe] = 0x18;
  if (r[0x1f99] & 2) { r[0xc38] = r[0xc39] = 1; r[0xc4e] = 0x5d; }
  if (r[0x1f99] & 4) { r[0xc63] = r[0xc58] = r[0xc59] = 1; r[0xc6e] = 0x5e; }
  if (r[0x1f99] & 8) { r[0xc83] = 2; r[0xc78] = r[0xc79] = 1; r[0xc8e] = 0x5f; }
}
bool MmxCompleteDebugFrame(uint8_t *r, uint16_t input) {
  if (!MmxCompleteEnabled(MMX_COMPLETE_DEBUG)) return false;
  input &= 4095;
  unsigned pressed = input & ~state.held;
  state.held = input;
  if (state.suppress) { if (!input) state.suppress = 0; return true; }
  if (state.map_pending || state.map_active) {
    if(!editor_frame(r,input)) {state.map_pending=state.map_active=0;state.open=1;}
    return true;
  }
  const unsigned chord = SNES_PAD_L | SNES_PAD_X | SNES_PAD_A;
  if (!state.open) {
    if (r[0xd1] != 2 || r[0xd2] != 4 || r[0xd3] != 4 ||
        (input & chord) != chord || !(pressed & chord)) return false;
    state.open = 1;
    return true;
  }
  if (pressed & SNES_PAD_UP) { if (state.selection) --state.selection; }
  if (pressed & SNES_PAD_DOWN) { if (state.selection < 10) ++state.selection; }
  bool set = (pressed & SNES_PAD_Y) != 0, clear = (pressed & SNES_PAD_B) != 0;
  if (pressed & SNES_PAD_START || ((set || clear) && state.selection == 10)) {
    state.open = 0; state.suppress = 1; return true;
  }
  if (!set && !clear) return true;
  switch (state.selection) {
    case 0:
      if (set) { state.map_pending = 1; state.open = 0; }
      break;
    case 1: break; /* Heading, as in the original menu. */
    case 2: case 3: case 4: case 5: {
      unsigned mask = 1u << (state.selection - 2);
      r[0x1f99] = (r[0x1f99] & ~mask) | (set ? mask : 0);
      upgrades(r); break;
    }
    case 6: r[0xbcf] = r[0x1f9a]; break;
    case 7: r[0x1e69] = r[0x1e5f] = 0; r[0x1e6f] = r[0x1e61] = 0x7f; break;
    case 8: r[0x1e5d] = 0; state.open = 0; state.suppress = 1; break;
    case 9: r[0xbd8] = (r[0xbd8] & ~8) | (set ? 8 : 0); break;
  }
  MmxCoopCapture(r);
  return true;
}
bool MmxCompleteDebugDraw(uint32_t *p, int width, int height, const uint8_t *ram) {
  if (!MmxCompleteEnabled(MMX_COMPLETE_DEBUG) || !state.open || !p || width < 256 || height != 224) return false;
  static const char *const rows[] = {"MAP EDITOR", "UPGRADES (Y SET / B CLEAR)", "HELMET", "ARM CANNON", "BODY ARMOR", "DASH BOOTS", "ENERGY REFILL", "FREE SCROLL", "RESTART", "GHOST MODE", "EXIT MENU"};
  for (int i = 0; i < width * height; ++i) p[i] = 0xff101820;
  int left = width / 2 - 120;
  snes_ovl_draw_text(p, width, height, left + 40, 16, "COMPLETE EDITION", 0xff88ddff, 1);
  for (unsigned i = 0; i < 11; ++i) {
    int y = 42 + (int)i * 13;
    if (state.selection == i) snes_ovl_draw_text(p, width, height, left, y, ">", 0xffffff80, 1);
    snes_ovl_draw_text(p, width, height, left + 12, y, rows[i], state.selection == i ? 0xffffff80 : 0xffffffff, 1);
    if (ram && ((i >= 2 && i <= 5) || i == 9)) {
      bool on = i == 9 ? (ram[0xbd8] & 8) != 0 : (ram[0x1f99] & (1u << (i - 2))) != 0;
      snes_ovl_draw_text(p, width, height, left + 208, y, on ? "ON" : "OFF", on ? 0xff88ffaa : 0xffa0a0a0, 1);
    }
  }
  snes_ovl_draw_text(p, width, height, left + 8, 198, "UP/DOWN  Y:SET  B:CLEAR", 0xff88ddff, 1);
  snes_ovl_draw_text(p, width, height, left + 40, 211, "START: RETURN", 0xff88ddff, 1);
  return true;
}
