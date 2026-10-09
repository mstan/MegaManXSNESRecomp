#include "mmx_boss_rush.h"
#include <stdio.h>
#include <string.h>

/* Five-bit rows, deliberately independent of stage VRAM/font residency. */
static const uint8_t font[36][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
 {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},
 {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
 {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
 {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static void box(uint32_t *p,int width,int x,int y,int w,int h,uint32_t color) {
  for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i)
    if(i>=0 && i<width && j>=0 && j<224) p[j*width+i]=color;
}
static void text(uint32_t *p,int width,int x,int y,const char *s,uint32_t color) {
  for(;*s;++s,x+=6) {
    int id=*s>='0' && *s<='9'?*s-'0':*s>='A' && *s<='Z'?*s-'A'+10:-1;
    if(id<0) continue;
    for(int row=0;row<7;++row) for(int col=0;col<5;++col)
      if(font[id][row]&(16>>col)) box(p,width,x+col,y+row,1,1,color);
  }
}
static void explosion(uint32_t *p,int width,int extra,const MmxBossRushBoss *b) {
  static const int directions[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
  unsigned tick=b->ticks;int radius=3+(int)(tick%8),travel=(int)tick*2;
  for(unsigned n=0;n<8;++n) {
    int x=extra+b->death_x+directions[n][0]*travel,y=b->death_y+directions[n][1]*travel;
    for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx)
      if(dx*dx+dy*dy<=radius*radius)
        box(p,width,x+dx,y+dy,1,1,dx*dx+dy*dy<radius*radius/2?0xffffffff:0xffffaa40);
  }
}
void MmxBossRushDraw(uint32_t *p,int width,int extra,const MmxBossRushState *s) {
  if(!p || !s || width<256) return;
  const uint32_t white=0xffffffff,yellow=0xffffdc60,black=0xff101820;
  if(s->menu==1 && s->mode==MMX_RUSH_OFF) {
    box(p,width,extra+48,207,164,13,black);
    text(p,width,extra+64,210,"BOSS RUSH",s->selection==3?yellow:white);
  }
  if(s->menu==2 && s->mode==MMX_RUSH_OFF) {
    box(p,width,extra+36,144,192,72,black);
    text(p,width,extra+88,150,"BOSS RUSH",yellow);
    text(p,width,extra+60,166,"SOLO",s->selection==0?yellow:white);
    text(p,width,extra+60,182,"COOP",s->selection==1?yellow:white);
    text(p,width,extra+48,200,"COOP USES THE COOP MOD",white);
  }
  if(s->mode==MMX_RUSH_PLAYING || s->mode==MMX_RUSH_FINISHED) {
    char count[32];snprintf(count,sizeof(count),"DEFEATED %u",s->defeated);
    text(p,width,width/2-(int)strlen(count)*3,8,count,yellow);
    for(unsigned i=0;i<2;++i) {
      const MmxBossRushBoss *b=&s->bosses[i];if(!b->phase) continue;
      if(b->phase==MMX_RUSH_DYING) explosion(p,width,extra,b);
      int x=width-32+(int)i*14,y=32;
      box(p,width,x,y,10,70,0xffb0b8c0);box(p,width,x+1,y+1,8,68,black);
      for(unsigned hp=0;hp<b->health && hp<32;++hp)
        box(p,width,x+2,y+66-(int)hp*2,6,1,i?0xffef8d62:0xffefdf62);
      static const char *const labels[]={"CP","SM","AA","LO","BK","SC","SE","FM"};
      text(p,width,x,106,labels[b->id],white);
    }
  }
  if(s->mode==MMX_RUSH_FINISHED) {
    box(p,width,width/2-84,82,168,78,black);
    text(p,width,width/2-36,90,"RUN FINISHED",yellow);
    text(p,width,width/2-18,112,"RETRY",s->result_selection==0?yellow:white);
    text(p,width,width/2-27,132,"MAIN MENU",s->result_selection==1?yellow:white);
  }
}
