#include "mmx_boss_rush_foreign_art.h"
#include "mmx_boss_rush.h"
#include <stdlib.h>
#include <string.h>

/* Where each relocated body's art lives in its source ROM, laid out as its
 * room's stage list loads it: each graphics slot's resource (slot ==
 * resource number in X2 and X3) at its OBJ tile, and the palette list (from
 * OBJ palette 4, attribute $28). An object's tiles start at its own base
 * (attribute bit 0 selects the upper 256 tiles, +$18 adds to the tile). */
typedef struct ArtSlot {unsigned resource,tile;} ArtSlot;
typedef struct ArtSource {
  unsigned id,game;
  ArtSlot slots[2];
  unsigned palette;
} ArtSource;
static const ArtSource kSources[]={
  /* Wire Sponge, room list $2A:B2D4 stage 12: body $45 at VRAM $1000 (tiles
   * $A1:9D4C), vine and lightning $46 at $0480; 48 colors at $85:B7A0. */
  {13,2,{{0x45,0x100},{0x46,0x048}},0x9e},
  {23,3,{{0x1d,0x100},{0,0}},0x3e}, /* Tunnel Rhino: tiles $9E:C284, colors $8C:E628 */
};
/* Per game: compressed resource records, palette lists (data bank $86),
 * the palette color bank. Both games keep pose layouts at $8D:8000. */
typedef struct GameArt {uint32_t resources,palette_lists;uint8_t palette_bank;} GameArt;
static const GameArt kGames[2]={
  {0x86fa01,0x86817a,0x85}, /* X2 */
  {0x86f732,0x868180,0x8c}, /* X3 */
};
enum { LAYOUT_SETS=0x8d8000, VRAM_TILES=512, MAX_COLORS=MMX_FOREIGN_MAX_PALETTES*16, CACHE=96 };

typedef struct Art {
  bool tried,ready;
  const GameArt *game;
  const uint8_t *rom;size_t size;
  uint8_t tiles[VRAM_TILES*32];bool loaded[VRAM_TILES];
  uint16_t colors[MAX_COLORS];unsigned palettes;
} Art;
static Art arts[MMX_RUSH_ROSTER];
static const uint8_t *sources[2];
static size_t source_sizes[2];
typedef struct CachedPose {
  bool used;uint8_t id,set,pose;uint16_t base;
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
static bool decompress(Art *a,unsigned resource,unsigned tile) {
  const uint8_t *record=at(a,a->game->resources+resource*5,5);
  if(!record || tile>=VRAM_TILES) return false;
  unsigned length=word(record+3);
  uint32_t source=long24(record);
  if(!length || length>(VRAM_TILES-tile)*32u) return false;
  uint8_t *tiles=a->tiles+tile*32;
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
        for(unsigned k=0;k<count;++k,++out) tiles[out]=tiles[out-distance];
      } else {
        if(o>=a->size) return false;
        tiles[out++]=a->rom[o++];
      }
    }
  }
  for(unsigned t=0;t<length/32;++t) a->loaded[tile+t]=true;
  return true;
}
static bool palette(Art *a,unsigned list) {
  const uint8_t *entry=at(a,a->game->palette_lists+list,2);
  if(!entry) return false;
  const uint8_t *record=at(a,0x860000|word(entry),4);
  if(!record || record[0]<16) return false;
  unsigned count=record[0]>MAX_COLORS?MAX_COLORS:record[0]&~15u;
  const uint8_t *colors=at(a,(uint32_t)a->game->palette_bank<<16|word(record+1),count*2);
  if(!colors) return false;
  for(unsigned c=0;c<count;++c) a->colors[c]=(uint16_t)(word(colors+c*2)&0x7fff);
  a->palettes=count/16;
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
    a->rom=sources[game-2];a->size=source_sizes[game-2];a->game=&kGames[game-2];
    a->ready=a->rom && palette(a,kSources[i].palette);
    for(unsigned s=0;s<2 && a->ready;++s)
      if(kSources[i].slots[s].resource)
        a->ready=decompress(a,kSources[i].slots[s].resource,kSources[i].slots[s].tile);
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
static uint32_t effect_classes[MMX_RUSH_ROSTER][8];
typedef struct EffectFilter {uint8_t kind,set,first,last;} EffectFilter;
enum { EFFECT_FILTERS=4 };
static EffectFilter effect_filters[MMX_RUSH_ROSTER][EFFECT_FILTERS];
void MmxBossRushForeignArtSetEffectClass(unsigned id,uint8_t kind,bool own) {
  if(id>=MMX_RUSH_ROSTER) {
    memset(effect_classes,0,sizeof(effect_classes));memset(effect_filters,0,sizeof(effect_filters));return;
  }
  if(own) effect_classes[id][kind>>5]|=1u<<(kind&31);
  else effect_classes[id][kind>>5]&=~(1u<<(kind&31));
}
bool MmxBossRushForeignArtEffectClass(unsigned id,uint8_t kind) {
  return id<MMX_RUSH_ROSTER && (effect_classes[id][kind>>5]>>(kind&31)&1);
}
void MmxBossRushForeignArtSetEffectFilter(unsigned id,uint8_t kind,uint8_t set,uint8_t first,uint8_t last) {
  if(id>=MMX_RUSH_ROSTER || !set) return;
  for(unsigned i=0;i<EFFECT_FILTERS;++i)
    if(!effect_filters[id][i].set || effect_filters[id][i].kind==kind) {
      effect_filters[id][i]=(EffectFilter){kind,set,first,last};return;
    }
}
bool MmxBossRushForeignOwnEffect(unsigned id,const uint8_t *object) {
  uint8_t kind=object[10];
  if(!MmxBossRushForeignArtEffectClass(id,kind)) return false;
  for(unsigned i=0;i<EFFECT_FILTERS;++i) {
    const EffectFilter *f=&effect_filters[id][i];
    if(f->set && f->kind==kind)
      return object[0x16]==f->set || (!object[1] && object[11]>=f->first && object[11]<=f->last);
  }
  return true;
}
const uint16_t *MmxBossRushForeignColors(unsigned id) {
  Art *a=art(id);return a?a->colors:NULL;
}
unsigned MmxBossRushForeignPalettes(unsigned id) {
  Art *a=art(id);return a?a->palettes:0;
}
/* Same arrangement rules as tools/extract_x_weapons.py pose_art: pieces are
 * (flags, x, y, tile) with 16x16 when flags bit 5, h/v flips in bits 6/7 and
 * the other 256-tile half in bit 0. A piece's tile adds to the object's base
 * within its 256-tile half; 16x16 pieces wrap within their 16-tile rows. */
static bool rasterize(Art *a,unsigned set,unsigned pose,unsigned base,MmxForeignPose *info,uint8_t **pixels) {
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
    const uint8_t *p=pieces+i*4;unsigned flags=p[0],tile=(p[3]+base)&255,half=((base>>8)^flags)&1;
    int size=flags&0x20?16:8,x=(int8_t)p[1],y=(int8_t)p[2];
    for(int dy=0;dy<size;++dy) for(int dx=0;dx<size;++dx) {
      int tx=flags&0x40?size-1-dx:dx,ty=flags&0x80?size-1-dy:dy;
      unsigned number=half<<8|(((tile>>4)+ty/8)&15)*16|((tile+tx/8)&15);
      if(!a->loaded[number]) continue;
      unsigned bits=number*32+(ty&7)*2,shift=7-(tx&7);
      unsigned color=((a->tiles[bits]>>shift)&1)|(((a->tiles[bits+1]>>shift)&1)<<1)|
          (((a->tiles[bits+16]>>shift)&1)<<2)|(((a->tiles[bits+17]>>shift)&1)<<3);
      if(color) out[(y-top+dy)*width+x-left+dx]=(uint8_t)color;
    }
  }
  *info=(MmxForeignPose){(int16_t)left,(int16_t)top,(uint16_t)width,(uint16_t)height,out};
  *pixels=out;return true;
}
const MmxForeignPose *MmxBossRushForeignObjectPose(unsigned id,unsigned set,unsigned pose,unsigned base) {
  Art *a=art(id);
  if(!a || set>255 || pose>255 || base>=VRAM_TILES) return NULL;
  for(unsigned i=0;i<CACHE;++i)
    if(cache[i].used && cache[i].id==id && cache[i].set==set && cache[i].pose==pose && cache[i].base==base)
      return cache[i].pixels?&cache[i].pose_info:NULL;
  CachedPose *c=&cache[cache_next];cache_next=(cache_next+1)%CACHE;
  free(c->pixels);memset(c,0,sizeof(*c));
  c->used=true;c->id=(uint8_t)id;c->set=(uint8_t)set;c->pose=(uint8_t)pose;c->base=(uint16_t)base;
  if(!rasterize(a,set,pose,base,&c->pose_info,&c->pixels)) c->pixels=NULL;
  return c->pixels?&c->pose_info:NULL;
}
const MmxForeignPose *MmxBossRushForeignPose(unsigned id,unsigned set,unsigned pose) {
  return MmxBossRushForeignObjectPose(id,set,pose,0x100);
}
