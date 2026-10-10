#include "mmx_boss_rush.h"

static void box(uint32_t *p,int width,int x,int y,int w,int h,uint32_t color) {
  for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i)
    if(i>=0 && i<width && j>=0 && j<224) p[j*width+i]=color;
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
  const uint32_t black=0xff101820;
  if(s->mode==MMX_RUSH_PLAYING || s->mode==MMX_RUSH_FINISHED) {
    for(unsigned i=0;i<2;++i) {
      const MmxBossRushBoss *b=&s->bosses[i];if(!b->phase) continue;
      if(b->phase==MMX_RUSH_DYING) explosion(p,width,extra,b);
    }
  }
  if(s->mode==MMX_RUSH_FINISHED) {
    box(p,width,width/2-84,82,168,78,black);
  }
}
