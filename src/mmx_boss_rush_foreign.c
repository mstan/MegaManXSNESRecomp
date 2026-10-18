#include "mmx_boss_rush_foreign.h"
#include "mmx_boss_rush.h"
#include "mmx_source_assets.h"
#include "mmx_cart_extension.h"
#include "mmx_boss_rush_foreign_art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *roms[2];
static size_t sizes[2];

/* A body is implemented once its relocated controller is registered.
 * Tunnel Rhino (id 23) is the pilot. */
static uint32_t implemented;
typedef struct ForeignBody {
  uint32_t controller,children[MMX_RUSH_FOREIGN_CHILDREN];
  uint8_t fighting,dying,kinds[MMX_RUSH_FOREIGN_CHILDREN];
} ForeignBody;
static ForeignBody bodies[MMX_RUSH_ROSTER];
enum { MAX_WEAKNESSES=8 };
static MmxBossRushWeakness weaknesses[MMX_RUSH_ROSTER][MAX_WEAKNESSES];
static uint8_t weakness_count[MMX_RUSH_ROSTER];

static bool dirty;
static void publish(void) {MmxBossRushSetOffered(MmxBossRushForeignOffered());}
bool MmxBossRushForeignLoad(unsigned game,const char *rom,char *error,size_t error_size) {
  if(game!=2 && game!=3) return false;
  unsigned char *image=NULL;size_t size=0;
  if(!MmxSourceRomLoad(rom,game,&image,&size,error,error_size)) return false;
  MmxBossRushForeignArtSetRom(game,NULL,0);
  free(roms[game-2]);roms[game-2]=image;sizes[game-2]=size;
  MmxBossRushForeignArtSetRom(game,image,size);
  dirty=true;publish();return true;
}
void MmxBossRushForeignUnload(void) {
  for(unsigned i=0;i<2;++i) {
    MmxBossRushForeignArtSetRom(i+2,NULL,0);free(roms[i]);roms[i]=NULL;sizes[i]=0;
  }
  memset(bodies,0,sizeof(bodies));implemented=0;memset(weakness_count,0,sizeof(weakness_count));
  dirty=true;publish();
}
bool MmxBossRushForeignReady(unsigned game) {
  return (game==2 || game==3) && roms[game-2]!=NULL;
}
const uint8_t *MmxBossRushForeignRom(unsigned game,size_t *size) {
  if(!MmxBossRushForeignReady(game)) {if(size) *size=0;return NULL;}
  if(size) *size=sizes[game-2];
  return roms[game-2];
}
uint32_t MmxBossRushForeignImplemented(void) {return implemented;}
bool MmxBossRushForeignSetController(unsigned id,uint32_t controller) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || controller>0xffffff) return false;
  if(controller) {bodies[id].controller=controller;implemented|=1u<<id;}
  else {memset(&bodies[id],0,sizeof(bodies[id]));implemented&=~(1u<<id);}
  publish();return true;
}
uint32_t MmxBossRushForeignController(unsigned id) {
  return id<MMX_RUSH_ROSTER?bodies[id].controller:0;
}
bool MmxBossRushForeignSetStates(unsigned id,uint8_t fighting,uint8_t dying) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || fighting==dying) return false;
  bodies[id].fighting=fighting;bodies[id].dying=dying;return true;
}
void MmxBossRushForeignStates(unsigned id,uint8_t *fighting,uint8_t *dying) {
  uint8_t f=4,d=6;
  if(id<MMX_RUSH_ROSTER && bodies[id].fighting!=bodies[id].dying) {f=bodies[id].fighting;d=bodies[id].dying;}
  if(fighting) *fighting=f;
  if(dying) *dying=d;
}
bool MmxBossRushForeignSetChild(unsigned id,uint8_t kind,uint32_t controller) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || !bodies[id].controller || controller>0xffffff) return false;
  ForeignBody *b=&bodies[id];
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(b->children[i] && b->kinds[i]==kind) {b->children[i]=controller;return true;}
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(!b->children[i]) {b->children[i]=controller;b->kinds[i]=kind;return controller!=0;}
  return false;
}
uint32_t MmxBossRushForeignChild(unsigned id,uint8_t kind) {
  if(id>=MMX_RUSH_ROSTER) return 0;
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(bodies[id].children[i] && bodies[id].kinds[i]==kind) return bodies[id].children[i];
  return 0;
}
/* Relocated bodies whose art, sound and weaknesses are finished. Others run
 * only in developer preview (MMX_BOSS_RUSH_PREVIEW=1 or the test hook). */
static const uint32_t playable=1u<<23; /* Tunnel Rhino */
static int preview=-1;
void MmxBossRushForeignSetPreview(bool enabled) {preview=enabled;publish();}
uint32_t MmxBossRushForeignOffered(void) {
  if(preview<0) {const char *v=getenv("MMX_BOSS_RUSH_PREVIEW");preview=v && *v && *v!='0';}
  uint32_t ready=preview?implemented:implemented&playable,mask=MMX_RUSH_X1_MASK;
  if(MmxBossRushForeignReady(2)) mask|=ready&MMX_RUSH_X2_MASK;
  if(MmxBossRushForeignReady(3)) mask|=ready&MMX_RUSH_X3_MASK;
  return mask;
}
bool MmxBossRushForeignSetWeaknesses(unsigned id,const MmxBossRushWeakness *rules,unsigned count) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || count>MAX_WEAKNESSES || (count && !rules)) return false;
  memcpy(weaknesses[id],rules,count*sizeof(*rules));weakness_count[id]=(uint8_t)count;return true;
}
const MmxBossRushWeakness *MmxBossRushForeignWeakness(unsigned id,unsigned page,unsigned weapon) {
  if(id>=MMX_RUSH_ROSTER) return NULL;
  for(unsigned i=0;i<weakness_count[id];++i)
    if(weaknesses[id][i].page==page && weaknesses[id][i].weapon==weapon) return &weaknesses[id][i];
  return NULL;
}
static uint32_t palette_hooks[2];
void MmxBossRushForeignSetPaletteHooks(uint32_t rotate,uint32_t fade) {
  palette_hooks[0]=rotate;palette_hooks[1]=fade;
}
void MmxBossRushForeignPaletteHooks(uint32_t *rotate,uint32_t *fade) {
  if(rotate) *rotate=palette_hooks[0];
  if(fade) *fade=palette_hooks[1];
}
static uint32_t midline_reads[8];static unsigned midline_count;
void MmxBossRushForeignSetMidlineReads(const uint32_t *pcs,unsigned count) {
  if(count>sizeof(midline_reads)/sizeof(*midline_reads)) count=0;
  if(count) memcpy(midline_reads,pcs,count*sizeof(*pcs));
  midline_count=count;
}
unsigned MmxBossRushForeignMidlineReads(const uint32_t **pcs) {
  if(pcs) *pcs=midline_reads;
  return midline_count;
}
static MmxBossRushSpawnGuard spawn_guards[8];static unsigned spawn_guard_count;
void MmxBossRushForeignSetSpawnGuards(const MmxBossRushSpawnGuard *guards,unsigned count) {
  if(count>sizeof(spawn_guards)/sizeof(*spawn_guards)) count=0;
  if(count) memcpy(spawn_guards,guards,count*sizeof(*guards));
  spawn_guard_count=count;
}
unsigned MmxBossRushForeignSpawnGuards(const MmxBossRushSpawnGuard **guards) {
  if(guards) *guards=spawn_guards;
  return spawn_guard_count;
}
static MmxBossRushChildGuard child_guards[4];static unsigned child_guard_count;
void MmxBossRushForeignSetChildGuards(const MmxBossRushChildGuard *guards,unsigned count) {
  if(count>sizeof(child_guards)/sizeof(*child_guards)) count=0;
  if(count) memcpy(child_guards,guards,count*sizeof(*guards));
  child_guard_count=count;
}
unsigned MmxBossRushForeignChildGuards(const MmxBossRushChildGuard **guards) {
  if(guards) *guards=child_guards;
  return child_guard_count;
}
static uint32_t sound_shims[2];
void MmxBossRushForeignSetSoundShims(uint32_t plain,uint32_t positional) {
  sound_shims[0]=plain;sound_shims[1]=positional;
}
void MmxBossRushForeignSoundShims(uint32_t *plain,uint32_t *positional) {
  if(plain) *plain=sound_shims[0];
  if(positional) *positional=sound_shims[1];
}
void MmxBossRushForeignSync(void) {
  if(!dirty || !MmxCartPatchAvailable()) return;
  dirty=false;
  char error[256];
  if(!MmxBossRushForeignBuild(error,sizeof(error)))
    fprintf(stderr,"[mmx-rush] %s; those Mavericks stay out of Boss Rush\n",error);
}
