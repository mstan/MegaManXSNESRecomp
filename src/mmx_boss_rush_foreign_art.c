#include "mmx_boss_rush_foreign_art.h"
#include "mmx_boss_rush.h"
#include <stdlib.h>
#include <string.h>

/* Where each relocated body's art lives in its source ROM. The resource is
 * the boss's graphics slot (X3 slot == resource number, record table
 * $86:F732); the palette is its stage list index into $86:8180. */
typedef struct ArtSource {
  unsigned id,game,resource,palette;
} ArtSource;
static const ArtSource kSources[]={
  {23,3,0x1d,0x3e}, /* Tunnel Rhino: tiles $9E:C284, colors $8C:E628 */
};
enum { RESOURCE_TABLE=0x86f732, PALETTE_LISTS=0x868180, PALETTE_BANK=0x8c,
       LAYOUT_SETS=0x8d8000, MAX_TILES=256, CACHE=96 };

typedef struct Art {
  bool tried,ready;
  const uint8_t *rom;size_t size;
  uint8_t tiles[MAX_TILES*32];unsigned tile_count;
  uint16_t colors[16];
} Art;
static Art arts[MMX_RUSH_ROSTER];
static const uint8_t *sources[2];
static size_t source_sizes[2];
typedef struct CachedPose {
  bool used;uint8_t id,set,pose;
  MmxForeignPose pose_info;uint8_t *pixels;
} CachedPose;
static CachedPose cache[CACHE];
static unsigned cache_next;

static const uint8_t *at(const Art *a,uint32_t address,size_t n) {
  if((address&0xffff)<0x8000) return NULL;
  size_t o=((size_t)((address>>16)&0x7f)<<15)|(address&0x7fff);
  return o<=a->size && n<=a->size-o?a->rom+o:NULL;
}
static unsigned word(const uint8_t *p) {return p[0]|p[1]<<8;}
static unsigned long24(const uint8_t *p) {return p[0]|p[1]<<8|p[2]<<16;}

/* The source games' OBJ resources use the LZ format the weapon extractor
 * decodes: an 8-bit control byte, then literals or 10-bit back-references. */
static bool decompress(Art *a,unsigned resource) {
  const uint8_t *record=at(a,RESOURCE_TABLE+resource*5,5);
  if(!record) return false;
  unsigned length=word(record+3);
  uint32_t source=long24(record);
  if(!length || length>sizeof(a->tiles)) return false;
  size_t o=((size_t)((source>>16)&0x7f)<<15)|(source&0x7fff),out=0;
  if((source&0xffff)<0x8000 || o>=a->size) return false;
  while(out<length) {
    if(o>=a->size) return false;
    unsigned control=a->rom[o++];
    for(unsigned bit=0x80;bit && out<length;bit>>=1) {
      if(control&bit) {
        if(o+1>=a->size) return false;
        unsigned hi=a->rom[o],lo=a->rom[o+1],count=hi>>2,distance=((hi&3)<<8)|lo;o+=2;
        if(!count || !distance || distance>out || out+count>length) return false;
        for(unsigned k=0;k<count;++k,++out) a->tiles[out]=a->tiles[out-distance];
      } else {
        if(o>=a->size) return false;
        a->tiles[out++]=a->rom[o++];
      }
    }
  }
  a->tile_count=length/32;
  return true;
}
static bool palette(Art *a,unsigned list) {
  const uint8_t *entry=at(a,PALETTE_LISTS+list,2);
  if(!entry) return false;
  const uint8_t *record=at(a,0x860000|word(entry),4);
  if(!record || record[0]<16) return false;
  const uint8_t *colors=at(a,(uint32_t)PALETTE_BANK<<16|word(record+1),32);
  if(!colors) return false;
  for(unsigned c=0;c<16;++c) a->colors[c]=(uint16_t)(word(colors+c*2)&0x7fff);
  return true;
}
static Art *art(unsigned id) {
  if(id>=MMX_RUSH_ROSTER) return NULL;
  Art *a=&arts[id];
  if(a->tried) return a->ready?a:NULL;
  a->tried=true;
  for(size_t i=0;i<sizeof(kSources)/sizeof(*kSources);++i) {
    if(kSources[i].id!=id) continue;
    unsigned game=kSources[i].game;
    if(game<2 || game>3) break;
    a->rom=sources[game-2];a->size=source_sizes[game-2];
    a->ready=a->rom && decompress(a,kSources[i].resource) && palette(a,kSources[i].palette);
  }
  return a->ready?a:NULL;
}
void MmxBossRushForeignArtSetRom(unsigned game,const uint8_t *rom,size_t size) {
  if(game<2 || game>3) return;
  sources[game-2]=rom;source_sizes[game-2]=rom?size:0;
  memset(arts,0,sizeof(arts));
  for(unsigned i=0;i<CACHE;++i) free(cache[i].pixels);
  memset(cache,0,sizeof(cache));cache_next=0;
}
const uint16_t *MmxBossRushForeignColors(unsigned id) {
  Art *a=art(id);return a?a->colors:NULL;
}
/* Same arrangement rules as tools/extract_x_weapons.py pose_art: pieces are
 * (flags, x, y, tile) with 16x16 when flags bit 5, h/v flips in bits 6/7. */
static bool rasterize(Art *a,unsigned set,unsigned pose,MmxForeignPose *info,uint8_t **pixels) {
  const uint8_t *entry=at(a,LAYOUT_SETS+set*3,3);
  if(!entry) return false;
  const uint8_t *slot=at(a,long24(entry)+pose*3,3);
  if(!slot) return false;
  uint32_t layout=long24(slot);
  const uint8_t *count=at(a,layout,1);
  if(!count || *count>64) return false;
  const uint8_t *pieces=at(a,layout+1,*count*4u);
  if(!pieces) return false;
  int left=0,top=0,right=0,bottom=0;
  for(unsigned i=0;i<*count;++i) {
    const uint8_t *p=pieces+i*4;int size=p[0]&0x20?16:8,x=(int8_t)p[1],y=(int8_t)p[2];
    if(!i || x<left) left=x;
    if(!i || y<top) top=y;
    if(!i || x+size>right) right=x+size;
    if(!i || y+size>bottom) bottom=y+size;
  }
  int width=*count?right-left:0,height=*count?bottom-top:0;
  if(width>256 || height>256) return false;
  uint8_t *out=calloc(width && height?(size_t)width*height:1,1);
  if(!out) return false;
  for(int i=(int)*count-1;i>=0;--i) {
    const uint8_t *p=pieces+i*4;unsigned flags=p[0],tile=p[3];
    int size=flags&0x20?16:8,x=(int8_t)p[1],y=(int8_t)p[2];
    for(int dy=0;dy<size;++dy) for(int dx=0;dx<size;++dx) {
      int tx=flags&0x40?size-1-dx:dx,ty=flags&0x80?size-1-dy:dy;
      unsigned number=(((tile>>4)+ty/8)&15)*16+((tile+tx/8)&15);
      if(number>=a->tile_count) continue;
      unsigned bits=number*32+(ty&7)*2,shift=7-(tx&7);
      unsigned color=((a->tiles[bits]>>shift)&1)|(((a->tiles[bits+1]>>shift)&1)<<1)|
          (((a->tiles[bits+16]>>shift)&1)<<2)|(((a->tiles[bits+17]>>shift)&1)<<3);
      if(color) out[(y-top+dy)*width+x-left+dx]=(uint8_t)color;
    }
  }
  *info=(MmxForeignPose){(int16_t)left,(int16_t)top,(uint16_t)width,(uint16_t)height,out};
  *pixels=out;return true;
}
const MmxForeignPose *MmxBossRushForeignPose(unsigned id,unsigned set,unsigned pose) {
  Art *a=art(id);
  if(!a || set>255 || pose>255) return NULL;
  for(unsigned i=0;i<CACHE;++i)
    if(cache[i].used && cache[i].id==id && cache[i].set==set && cache[i].pose==pose)
      return cache[i].pixels?&cache[i].pose_info:NULL;
  CachedPose *c=&cache[cache_next];cache_next=(cache_next+1)%CACHE;
  free(c->pixels);memset(c,0,sizeof(*c));
  c->used=true;c->id=(uint8_t)id;c->set=(uint8_t)set;c->pose=(uint8_t)pose;
  if(!rasterize(a,set,pose,&c->pose_info,&c->pixels)) c->pixels=NULL;
  return c->pixels?&c->pose_info:NULL;
}
