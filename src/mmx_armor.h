#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Simulation state is fixed-width and pointer-free. Art is a local ROM cache;
 * none of it participates in rollback or a native in-game password. */
typedef struct MmxArmorShot {
  int32_t x,y;
  int16_t vx,vy;
  uint16_t animation,age,hit_slots;
  uint8_t active,kind,sub,parent,facing,group,pose,timer,flags;
  uint8_t segment,segment_timer,history_pos,cross_fragment,palette;
  /* X2/X3 keep a 32-entry ring for each trailing charged component. */
  int16_t trail_x[32],trail_y[32];
  uint8_t trail_flags[32];
} MmxArmorShot;
typedef struct MmxArmorState {
  uint16_t anim_offset, charge, recovery_ticks, barrier_ticks;
  uint16_t energy, health_before, shot_mask, reserved_word;
  uint8_t anim_timer, anim_pose, anim_flags, anim_valid;
  uint8_t gold, dash_used, dash_ticks, dash_up;
  uint8_t dash_facing, dash_jump, stage, valid;
  uint8_t stored_shot, fire_held, barrier_level, barrier_delay;
  uint8_t tracer, utility_held, anim_group, dash_recovery;
  uint16_t burst_offset;
  uint8_t burst,burst_timer,burst_pose,burst_flags,burst_fired,burst_air;
  uint8_t burst_facing,burst_transition,burst_end,burst_queued;
  uint16_t barrier_offset;
  uint8_t barrier_timer,barrier_pose,barrier_flags,barrier_phase;
  int32_t barrier_x,barrier_y;
  uint16_t utility_ticks,radar_epoch;
  uint8_t radar_stage,radar_closing;
  uint16_t giga_tick,hyper_energy;
  uint8_t giga_fired,hyper_active;
  uint16_t utility_reserved;
  MmxArmorShot shots[8];
} MmxArmorState;

bool MmxArmorLoad(const char *path, unsigned game);
void MmxArmorDisable(void);
bool MmxArmorEnabled(void);
unsigned MmxArmorGame(void);
MmxArmorState MmxArmorGetState(void);
bool MmxArmorValidState(const MmxArmorState *value);
void MmxArmorSetState(MmxArmorState value);
void MmxArmorResetState(void);
unsigned MmxArmorUpgradeBits(unsigned pc, unsigned original);
void MmxArmorPrePlayer(uint8_t *ram);
void MmxArmorPlayerEnd(uint8_t *ram);
void MmxArmorPlayerMotion(uint8_t *ram, unsigned object);
void MmxArmorSetBusterQuery(bool (*query)(void));
void MmxArmorSetActorQuery(bool (*query)(void));
void MmxArmorSetBodyQuery(uint8_t *(*query)(uint8_t *ram));
void MmxArmorSetTerrainQuery(bool (*query)(const uint8_t *,int,int));
void MmxArmorRefill(void);
/* Physical SNES buttons from X's actual seat, after NMI. True holds the
 * scheduler for the X3 radar while NMI, audio and rendering continue. */
bool MmxArmorFrameTick(uint8_t *ram,uint16_t pad,bool x_available);
void MmxArmorCollisionRom(uint8_t *rom,size_t size);
unsigned MmxArmorProjectileTick(uint8_t *ram,unsigned object,unsigned original);
unsigned MmxArmorDamage(uint8_t *ram,unsigned enemy,unsigned projectile,unsigned original);
unsigned MmxArmorHitbox(const uint8_t *ram,unsigned enemy,unsigned projectile,unsigned original);
unsigned MmxArmorResponse(uint8_t *ram,unsigned enemy,unsigned projectile,unsigned original);
bool MmxArmorProjectileOwned(const uint8_t *ram,unsigned object);
const uint16_t *MmxArmorEffectColors(void);
void MmxArmorAnimationStart(unsigned object, unsigned sequence);
void MmxArmorAnimationAdvance(unsigned object);
/* Called only by the original secret capsule's award instruction. */
void MmxArmorHadoukenAward(const uint8_t *ram);
/* The original positive player-damage path has already rejected immunity,
 * contact knockback and instant-death cases. `object` is the attacker. */
void MmxArmorTakeDamage(uint8_t *ram, unsigned object);
typedef struct MmxArmorTarget {
  uint16_t x,y;
  uint8_t kind,mask; /* 1 heart, 2 Sub Tank, 3 armor capsule, 4 secret capsule */
} MmxArmorTarget;
void MmxArmorSetRom(const uint8_t *rom,size_t size);
unsigned MmxArmorTargets(const uint8_t *ram,MmxArmorTarget *out,unsigned capacity);
struct RecompLauncherCGameInfo;
void MmxArmorConfigureLauncher(struct RecompLauncherCGameInfo *info);
/* Development command handoff; invoked by the desktop simulation thread. */
void MmxArmorDebugHostFrame(void);

enum { MMX_ARMOR_CANVAS = 128 };
typedef struct MmxArmorGraphic {
  const uint8_t *pixels;
  uint16_t x,y,w,h;
} MmxArmorGraphic;
bool MmxArmorGraphicGet(unsigned group,unsigned pose,MmxArmorGraphic *out);
/* The caller owns pixels/palette so both co-op seats have stable snapshots.
 * Acquired parts retain their real X1 inventory bits. */
bool MmxArmorCompose(const uint8_t *ram, const MmxArmorState *snapshot, bool is_x, bool menu,
                     uint8_t pixels[128*128], uint16_t palette[128]);
