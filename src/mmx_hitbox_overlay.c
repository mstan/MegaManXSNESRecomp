#include "mmx_hitbox_overlay.h"
#include "mmx_coop.h"
#include "saber/mmx_saber_attack.h"
#include "saber/mmx_saber_wave_runtime.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include <string.h>

extern uint8_t g_ram[0x20000];
extern Snes *g_snes;

enum {
  PLAYER = 0x0ba8, ENEMY_FIRST = 0x0e68, SHOT_FIRST = 0x1228,
  HAZARD_FIRST = 0x1428, HAZARD_END = 0x1628, SLOT = 0x40,
  COLLISION_BANK = 0x30000, /* LoROM $86:8000 */
  MAX_STOCK_RECTS = 64
};

static bool enabled;
static MmxRenderDebugRect stock_rects[MAX_STOCK_RECTS];
static unsigned stock_count;
static int stock_camera_x, stock_camera_y;

void MmxHitboxOverlaySetEnabled(bool on) {
  enabled = on;
  stock_count = 0;
  MmxRendererSetHitboxOverlayProvider(on ? MmxHitboxOverlayProvide : NULL);
}
bool MmxHitboxOverlayEnabled(void) { return enabled; }

static unsigned word(const uint8_t *p) { return p[0] | p[1] << 8; }

/* Melee objects are player-pool entries the port owns, tagged at +$3E:
 * Zero's X3 slash ('SZ') and Saber's attack and wave families. */
static bool melee_tag(unsigned tag) {
  return tag == 0x5a53 ||
      (tag & MMX_SABER_PROJECTILE_TAG_FAMILY_MASK) == MMX_SABER_PROJECTILE_TAG_FAMILY ||
      ((tag & MMX_SABER_WAVE_TAG_FAMILY_MASK) == MMX_SABER_WAVE_TAG_FAMILY && (tag & 0xff));
}

/* Pool objects are live when +$00 is set; a player body is not a pool entry
 * and is live while its +$01 state is set. */
static unsigned append(MmxRenderDebugRect *out, unsigned max, unsigned count,
                       const uint8_t *object, bool live, const uint8_t *rom,
                       size_t rom_size, uint16_t color) {
  if (count >= max || !live) return count;
  unsigned pointer = word(object + 0x20);
  size_t offset = COLLISION_BANK + (pointer & 0x7fff);
  if (pointer < 0x8000 || !rom || offset + 4 > rom_size) return count;
  const uint8_t *r = rom + offset;
  if (r[0] == 0xff && r[1] == 0xff && r[2] == 0xff && r[3] == 0xff) return count;
  int center = (int8_t)r[0];
  if (object[0x11] & 0x40) center = -center;
  out[count].world_x = (int16_t)word(object + 5) + center - r[2];
  out[count].world_y = (int16_t)word(object + 8) + (int8_t)r[1] - r[3];
  out[count].w = (uint16_t)(r[2] * 2u + 1u);
  out[count].h = (uint16_t)(r[3] * 2u + 1u);
  out[count].rgb555 = color;
  return count + 1;
}

static unsigned shots(MmxRenderDebugRect *out, unsigned max, unsigned count,
                      const uint8_t *pool, const uint8_t *rom, size_t size) {
  for (unsigned i = 0; i < 8; ++i) {
    const uint8_t *shot = pool + i * SLOT;
    count = append(out, max, count, shot, shot[0] != 0, rom, size,
                   melee_tag(word(shot + 0x3e)) ? MMX_HITBOX_MELEE : MMX_HITBOX_SHOT);
  }
  return count;
}

unsigned MmxHitboxOverlayCollect(const uint8_t *ram, const uint8_t *rom,
                                 size_t rom_size,
                                 const uint8_t *partner_body,
                                 const uint8_t *partner_shots,
                                 MmxRenderDebugRect *out, unsigned max) {
  unsigned count = 0;
  if (!ram || !rom || !out) return 0;
  for (unsigned d = ENEMY_FIRST; d < SHOT_FIRST; d += SLOT)
    count = append(out, max, count, ram + d, ram[d] != 0, rom, rom_size, MMX_HITBOX_ENEMY);
  for (unsigned d = HAZARD_FIRST; d < HAZARD_END; d += SLOT)
    count = append(out, max, count, ram + d, ram[d] != 0, rom, rom_size, MMX_HITBOX_HAZARD);
  count = append(out, max, count, ram + PLAYER, ram[PLAYER + 1] != 0, rom, rom_size,
                 MMX_HITBOX_PLAYER);
  if (partner_body)
    count = append(out, max, count, partner_body, partner_body[1] != 0, rom, rom_size,
                   MMX_HITBOX_PLAYER);
  count = shots(out, max, count, ram + SHOT_FIRST, rom, rom_size);
  if (partner_shots) count = shots(out, max, count, partner_shots, rom, rom_size);
  return count;
}

unsigned MmxHitboxOverlayProvide(MmxRenderDebugRect *out, unsigned max) {
  if (!enabled || !g_snes || !g_snes->cart) return 0;
  const uint8_t *body = NULL, *partner_shots = NULL;
  static MmxCoopState coop;
  coop = MmxCoopGetState();
  if (MmxCoopEnabled() && coop.initialized) {
    const MmxCoopPlayer *partner = &coop.players[coop.current ^ 1];
    if (partner->status == MMX_COOP_ALIVE) {
      body = partner->body;
      partner_shots = partner->shots;
    }
  }
  return MmxHitboxOverlayCollect(g_ram, g_snes->cart->rom, g_snes->cart->romSize,
                                 body, partner_shots, out, max);
}

void MmxHitboxOverlayBeginStockFrame(void) {
  stock_count = enabled ? MmxHitboxOverlayProvide(stock_rects, MAX_STOCK_RECTS) : 0;
  stock_camera_x = (int)word(g_ram + 0x1e4d);
  stock_camera_y = (int)word(g_ram + 0x1e50);
}

static void plot(uint32_t *pixels, int x, int y, uint32_t color) {
  if (x >= 0 && x < 256 && y >= 0 && y < 224) pixels[y * 256 + x] = color;
}

bool MmxHitboxOverlayDrawStock(uint32_t *pixels, const uint8_t *field) {
  if (!enabled || !stock_count || !pixels || !field) return false;
  memcpy(pixels, field, 256 * 224 * 4);
  for (unsigned i = 0; i < stock_count; ++i) {
    const MmxRenderDebugRect *r = &stock_rects[i];
    unsigned red = r->rgb555 & 31, green = r->rgb555 >> 5 & 31, blue = r->rgb555 >> 10 & 31;
    uint32_t color = (red << 3 | red >> 2) << 16 | (green << 3 | green >> 2) << 8 |
        (blue << 3 | blue >> 2);
    int left = r->world_x - stock_camera_x, top = r->world_y - stock_camera_y;
    int right = left + r->w - 1, bottom = top + r->h - 1;
    for (int x = left; x <= right; ++x) { plot(pixels, x, top, color); plot(pixels, x, bottom, color); }
    for (int y = top; y <= bottom; ++y) { plot(pixels, left, y, color); plot(pixels, right, y, color); }
  }
  return true;
}
