/* Native practice menu based on CF's Complete Edition / MottZilla's debugger. */
#include "mmx_complete.h"
#include "mmx_zero.h"
#include "mmx_coop.h"
#include "snes_overlay_draw.h"
#include "snes/ppu.h"
#include "common_rtl.h"
#include <string.h>

static MmxCompleteDebugState state;
MmxCompleteDebugState MmxCompleteDebugGetState(void) { return state; }
bool MmxCompleteDebugValidState(const MmxCompleteDebugState *s) {
  return s && s->held < 4096 && s->open < 2 && s->selection < 11 &&
      s->suppress < 2 && !s->reserved[0] && !s->reserved[1] && !s->reserved[2];
}
void MmxCompleteDebugSetState(const MmxCompleteDebugState *s) {
  if (MmxCompleteDebugValidState(s)) state = *s;
  else memset(&state, 0, sizeof(state));
}
void MmxCompleteDebugReset(void) { memset(&state, 0, sizeof(state)); }
static const uint8_t practice_rows[]={2,3,4,5,6,9,7,8,10};

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
  const unsigned chord = SNES_PAD_L | SNES_PAD_X | SNES_PAD_A;
  if (!state.open) {
    if (r[0xd1] != 2 || r[0xd2] != 4 || r[0xd3] != 4 ||
        (input & chord) != chord || !(pressed & chord)) return false;
    state.open = 1;state.selection=2;
    return true;
  }
  unsigned count=sizeof(practice_rows),index=0;const uint8_t *rows=practice_rows;
  for(unsigned i=0;i<count;++i)if(rows[i]==state.selection)index=i;
  if ((pressed & SNES_PAD_UP) && index) --index;
  if ((pressed & SNES_PAD_DOWN) && index+1<count) ++index;
  state.selection=rows[index];
  bool set = (pressed & SNES_PAD_Y) != 0, clear = (pressed & SNES_PAD_B) != 0;
  if (pressed & SNES_PAD_START || ((set || clear) && state.selection == 10)) {
    state.open = 0; state.suppress = 1; return true;
  }
  if (!set && !clear) return true;
  switch (state.selection) {
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
/* The stock BG3 options alphabet is already resident during gameplay. Read
 * its two bitplanes without changing VRAM, palettes or the paused scene. */
static void menu_text(uint32_t *p,int width,int x,int y,const char *text,unsigned tone) {
  static const uint32_t colors[][4]={
    {0,0xfff8f8f8,0xff70d8f8,0xff184878},
    {0,0xfffff8b0,0xfff8b830,0xff783808},
    {0,0xffa8c0b0,0xff689878,0xff284830}
  };
  for(unsigned n=0;text[n];++n) {
    unsigned c=(unsigned char)text[n];bool ink=false;
    if(g_ppu) for(int row=0;row<8;++row) {
      unsigned address=(PPU_bgTileAdr(g_ppu,2)+c*8+row)&0x7fff;
      unsigned planes=g_ppu->vram[address];
      for(int col=0;col<8;++col) {
        unsigned bits=planes>>(7-col),pixel=(bits&1)|((bits>>7)&2);
        int dx=x+(int)n*8+col,dy=y+row;
        ink|=pixel!=0;
        if(pixel && dx>=0 && dx<width && dy>=0 && dy<224)p[dy*width+dx]=colors[tone][pixel];
      }
    }
    if(!ink && c!=' ')snes_ovl_draw_char(p,width,224,x+(int)n*8,y,(char)c,colors[tone][1],1);
  }
}
static void menu_frame(uint32_t *p,int width,int left) {
  const uint32_t steel=0xffb8c8d8,light=0xfff0f8ff,blue=0xff3068b0,dark=0xff102838;
  /* Carry the pause screen's green field, cyan rails and purple end caps
   * across a fixed, readable menu panel at every adaptive viewport width. */
  for(int y=0;y<224;++y)for(int x=0;x<width;++x) {
    unsigned rib=(unsigned)x%32;
    p[y*width+x]=rib<2?0xff283038:rib<5?0xff485058:0xff182028;
  }
  snes_ovl_fill_rect(p,width,224,left,4,248,216,dark);
  snes_ovl_fill_rect(p,width,224,left+6,10,236,204,0xff287c40);
  snes_ovl_stroke_rect(p,width,224,left+2,6,244,212,light);
  snes_ovl_stroke_rect(p,width,224,left+3,7,242,210,steel);
  snes_ovl_stroke_rect(p,width,224,left+4,8,240,208,blue);
  snes_ovl_stroke_rect(p,width,224,left+5,9,238,206,dark);
  for(int y=12;y<212;y+=8) {
    snes_ovl_fill_rect(p,width,224,left+1,y,3,3,0xffb048a0);
    snes_ovl_fill_rect(p,width,224,left+244,y,3,3,0xffb048a0);
  }
  for(int side=0;side<2;++side)for(int end=0;end<2;++end) {
    int x=left+(side?236:6),y=end?206:10;
    snes_ovl_fill_rect(p,width,224,x,y,6,6,steel);
    snes_ovl_fill_rect(p,width,224,x+2,y+2,2,2,dark);
  }
  snes_ovl_fill_rect(p,width,224,left+12,12,224,31,0xff123c30);
  snes_ovl_fill_rect(p,width,224,left+8,44,232,1,light);
  snes_ovl_fill_rect(p,width,224,left+8,45,232,1,blue);
  snes_ovl_fill_rect(p,width,224,left+8,190,232,1,light);
  snes_ovl_fill_rect(p,width,224,left+8,191,232,1,blue);
}
bool MmxCompleteDebugDraw(uint32_t *p, int width, int height, const uint8_t *ram) {
  if (!MmxCompleteEnabled(MMX_COMPLETE_DEBUG) || !state.open || !p || width < 256 || height != 224) return false;
  static const char *const labels[]={"","","HELMET","ARM CANNON","BODY ARMOR","DASH BOOTS","REFILL ENERGY","FREE CAMERA","RESTART STAGE","GHOST MODE","RETURN TO GAME"};
  int left=(width-248)/2;menu_frame(p,width,left);
  menu_text(p,width,left+72,17,"PRACTICE MENU",1);
  menu_text(p,width,left+60,31,"COMPLETE EDITION",0);
  unsigned count=sizeof(practice_rows);const uint8_t *rows=practice_rows;
  for(unsigned i=0;i<count;++i) {
    unsigned row=rows[i];int y=54+(int)i*14;bool selected=state.selection==row;
    if(selected) {
      snes_ovl_fill_rect(p,width,224,left+10,y-2,228,12,0xff123c30);
      snes_ovl_stroke_rect(p,width,224,left+10,y-2,228,12,0xff70d8f8);
      for(int dx=0;dx<5;++dx)snes_ovl_fill_rect(p,width,224,left+14+dx,y+dx-1,1,9-dx*2,0xfff8d048);
    }
    menu_text(p,width,left+26,y,labels[row],selected?1:0);
    if(ram && ((row>=2 && row<=5) || row==9)) {
      bool on=row==9?(ram[0xbd8]&8)!=0:(ram[0x1f99]&(1u<<(row-2)))!=0;
      menu_text(p,width,left+204,y,on?"ON":"OFF",on?1:2);
    }
  }
  menu_text(p,width,left+20,177,state.selection==9?"NO ENEMY CONTACT DAMAGE":"UP/DOWN: SELECT",0);
  menu_text(p,width,left+20,198,"Y: SET  B: CLEAR",1);
  menu_text(p,width,left+72,208,"START: RETURN",0);
  return true;
}
