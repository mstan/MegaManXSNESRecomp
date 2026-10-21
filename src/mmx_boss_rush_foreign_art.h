#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Original X2/X3 art for relocated Boss Rush bodies, decoded from the user's
 * in-memory source ROM: the boss's compressed OBJ resource, its palette and
 * the source game's pose layouts. Nothing is written to disk. */
typedef struct MmxForeignPose {
  int16_t left,top;      /* offset of pixel (0,0) from the object's origin */
  uint16_t width,height;
  const uint8_t *pixels; /* 4bpp color indices, 0 transparent */
} MmxForeignPose;

enum { MMX_FOREIGN_MAX_PALETTES=4 };
/* Pose `pose` of sprite set `set` for roster id `id`, drawn from an object's
 * tile base (attribute bit 0 << 8 | +$18), or NULL when the art is
 * unavailable (no source ROM, unknown boss, invalid layout). */
const MmxForeignPose *MmxBossRushForeignObjectPose(unsigned id,unsigned set,unsigned pose,unsigned base);
/* The same for the boss body's own base (the upper half's first tile). */
const MmxForeignPose *MmxBossRushForeignPose(unsigned id,unsigned set,unsigned pose);
/* The boss's BGR555 colors, 16 per palette from OBJ palette 4, or NULL. */
const uint16_t *MmxBossRushForeignColors(unsigned id);
unsigned MmxBossRushForeignPalettes(unsigned id);
/* The foreign pack hands over each validated source ROM (NULL when it
 * unloads); decoded art is dropped whenever a ROM changes. */
void MmxBossRushForeignArtSetRom(unsigned game,const uint8_t *rom,size_t size);
/* Effect-list classes that are the boss's own source-game effects (drawn from
 * its art); other effects it spawns are X1-native. id >= roster clears all. */
void MmxBossRushForeignArtSetEffectClass(unsigned id,uint8_t kind,bool own);
bool MmxBossRushForeignArtEffectClass(unsigned id,uint8_t kind);
/* X1 helpers a relocated body calls can spawn X1 effects of the same class
 * in its seat (Wire Sponge's vine hitting the ceiling spawns X1 rubble, class
 * $2B like his halves). With a filter, an effect of that class is the boss's
 * own only when it uses the boss's sprite `set` (+$16) or, before its first
 * update (+$01 = 0), was spawned with a variant (+$0B) in first..last. */
void MmxBossRushForeignArtSetEffectFilter(unsigned id,uint8_t kind,uint8_t set,uint8_t first,uint8_t last);
bool MmxBossRushForeignOwnEffect(unsigned id,const uint8_t *object);
