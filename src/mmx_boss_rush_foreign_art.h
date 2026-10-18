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

/* Pose `pose` of sprite set `set` for roster id `id`, or NULL when the art is
 * unavailable (no source ROM, unknown boss, invalid layout). */
const MmxForeignPose *MmxBossRushForeignPose(unsigned id,unsigned set,unsigned pose);
/* The boss's 16 BGR555 colors, or NULL. */
const uint16_t *MmxBossRushForeignColors(unsigned id);
/* The foreign pack hands over each validated source ROM (NULL when it
 * unloads); decoded art is dropped whenever a ROM changes. */
void MmxBossRushForeignArtSetRom(unsigned game,const uint8_t *rom,size_t size);
