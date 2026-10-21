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
  uint32_t controller,children[MMX_RUSH_FOREIGN_CHILDREN],effects[MMX_RUSH_FOREIGN_CHILDREN];
  uint8_t fighting,dying,kinds[MMX_RUSH_FOREIGN_CHILDREN],effect_kinds[MMX_RUSH_FOREIGN_CHILDREN];
  uint32_t scratch,saber_kill;uint8_t scratch_size,saber_value;
  uint8_t death_field,death_value,death_frames;
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
  MmxBossRushForeignArtSetEffectClass(MMX_RUSH_ROSTER,0,false);
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
  else {
    for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
      if(bodies[id].effects[i]) MmxBossRushForeignArtSetEffectClass(id,bodies[id].effect_kinds[i],false);
    memset(&bodies[id],0,sizeof(bodies[id]));implemented&=~(1u<<id);
  }
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
bool MmxBossRushForeignSetEffectChild(unsigned id,uint8_t kind,uint32_t controller) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || !bodies[id].controller || controller>0xffffff) return false;
  ForeignBody *b=&bodies[id];
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(b->effects[i] && b->effect_kinds[i]==kind) {
      b->effects[i]=controller;MmxBossRushForeignArtSetEffectClass(id,kind,controller!=0);return true;
    }
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(!b->effects[i]) {
      b->effects[i]=controller;b->effect_kinds[i]=kind;
      MmxBossRushForeignArtSetEffectClass(id,kind,controller!=0);return controller!=0;
    }
  return false;
}
uint32_t MmxBossRushForeignEffectChild(unsigned id,uint8_t kind) {
  if(id>=MMX_RUSH_ROSTER) return 0;
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(bodies[id].effects[i] && bodies[id].effect_kinds[i]==kind) return bodies[id].effects[i];
  return 0;
}
bool MmxBossRushForeignSetScratch(unsigned id,uint32_t address,unsigned size) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || size>MMX_RUSH_SCRATCH_BYTES ||
      (size && (address<0x7e0000 || (address&0xffffff)+size>0x800000))) return false;
  bodies[id].scratch=size?address:0;bodies[id].scratch_size=(uint8_t)size;return true;
}
unsigned MmxBossRushForeignScratch(unsigned id,uint32_t *address) {
  if(id>=MMX_RUSH_ROSTER || !bodies[id].scratch_size) return 0;
  if(address) *address=bodies[id].scratch;
  return bodies[id].scratch_size;
}
bool MmxBossRushForeignSetSaberKill(unsigned id,uint32_t pc,uint8_t value) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || pc>0xffffff) return false;
  bodies[id].saber_kill=pc;bodies[id].saber_value=value;return true;
}
bool MmxBossRushForeignSaberKill(unsigned id,uint32_t *pc,uint8_t *value) {
  if(id>=MMX_RUSH_ROSTER || !bodies[id].saber_kill) return false;
  if(pc) *pc=bodies[id].saber_kill;
  if(value) *value=bodies[id].saber_value;
  return true;
}
unsigned MmxBossRushForeignSaberKillReads(const uint32_t **pcs) {
  static uint32_t reads[MMX_RUSH_ROSTER];unsigned n=0;
  for(unsigned id=0;id<MMX_RUSH_ROSTER;++id) if(bodies[id].saber_kill) reads[n++]=bodies[id].saber_kill;
  if(pcs) *pcs=reads;
  return n;
}
bool MmxBossRushForeignSetOwnDeath(unsigned id,uint8_t field,uint8_t value,uint8_t frames) {
  if(MmxBossRushNative(id) || id>=MMX_RUSH_ROSTER || field>=0x40) return false;
  bodies[id].death_field=field;bodies[id].death_value=value;bodies[id].death_frames=frames;return true;
}
bool MmxBossRushForeignOwnDeath(unsigned id,uint8_t *field,uint8_t *value,uint8_t *frames) {
  if(id>=MMX_RUSH_ROSTER || !bodies[id].death_frames) return false;
  if(field) *field=bodies[id].death_field;
  if(value) *value=bodies[id].death_value;
  if(frames) *frames=bodies[id].death_frames;
  return true;
}
uint32_t MmxBossRushForeignChild(unsigned id,uint8_t kind) {
  if(id>=MMX_RUSH_ROSTER) return 0;
  for(unsigned i=0;i<MMX_RUSH_FOREIGN_CHILDREN;++i)
    if(bodies[id].children[i] && bodies[id].kinds[i]==kind) return bodies[id].children[i];
  return 0;
}
/* Relocated bodies whose art, sound and weaknesses are finished. Others run
 * only in developer preview (MMX_BOSS_RUSH_PREVIEW=1 or the test hook). */
static const uint32_t playable=1u<<13|1u<<23; /* Wire Sponge, Tunnel Rhino */
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
/* Hook registries, appended per relocated boss. */
#define HOOKS_MAX 16
static uint32_t palette_pcs[HOOKS_MAX];static bool palette_rotates[HOOKS_MAX];static unsigned palette_count;
static uint32_t midline_reads[HOOKS_MAX];static unsigned midline_count;
static MmxBossRushSpawnGuard spawn_guards[HOOKS_MAX];static unsigned spawn_guard_count;
static MmxBossRushChildGuard child_guards[HOOKS_MAX];static unsigned child_guard_count;
static uint32_t sound_pcs[HOOKS_MAX];static bool sound_positional[HOOKS_MAX];static unsigned sound_count;
static MmxBossRushRoomRead room_reads[HOOKS_MAX];static unsigned room_read_count;
static MmxBossRushPoke pokes[HOOKS_MAX];static unsigned poke_count;
void MmxBossRushForeignResetHooks(void) {
  palette_count=midline_count=spawn_guard_count=child_guard_count=sound_count=room_read_count=poke_count=0;
}
void MmxBossRushForeignAddRoomReads(const MmxBossRushRoomRead *reads,unsigned count) {
  for(unsigned i=0;i<count && room_read_count<HOOKS_MAX;++i) room_reads[room_read_count++]=reads[i];
}
void MmxBossRushForeignAddPokes(const MmxBossRushPoke *list,unsigned count) {
  for(unsigned i=0;i<count && poke_count<HOOKS_MAX;++i) pokes[poke_count++]=list[i];
}
unsigned MmxBossRushForeignPokes(const MmxBossRushPoke **list) {
  if(list) *list=pokes;
  return poke_count;
}
unsigned MmxBossRushForeignRoomReads(const MmxBossRushRoomRead **reads) {
  if(reads) *reads=room_reads;
  return room_read_count;
}
void MmxBossRushForeignAddPaletteHooks(uint32_t rotate,uint32_t fade) {
  if(rotate && palette_count<HOOKS_MAX) {palette_pcs[palette_count]=rotate;palette_rotates[palette_count++]=true;}
  if(fade && palette_count<HOOKS_MAX) {palette_pcs[palette_count]=fade;palette_rotates[palette_count++]=false;}
}
unsigned MmxBossRushForeignPaletteHooks(const uint32_t **pcs,const bool **rotates) {
  if(pcs) *pcs=palette_pcs;
  if(rotates) *rotates=palette_rotates;
  return palette_count;
}
void MmxBossRushForeignAddMidlineReads(const uint32_t *pcs,unsigned count) {
  for(unsigned i=0;i<count && midline_count<HOOKS_MAX;++i) midline_reads[midline_count++]=pcs[i];
}
unsigned MmxBossRushForeignMidlineReads(const uint32_t **pcs) {
  if(pcs) *pcs=midline_reads;
  return midline_count;
}
void MmxBossRushForeignAddSpawnGuards(const MmxBossRushSpawnGuard *guards,unsigned count) {
  for(unsigned i=0;i<count && spawn_guard_count<HOOKS_MAX;++i) spawn_guards[spawn_guard_count++]=guards[i];
}
unsigned MmxBossRushForeignSpawnGuards(const MmxBossRushSpawnGuard **guards) {
  if(guards) *guards=spawn_guards;
  return spawn_guard_count;
}
void MmxBossRushForeignAddChildGuards(const MmxBossRushChildGuard *guards,unsigned count) {
  for(unsigned i=0;i<count && child_guard_count<HOOKS_MAX;++i) child_guards[child_guard_count++]=guards[i];
}
unsigned MmxBossRushForeignChildGuards(const MmxBossRushChildGuard **guards) {
  if(guards) *guards=child_guards;
  return child_guard_count;
}
void MmxBossRushForeignAddSoundShims(uint32_t plain,uint32_t positional) {
  if(plain && sound_count<HOOKS_MAX) {sound_pcs[sound_count]=plain;sound_positional[sound_count++]=false;}
  if(positional && positional!=plain && sound_count<HOOKS_MAX) {
    sound_pcs[sound_count]=positional;sound_positional[sound_count++]=true;
  }
}
unsigned MmxBossRushForeignSoundShims(const uint32_t **pcs,const bool **positional) {
  if(pcs) *pcs=sound_pcs;
  if(positional) *positional=sound_positional;
  return sound_count;
}
void MmxBossRushForeignSync(void) {
  if(!dirty || !MmxCartPatchAvailable()) return;
  dirty=false;
  char error[256];
  if(!MmxBossRushForeignBuild(error,sizeof(error)))
    fprintf(stderr,"[mmx-rush] %s; those Mavericks stay out of Boss Rush\n",error);
}
