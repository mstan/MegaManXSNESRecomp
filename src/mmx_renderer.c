#include "mmx_renderer.h"
#include "mmx_display.h"
#include "mmx_wide_policy.h"
#include "mmx_render_assets.h"
#include "mmx_zero.h"
#include "mmx_knc_bugfix.h"
#include "mmx_weapons.h"
#include "mmx_weapon_combat.h"
#include "mmx_coop_view.h"
#include <math.h>

/* Mode-1 decode/composition follows SuperMetroidRecomp's sm_renderer.c.
 * The PPU remains native. Immutable raster snapshots are the only inputs to
 * presentation; this module never calls guest code or the PPU renderer. */
typedef struct Raster {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high_oam[32];
} Raster;
typedef struct Piece {
  int16_t x, y; uint16_t attr; uint8_t size, animation;
  /* Palette occupies bits 1..3; the five spare bits retain charged-shot
   * pose 0..20 at submission, without changing the capture record size. */
  uint8_t tile, palette_pose; uint16_t object;
} Piece;
static unsigned piece_pose(const Piece *p) {
  return (p->palette_pose&1)|((p->palette_pose>>3)&30);
}
enum { MAX_PIECES = 2048 };
typedef struct Frame {
  Raster lines[224];
  uint8_t ram[0x20000];
  uint32_t stock[256 * 224];
  Piece pieces[MAX_PIECES];
  unsigned piece_count, captured;
  bool valid;
  Piece expanded[MAX_PIECES];
  unsigned expanded_count;
  bool expand;
} Frame;
static Frame frame;

typedef struct PilotOverlayAlignment {
  bool evaluated;
  bool valid;
  int dx;
  int dy;
} PilotOverlayAlignment;

static PilotOverlayAlignment pilot_overlay_alignment;

static void reset_pilot_overlay_alignment(void) {
  memset(&pilot_overlay_alignment, 0, sizeof(pilot_overlay_alignment));
}

static MmxZeroState frame_zero;
static MmxWeaponsState frame_weapons;
static MmxWeaponCombatState frame_weapon_combat;
static MmxCoopState frame_coop;
static bool frame_held;
static int peer_seat=-1,view_dx,view_dy;
void MmxRendererSetPeerView(int seat) { peer_seat=seat>=0 && seat<2?seat:-1; }
static int view_camera(const uint8_t *r,unsigned address) {
  return (int)(r[address]|r[address+1]<<8)+(address==0x1e4d?view_dx:view_dy);
}
void MmxRendererHoldFrame(bool held) {frame_held=held;}
static uint8_t partner_ram[0x20000];
static Raster partner_raster;
static Piece building[MAX_PIECES], latched[MAX_PIECES];
static unsigned building_count, latched_count;
static uint8_t building_stage, latched_stage;
static Piece expanded_building[MAX_PIECES], expanded_latched[MAX_PIECES];
static unsigned expanded_building_count, expanded_latched_count;
static uint16_t current_object;
static bool observed_lists;
static const uint8_t *rom;
static size_t rom_size;
static MmxRenderStats stats;
static MmxRendererPlayerOverlayProvider player_overlay_provider;
static MmxRenderPlayerOverlay frame_player_overlay;
/* The co-op partner seat's Zero, drawn from partner_ram rather than OAM. */
static MmxRenderPlayerOverlay frame_partner_overlay;
enum { MAX_WORLD_SPRITES = 8 };
static MmxRendererWorldSpriteProvider world_sprite_provider;
static MmxRenderWorldSprite frame_world_sprites[MAX_WORLD_SPRITES];
static unsigned frame_world_sprite_count;
enum { MAX_DEBUG_RECTS = 64 };
static MmxRendererDebugRectProvider debug_rect_provider;
static MmxRendererDebugRectProvider hitbox_overlay_provider;
static MmxRenderDebugRect frame_debug_rects[MAX_DEBUG_RECTS];
static unsigned frame_debug_rect_count;
static uint8_t door_cache[512 * 512];
static int airport_sky_width;
typedef struct SubmarineBody {
  int left, top, bottom;
  unsigned shift;
  bool hidden, scrolling;
} SubmarineBody;
static SubmarineBody submarine_bodies[16];
static unsigned submarine_count;
bool g_mmx_custom_renderer;
bool g_mmx_custom_hud = true;
static bool coop_hud_compact = true;
void MmxRendererSetCompactCoopHud(bool compact) { coop_hud_compact=compact; }
bool g_mmx_expanded_sprites;
bool g_mmx_render_asset_repairs = true;
MmxRenderAspect g_mmx_custom_aspect = MMX_ASPECT_ADAPTIVE;
MmxRenderView g_mmx_custom_view = {342, 43, 16.0 / 9.0};

MmxRenderView MmxRendererViewport(MmxRenderAspect mode, int w, int h,
                                  SnesDisplayAspect display_aspect) {
  double aspect = mode == MMX_ASPECT_16_9 ? 16.0 / 9.0 :
                  mode == MMX_ASPECT_21_9 ? 21.0 / 9.0 :
                  mode == MMX_ASPECT_32_9 ? 32.0 / 9.0 :
                  w > 0 && h > 0 ? (double)w / h : 16.0 / 9.0;
  SnesDisplayFrame frame = SnesDisplayAspect_ComputeAdaptiveFrame(
      256, MMX_RENDER_HEIGHT, MMX_RENDER_MAX_WIDTH, aspect, display_aspect);
  return (MmxRenderView){frame.width, frame.extra, frame.aspect};
}
MmxDisplayViewport MmxRendererDestination(MmxRenderView view, int width, int height) {
  return SnesDisplayAspect_FitViewport(view.aspect, width, height);
}
static unsigned word(const uint8_t *p, unsigned a) { return p[a] | (p[a + 1] << 8); }
static bool zero_weapons_menu(void) {
  return MmxZeroEnabled() && !frame_zero.active_x && frame.ram[0xd1] == 2 && frame.ram[0xd2] == 4 &&
      (((frame.ram[0x1f10] == 6 || frame.ram[0x1f10] == 8) && (frame.ram[0xc3] & 128)) ||
       (frame.ram[0x1989] == 1 && word(frame.ram,0x198d) == 128 &&
        word(frame.ram,0x1990) == 160 && (frame.ram[0x199e] == 0 || frame.ram[0x199e] == 0x18)));
}
static bool zero_title_menu(void) {
  /* Retail title-menu cursor is the player object, at one of three rows.
   * Its native idle/firing animation and selection beam remain active. */
  unsigned y = word(frame.ram,0xbb0);
  return MmxZeroEnabled() && !frame_zero.active_x && frame.ram[0xd1] == 0 && frame.ram[0xba9] == 2 &&
      frame.ram[0xbbe] == 0 && word(frame.ram,0xbad) == 32 &&
      y >= 166 && y <= 198;
}
/* X1's buster charge cycles palette 1 in CGRAM itself while its sparkle
 * objects (class 1, $0C98..$0E17, $82:82ED) are live. The co-op weapon
 * palette would replace that glow with the plain body colours. */
static bool x_charging(const uint8_t *ram) {
  for (unsigned d = 0xc98; d < 0xe18; d += 32) if (ram[d] && ram[d + 10] == 1) return true;
  return false;
}
static bool zero_actor(unsigned object, unsigned animation) {
  return object == 0xba8 || object == 0xc38 || object == 0xc58 || object == 0xc78 ||
      (object == 0x1928 && animation == 0x5f) || (object == 0x1948 && animation == 0x5e) ||
      (object == 0x1968 && animation == 0x5d) ||
      (object == 0x1988 && (animation == 0 || animation == 0x18));
}
static const uint8_t *rom_at(unsigned address, size_t length) {
  if ((address & 0xffff) < 0x8000) return NULL;
  size_t offset = ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
  return rom && offset <= rom_size && length <= rom_size - offset ? rom + offset : NULL;
}
void MmxRendererSetRom(const uint8_t *bytes, size_t length) {
  rom = bytes; rom_size = length; MmxRenderAssetsSetRom(bytes, length);
}
void MmxRendererSetPlayerOverlayProvider(
    MmxRendererPlayerOverlayProvider provider) {
  player_overlay_provider = provider;
  if (!provider) {
    memset(&frame_player_overlay, 0, sizeof(frame_player_overlay));
    memset(&frame_partner_overlay, 0, sizeof(frame_partner_overlay));
  }
}
MmxRenderPlayerOverlay MmxRendererPlayerOverlaySnapshot(void) {
  return frame_player_overlay;
}
void MmxRendererSetWorldSpriteProvider(MmxRendererWorldSpriteProvider provider) {
  world_sprite_provider = provider;
  if (!provider) frame_world_sprite_count = 0;
}
unsigned MmxRendererWorldSpriteSnapshot(MmxRenderWorldSprite *out,
                                        unsigned max) {
  unsigned count = frame_world_sprite_count < max ? frame_world_sprite_count : max;
  if (out && count)
    memcpy(out, frame_world_sprites, count * sizeof(*out));
  return count;
}
void MmxRendererSetHitboxOverlayProvider(MmxRendererDebugRectProvider provider) {
  hitbox_overlay_provider = provider;
  if (!provider && !debug_rect_provider) frame_debug_rect_count = 0;
}
void MmxRendererSetDebugRectProvider(MmxRendererDebugRectProvider provider) {
  debug_rect_provider = provider;
  if (!provider) frame_debug_rect_count = 0;
}
unsigned MmxRendererDebugRectSnapshot(MmxRenderDebugRect *out,
                                      unsigned max) {
  unsigned count = frame_debug_rect_count < max ? frame_debug_rect_count : max;
  if (out && count)
    memcpy(out, frame_debug_rects, count * sizeof(*out));
  return count;
}
void MmxRendererReset(void) {
  memset(&frame_zero, 0, sizeof(frame_zero));
  memset(&frame_player_overlay, 0, sizeof(frame_player_overlay));
  memset(&frame_partner_overlay, 0, sizeof(frame_partner_overlay));
  reset_pilot_overlay_alignment();
  frame_world_sprite_count = 0;
  frame_debug_rect_count = 0;
  frame.valid = false; frame.captured = 0;
  building_count = latched_count = 0;
  building_stage = latched_stage = 0xff;
  expanded_building_count = expanded_latched_count = 0;
  current_object = 0; observed_lists = false;
}
static Piece make_piece(const uint8_t *p, int x, int y, unsigned flip,
                        unsigned attributes, unsigned base, unsigned animation, unsigned object, unsigned pose) {
  int size = p[4] & 0x20 ? 16 : 8;
  x += flip & 0x40 ? -(int8_t)p[1] - size : (int8_t)p[1];
  y += flip & 0x80 ? -(int8_t)p[2] - size : (int8_t)p[2];
  unsigned attr = (((p[4] & 0xce) | attributes) ^ flip) << 8;
  attr |= (p[3] + base) & 255;
  unsigned retained_pose=(animation==0x0e || animation==0x9e) ? pose&31 : 0;
  return (Piece){(int16_t)x, (int16_t)y, (uint16_t)attr, (uint8_t)size,
      (uint8_t)animation, p[3], (uint8_t)((p[4]&14)|(retained_pose&1)|((retained_pose&30)<<3)), (uint16_t)object};
}
static const uint8_t *sprite_arrangement(unsigned animation, unsigned f) {
  const uint8_t *pointer = rom_at(0x8d8000 + animation * 3, 3);
  if (!pointer) return NULL;
  unsigned address = word(pointer, 0) | (pointer[2] << 16);
  pointer = rom_at(address + f * 3, 3);
  if (!pointer) return NULL;
  address = word(pointer, 0) | (pointer[2] << 16);
  const uint8_t *arrangement = rom_at(address, 1);
  return arrangement && rom_at(address, 1 + arrangement[0] * 4) ? arrangement : NULL;
}
static void expand_object(const uint8_t *ram, unsigned object) {
  if (object < 0x20 || object > 0x1fe0) return;
  unsigned animation = ram[object + 0x16], f = ram[object + 0x17] & 127;
  const uint8_t *arrangement = sprite_arrangement(animation, f);
  if (!arrangement) return;
  int x = (int16_t)(word(ram, object + 5) - word(ram, 0x1e4d));
  int y = (int16_t)(word(ram, object + 8) + (int8_t)ram[object + 0x19] - word(ram, 0x1e50));
  unsigned base = MmxWidePolicy_CrusherTileBase(ram, (uint16_t)object, ram[object + 0x18]);
  for (unsigned i = 0; i < arrangement[0] && expanded_building_count < MAX_PIECES; ++i)
    expanded_building[expanded_building_count++] = make_piece(arrangement + i * 4, x, y,
        ram[object + 0x11] & 0x40, ram[object + 0x11] & 0x3f, base, animation, object, f);
}
static bool fortress_sound_actor(unsigned object) {
  /* $88:D359 uses Zero's previous pose while playing the offscreen room
   * sounds. It still owns a release timer; suppress only its margin art. */
  const uint8_t *r = frame.ram;
  return r[0x1f7a] == 9 && object >= 0xe68 && object < 0x1228 &&
      (object & 63) == 0x28 && r[object + 10] == 0x66 &&
      r[object + 1] == 2 && r[object + 2] == 6;
}
static unsigned fortress_waiting_pieces(Piece out[128], const Piece *pieces, unsigned piece_count) {
  const uint8_t *r = frame.ram;
  unsigned camera = word(r, 0x1e4d);
  if (r[0x1f7a] != 9 || r[0x1f08] != 4 || r[0x1f7d] >= 2 ||
      camera < 0x900 || camera > 0xa80 || word(r, 0x1e50) != 0x500) return 0;
  bool live[3] = {false, false, false};
  for (unsigned d = 0xe68; d < 0x1228; d += 64) if (r[d]) {
    bool submitted = false;
    for (unsigned i = 0; i < piece_count; ++i) submitted |= pieces[i].object == d;
    /* Initializers set their states one frame before submitting art. Keep
     * the preview through that gap, but never replace later hidden poses. */
    if (r[d + 10] == 0x67 && r[d + 1])
      live[0] = submitted || r[d + 1] != 2 || r[d + 2] != 0x1a || r[d + 3] != 0;
    if (r[d + 10] == 0x66 && r[d + 1] == 2 && r[d + 2] >= 8 && r[d + 3])
      live[1] = submitted || r[d + 2] != 8 || r[d + 3] != 2;
    if (r[d + 10] == 0x64 && r[d + 1] >= 4) live[2] = true;
  }
  /* $87:EDBB creates a separate looping electrical effect (kind 1, $10/$2E).
   * Its later allocation must not leave an empty cage between the previews
   * and native actors. Once submitted, retain its original animation timing. */
  for (unsigned d = 0x1928; d < 0x1d08; d += 32)
    if (r[d] && r[d + 10] == 0x10 && r[d + 11] == 0x2e && r[d + 22] == 0x9d)
      for (unsigned i = 0; i < piece_count; ++i) live[2] |= pieces[i].object == d;
  /* $83:E858 and $88:D3DB start these waiting poses. Section 4 has loaded
   * the shared resources; Zero's pose needs its private dynamic CHR transfer.
   * Preview only the margins until each real actor submits art: changing
   * native allocation order here breaks the Vile/Zero cutscene handoff. */
  unsigned count = 0;
  for (unsigned actor = 0; actor < 3; ++actor) if (!live[actor]) {
    unsigned animation = actor == 2 ? 0x9d : actor ? 0x53 : 0x52;
    /* Zero's $11 sequence blinks every six frames. The cage's $01 sequence
     * cycles through frames $01..$0F, one frame each. */
    unsigned pose = actor == 2 ? 1 + word(r, 0xb9c) % 15 :
                    actor ? 0x20 + (word(r, 0xb9c) % 12 >= 6) : 0;
    const uint8_t *a = sprite_arrangement(animation, pose);
    if (!a) continue;
    int x = actor == 2 ? 0xb5f : actor ? 0xb60 : 0xb30;
    int y = actor == 2 ? 0x58b : 0x58e;
    unsigned attributes = actor == 2 ? r[0x18396] : actor ? 0x2c : 0x29;
    for (unsigned i = 0; i < a[0] && count < 128; ++i)
      out[count++] = make_piece(a + i * 4, x - (int)camera,
          y - (int)word(r, 0x1e50), 0, attributes, actor == 2 ? r[0x18296] : 0, animation, 0, pose);
  }
  return count;
}
static void expand_queues(const uint8_t *ram) {
  /* D56F's actual six priority queues, captured before D6A7 can exhaust OAM.
   * Keep its order: queues 0..2, weapon objects, X, queues 3..5. No arbitrary
   * scan of dormant object slots and no additional guest objects or writes. */
  for (unsigned group = 0; group < 6; ++group) {
    if (group == 3) {
      for (unsigned d = 0xc38; d <= 0xc78; d += 0x20)
        if (ram[d] && ram[d + 14]) expand_object(ram, d);
      if (ram[0xbb6]) expand_object(ram, 0xba8);
    }
    unsigned count = ram[0xe7 + group];
    if (count > 32) count = 32;
    for (unsigned i = 0; i < count; ++i) expand_object(ram, word(ram, 0x920 + group * 64 + i * 2));
  }
}
void MmxRendererObserveObject(const uint8_t ram[0x20000], uint16_t object) {
  if (!g_mmx_custom_renderer || !ram) return;
  if (building_stage != ram[0x1f7a]) {
    building_count = expanded_building_count = 0; observed_lists = false;
    building_stage = ram[0x1f7a];
  }
  current_object = object;
  if (observed_lists || (!g_mmx_expanded_sprites && !MmxCoopViewsOnline())) return;
  observed_lists = true;
  expand_queues(ram);
}
MmxRendererPieceMark MmxRendererMarkPieces(void) {
  return (MmxRendererPieceMark){building_count,expanded_building_count,current_object,building_stage,observed_lists};
}
void MmxRendererRewindPieces(MmxRendererPieceMark mark) {
  if (mark.building<=building_count) building_count=mark.building;
  if (mark.expanded<=expanded_building_count) expanded_building_count=mark.expanded;
  current_object=mark.object;building_stage=mark.stage;observed_lists=mark.observed;
}
void MmxRendererRecordPiece(const uint8_t ram[0x20000], uint16_t d) {
  if (!g_mmx_custom_renderer || !ram || d > 0xffe0 || building_count >= MAX_PIECES) return;
  if (building_stage != ram[0x1f7a]) {
    building_count = 0;
    building_stage = ram[0x1f7a];
  }
  unsigned pointer = word(ram, d + 0x18) | (ram[d + 0x1a] << 16);
  const uint8_t *p = rom_at(pointer, 5);
  if (!p) return;
  int x = (int16_t)word(ram, d), y = (int16_t)word(ram, d + 2);
  unsigned animation = current_object && current_object < 0x1fe0 ? ram[current_object + 0x16] : 255;
  building[building_count++] = make_piece(p, x, y, ram[d + 0xb], ram[d + 0xf],
                                         ram[d + 0x10], animation, current_object,
                                         current_object && current_object<0x1fe0 ? ram[current_object+23]&127 : 0);
}
void MmxRendererLatchSprites(void) {
  if ((frame_held || MmxZeroSwapping() || MmxWeaponsTimeActive()) && !building_count) return;
  /* Menu fades suspend OAM construction but the PPU keeps its previous
   * sprites. Retain only Zero's attribution; drawing still requires an exact
   * match in that raster's live OAM, so a real hidden blink stays hidden. */
  if (MmxZeroEnabled() && !building_count && latched_stage == building_stage) {
    unsigned count = 0;
    for (unsigned i = 0; i < latched_count; ++i)
      if (zero_actor(latched[i].object,latched[i].animation)) latched[count++] = latched[i];
    latched_count = count;
  } else {
    latched_count = building_count;
    latched_stage = building_stage;
    memcpy(latched, building, building_count * sizeof(*building));
  }
  building_count = 0;
  expanded_latched_count = expanded_building_count;
  memcpy(expanded_latched, expanded_building, expanded_building_count * sizeof(Piece));
  expanded_building_count = 0; observed_lists = false; current_object = 0;
}
static void trace_objects(const uint8_t *ram) {
  static FILE *log;
  static bool checked;
  static unsigned tick, previous[16];
  ++tick;
  if (!checked) {
    checked = true;
    const char *path = getenv("MMX_RENDER_OBJECT_TRACE");
    if (path && *path) log = fopen(path, "w");
    if (log) fputs("frame,object,state,camera,player_x,enemy_x,enemy_y,pieces,stage,id,health,player_y\n", log);
  }
  unsigned stage = ram[0x1f7a];
  if (!log) return;
  for (unsigned i = 0; i < 16; ++i) {
    unsigned d = i == 15 ? 0xe18 : 0xe68 + i * 64;
    bool selected = i == 15 ? stage == 8 :
        MmxWidePolicy_IsBossEncounter(ram[d + 10]) ||
        (stage == 8 && ram[d + 10] == 0x36) || (stage == 6 && ram[d + 10] == 0x37);
    unsigned state = ram[d] && selected ? ram[d + 1] + 1u : 0;
    if (state == previous[i] && (!state || (tick & 15))) continue;
    previous[i] = state;
    fprintf(log, "%u,%04x,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", tick, d, (int)state - 1,
        word(ram, 0x1e4d), word(ram, 0xbad), word(ram, d + 5), word(ram, d + 8), latched_count,
        stage, ram[d + 10], ram[d + 0x27], word(ram, 0xbb0));
    fflush(log);
  }
}
void MmxRendererBeginFrame(const uint8_t ram[0x20000]) {
  MmxRenderPlayerOverlay overlay;
  MmxRenderWorldSprite world_sprites[MAX_WORLD_SPRITES];
  MmxRenderDebugRect debug_rects[MAX_DEBUG_RECTS];
  unsigned world_sprite_count = 0;
  unsigned debug_rect_count = 0;
  reset_pilot_overlay_alignment();
  memset(&frame_coop,0,sizeof(frame_coop));
  memset(&frame_partner_overlay, 0, sizeof(frame_partner_overlay));
  memset(&overlay, 0, sizeof(overlay));
  MmxZeroState live_zero = MmxZeroGetState();
  if (player_overlay_provider && player_overlay_provider(ram, &live_zero, &overlay))
    frame_player_overlay = overlay;
  else
    memset(&frame_player_overlay, 0, sizeof(frame_player_overlay));
  memset(world_sprites, 0, sizeof(world_sprites));
  if (world_sprite_provider)
    world_sprite_count = world_sprite_provider(world_sprites,
                                               MAX_WORLD_SPRITES);
  if (world_sprite_count > MAX_WORLD_SPRITES)
    world_sprite_count = MAX_WORLD_SPRITES;
  frame_world_sprite_count = world_sprite_count;
  if (world_sprite_count)
    memcpy(frame_world_sprites, world_sprites,
           world_sprite_count * sizeof(*frame_world_sprites));
  memset(debug_rects, 0, sizeof(debug_rects));
  /* The developer overlay outlines every object, a superset of any
   * mod's own debug boxes, so it takes the frame when both are on. */
  MmxRendererDebugRectProvider rects =
      hitbox_overlay_provider ? hitbox_overlay_provider : debug_rect_provider;
  if (rects)
    debug_rect_count = rects(debug_rects, MAX_DEBUG_RECTS);
  if (debug_rect_count > MAX_DEBUG_RECTS)
    debug_rect_count = MAX_DEBUG_RECTS;
  frame_debug_rect_count = debug_rect_count;
  if (debug_rect_count)
    memcpy(frame_debug_rects, debug_rects,
           debug_rect_count * sizeof(*frame_debug_rects));
  trace_objects(ram);
  frame.valid = false; frame.captured = 0;
  memcpy(frame.ram, ram, sizeof(frame.ram));
  frame_zero = MmxZeroGetState();
  frame_weapons = MmxWeaponsGetState();
  frame_weapon_combat = MmxWeaponsGetCombatState();
  if ((frame_held || MmxZeroSwapping() || MmxWeaponsTimeActive()) && !latched_count) {
    /* A mid-swap load has no preceding submission. Rebuild the frozen
     * native queues, including margin actors, before matching live OAM. */
    expanded_building_count = 0; expand_queues(ram);
    latched_stage = ram[0x1f7a];
    latched_count = expanded_latched_count = expanded_building_count;
    memcpy(latched,expanded_building,latched_count * sizeof(Piece));
    memcpy(expanded_latched,expanded_building,latched_count * sizeof(Piece));
    expanded_building_count = 0;
  }
  frame.piece_count = latched_stage == ram[0x1f7a] ? latched_count : 0;
  memcpy(frame.pieces, latched, frame.piece_count * sizeof(*latched));
  if (MmxZeroEnabled()) {
    /* Loading a snapshot can begin on a fade before the first observed
     * submission. Reconstruct candidates, then match them against live OAM. */
    const unsigned actors[] = {0xba8,0xc38,0xc58,0xc78,0x1928,0x1948,0x1968,0x1988};
    for (unsigned j = 0; j < sizeof(actors)/sizeof(actors[0]); ++j) {
      unsigned d = actors[j]; bool found = false;
      if (d >= 0x1928 && !zero_weapons_menu()) continue;
      for (unsigned i = 0; i < frame.piece_count; ++i) found |= frame.pieces[i].object == d;
      if (found || !ram[d + 14]) continue;
      const uint8_t *a = sprite_arrangement(ram[d + 22],ram[d + 23] & 127);
      if (!a) continue;
      int x = (int16_t)(word(ram,d + 5) - word(ram,0x1e4d));
      int y = (int16_t)(word(ram,d + 8) + (int8_t)ram[d + 25] - word(ram,0x1e50));
      if (d >= 0x1928 && zero_weapons_menu()) { x = word(ram,d + 5); y = word(ram,d + 8); }
      for (unsigned i = 0; i < a[0] && frame.piece_count < MAX_PIECES; ++i)
        frame.pieces[frame.piece_count++] = make_piece(a + i * 4,x,y,ram[d + 17] & 64,
            ram[d + 17] & 63,ram[d + 24],ram[d + 22],d,ram[d+23]&127);
    }
  }
  frame.expanded_count = latched_stage == ram[0x1f7a] ? expanded_latched_count : 0;
  memcpy(frame.expanded, expanded_latched, frame.expanded_count * sizeof(Piece));
  frame.expand = g_mmx_expanded_sprites || MmxCoopViewsOnline();
  if (MmxZeroEnabled() && frame.expand) for (unsigned i = 0; i < frame.piece_count; ++i) {
    Piece p = frame.pieces[i]; if (!zero_actor(p.object,p.animation)) continue;
    bool found = false;
    for (unsigned j = 0; j < expanded_latched_count; ++j) found |= frame.expanded[j].object == p.object;
    if (!found && frame.expanded_count < MAX_PIECES) frame.expanded[frame.expanded_count++] = p;
  }
}
static bool pilot_overlay_oam_match(const Piece *piece) {
  for (unsigned line = 0; line < 224; ++line) {
    const Raster *r = &frame.lines[line];
    for (unsigned slot = 16; slot < 128; ++slot) {
      unsigned pos = r->oam[slot * 2];
      unsigned hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
      int ox = (pos & 255) | ((hi & 1) << 8);
      if (ox >= 256) ox -= 512;
      if (ox == piece->x && (pos >> 8) == ((unsigned)piece->y & 255) &&
          r->oam[slot * 2 + 1] == piece->attr) return true;
    }
  }
  return false;
}
static void align_wide_pilot_overlay(const MmxRenderPlayerOverlay *overlay,
                                     unsigned animation, int *zx, int *zy) {
  const MmxRenderPlayerOverlayPlane *plane = overlay ? &overlay->body : NULL;
  int native_left = 10000, native_top = 10000;
  int native_right = -10000, native_bottom = -10000;
  int overlay_left = 10000, overlay_top = 10000;
  int overlay_right = -10000, overlay_bottom = -10000;
  if (!plane || !plane->pixels || plane->width <= MMX_ZERO_WIDTH ||
      !plane->height || !overlay->palette || !overlay->palette_count ||
      !zx || !zy) return;
  if (pilot_overlay_alignment.evaluated) {
    if (pilot_overlay_alignment.valid) {
      *zx += pilot_overlay_alignment.dx;
      *zy += pilot_overlay_alignment.dy;
    }
    return;
  }
  pilot_overlay_alignment.evaluated = true;
  for (unsigned i = 0; i < frame.piece_count; ++i) {
    const Piece *piece = &frame.pieces[i];
    if (piece->object != 0xba8 || piece->animation != animation ||
        !pilot_overlay_oam_match(piece)) continue;
    if (piece->x < native_left) native_left = piece->x;
    if (piece->y < native_top) native_top = piece->y;
    if (piece->x + piece->size - 1 > native_right)
      native_right = piece->x + piece->size - 1;
    if (piece->y + piece->size - 1 > native_bottom)
      native_bottom = piece->y + piece->size - 1;
  }
  if (native_left > native_right || native_top > native_bottom) return;
  native_left -= view_dx;
  native_right -= view_dx;
  native_top -= view_dy;
  native_bottom -= view_dy;
  for (int row = 0; row < plane->height; ++row) for (int col = 0; col < plane->width; ++col) {
    unsigned pixel = plane->pixels[row * plane->width + col];
    int x, y;
    if (!pixel || pixel >= overlay->palette_count) continue;
    x = *zx + (overlay->facing_left ? 63 - (plane->origin_x + col) :
        plane->origin_x + col - 64);
    y = *zy - 64 + plane->origin_y + row;
    if (x < overlay_left) overlay_left = x;
    if (y < overlay_top) overlay_top = y;
    if (x > overlay_right) overlay_right = x;
    if (y > overlay_bottom) overlay_bottom = y;
  }
  if (overlay_left > overlay_right || overlay_top > overlay_bottom) return;
  /* Align the authored cockpit canvas by its visible bounds. The native pilot
   * OAM is the stable pose anchor; the player/armor positions are the same
   * world point, but are not the visible Zero anchor inside the wide canvas. */
  pilot_overlay_alignment.dx =
      (native_left + native_right - overlay_left - overlay_right) / 2;
  pilot_overlay_alignment.dy =
      (native_top + native_bottom - overlay_top - overlay_bottom) / 2;
  pilot_overlay_alignment.valid = true;
  *zx += pilot_overlay_alignment.dx;
  *zy += pilot_overlay_alignment.dy;
}
/* Kept out of the public header: the Saber ROM test uses the renderer's
 * captured OAM and the translation actually applied by Draw to assert the
 * real presentation result, without duplicating the alignment decision. */
bool MmxRendererRidePilotBoundsForTest(int native_box[4], int drawn_box[4]) {
  const MmxRenderPlayerOverlayPlane *plane = &frame_player_overlay.body;
  int native_left = 10000, native_top = 10000;
  int native_right = -10000, native_bottom = -10000;
  int overlay_left = 10000, overlay_top = 10000;
  int overlay_right = -10000, overlay_bottom = -10000;
  int zx, zy;
  unsigned animation;
  if (!native_box || !drawn_box || !frame.valid ||
      !frame_player_overlay.active || frame.ram[0x0baa] != 0x2c ||
      !plane->pixels || !plane->width || !plane->height ||
      !frame_player_overlay.palette || !frame_player_overlay.palette_count ||
      !pilot_overlay_alignment.valid) return false;
  animation = frame.ram[0x0bbe];
  if (animation != 0x6a && animation != 0x6b) return false;
  for (unsigned i = 0; i < frame.piece_count; ++i) {
    const Piece *piece = &frame.pieces[i];
    if (piece->object != 0xba8 || piece->animation != animation ||
        !pilot_overlay_oam_match(piece)) continue;
    if (piece->x - view_dx < native_left) native_left = piece->x - view_dx;
    if (piece->y - view_dy < native_top) native_top = piece->y - view_dy;
    if (piece->x + piece->size - 1 - view_dx > native_right)
      native_right = piece->x + piece->size - 1 - view_dx;
    if (piece->y + piece->size - 1 - view_dy > native_bottom)
      native_bottom = piece->y + piece->size - 1 - view_dy;
  }
  if (native_left > native_right || native_top > native_bottom) return false;
  zx = (int16_t)(word(frame.ram, 0x0bad) - view_camera(frame.ram, 0x1e4d));
  zy = (int16_t)(word(frame.ram, 0x0bb0) - view_camera(frame.ram, 0x1e50)) +
      MmxZeroPoseOffsetY(frame.ram);
  for (int row = 0; row < plane->height; ++row) for (int col = 0; col < plane->width; ++col) {
    unsigned pixel = plane->pixels[row * plane->width + col];
    int x, y;
    if (!pixel || pixel >= frame_player_overlay.palette_count) continue;
    x = zx + (frame_player_overlay.facing_left ?
        63 - (plane->origin_x + col) : plane->origin_x + col - 64);
    y = zy - 64 + plane->origin_y + row;
    if (x < overlay_left) overlay_left = x;
    if (y < overlay_top) overlay_top = y;
    if (x > overlay_right) overlay_right = x;
    if (y > overlay_bottom) overlay_bottom = y;
  }
  if (overlay_left > overlay_right || overlay_top > overlay_bottom) return false;
  native_box[0] = native_left;
  native_box[1] = native_top;
  native_box[2] = native_right;
  native_box[3] = native_bottom;
  drawn_box[0] = overlay_left + pilot_overlay_alignment.dx;
  drawn_box[1] = overlay_top + pilot_overlay_alignment.dy;
  drawn_box[2] = overlay_right + pilot_overlay_alignment.dx;
  drawn_box[3] = overlay_bottom + pilot_overlay_alignment.dy;
  return true;
}
/* Test-only orientation probe. The native pilot's OAM pieces should share one
 * horizontal-flip bit; expose that captured bit beside the overlay mirror so
 * the ROM test can prove both facings use the same authored orientation. */
bool MmxRendererRidePilotFacingForTest(bool *native_hflip,
                                       bool *drawn_mirror) {
  bool found = false;
  bool hflip = false;
  unsigned animation;
  if (!native_hflip || !drawn_mirror || !frame.valid ||
      !frame_player_overlay.active || frame.ram[0x0baa] != 0x2c ||
      !pilot_overlay_alignment.valid)
    return false;
  animation = frame.ram[0x0bbe];
  if (animation != 0x6a && animation != 0x6b) return false;
  for (unsigned i = 0; i < frame.piece_count; ++i) {
    const Piece *piece = &frame.pieces[i];
    bool piece_hflip;
    if (piece->object != 0xba8 || piece->animation != animation ||
        !pilot_overlay_oam_match(piece)) continue;
    piece_hflip = (piece->attr & 0x4000) != 0;
    if (!found) {
      hflip = piece_hflip;
      found = true;
    } else if (hflip != piece_hflip) {
      return false;
    }
  }
  if (!found) return false;
  *native_hflip = hflip;
  *drawn_mirror = frame_player_overlay.facing_left;
  return true;
}
void MmxRendererCaptureLine(const Ppu *p, unsigned line) {
  if (!p || line < 1 || line > 224 || line != frame.captured + 1) return;
  Raster *r = &frame.lines[line - 1];
  memcpy(r->registers, p, sizeof(r->registers));
  memcpy(r->palette, p->cgram, sizeof(r->palette));
  memcpy(r->oam, p->oam, sizeof(r->oam));
  memcpy(r->high_oam, p->highOam, sizeof(r->high_oam));
  memcpy(r->vram, p->vram, sizeof(r->vram));
  ++frame.captured;
}
bool MmxRendererEndFrame(const uint32_t stock[256 * 224]) {
  if (!stock || frame.captured != 224) return false;
  memcpy(frame.stock, stock, sizeof(frame.stock));
  return frame.valid = true;
}
MmxRenderStats MmxRendererGetStats(void) { return stats; }
const uint32_t *MmxRendererStockFrame(void) { return frame.valid ? frame.stock : NULL; }
void MmxRendererCoopFrame(const MmxCoopState *s) {
  if (!s || !s->initialized) { memset(&frame_coop,0,sizeof(frame_coop)); return; }
  frame_coop = *s;
  bool menu=((frame.ram[0x1f10]==6 || frame.ram[0x1f10]==8) && (frame.ram[0xc3]&128)) ||
      (frame.ram[0x1989]==1 && word(frame.ram,0x198d)==128 && word(frame.ram,0x1990)==160 &&
       (frame.ram[0x199e]==0 || frame.ram[0x199e]==0x18));
  if (menu) {
    const MmxCoopPlayer *owner=&s->players[s->menu_last];
    frame_zero=owner->zero;frame_weapons=owner->weapons;frame_weapon_combat=owner->combat;
    memcpy(frame.ram+0xba8,owner->body,sizeof(owner->body));
    /* The menu shows its owner's body, which need not be the live seat's. */
    MmxRenderPlayerOverlay overlay;
    memset(&overlay,0,sizeof(overlay));
    if (player_overlay_provider && player_overlay_provider(frame.ram,&frame_zero,&overlay))
      frame_player_overlay=overlay;
    else memset(&frame_player_overlay,0,sizeof(frame_player_overlay));
  }
  memcpy(partner_ram,frame.ram,sizeof(partner_ram));
  memcpy(partner_ram+0xba8,s->players[s->current^1].body,sizeof(s->players[s->current^1].body));
  memcpy(partner_ram+0xc38,s->players[s->current^1].auxiliaries,sizeof(s->players[s->current^1].auxiliaries));
  memcpy(partner_ram+0x1228,s->players[s->current^1].shots,sizeof(s->players[s->current^1].shots));
  const MmxCoopPlayer *partner=&s->players[s->current^1];
  memset(&frame_partner_overlay,0,sizeof(frame_partner_overlay));
  if (!menu && partner->character==MMX_COOP_ZERO && player_overlay_provider) {
    MmxRenderPlayerOverlay overlay;
    memset(&overlay,0,sizeof(overlay));
    if (player_overlay_provider(partner_ram,&partner->zero,&overlay))
      frame_partner_overlay=overlay;
  }
}
bool MmxRendererSaveCapture(const char *path) {
  if (!frame.valid || !path) return false;
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  uint32_t header[] = {0x4d4d5843, frame_coop.initialized ? 17 : 16,
      sizeof(frame) + sizeof(frame_zero) + sizeof(frame_weapons) + sizeof(frame_weapon_combat) +
      (frame_coop.initialized ? sizeof(frame_coop) : 0)};
  bool ok = fwrite(header, sizeof(header), 1, f) == 1 && fwrite(&frame, sizeof(frame), 1, f) == 1 &&
      fwrite(&frame_zero, sizeof(frame_zero), 1, f) == 1 &&
      fwrite(&frame_weapons, sizeof(frame_weapons), 1, f) == 1 &&
      fwrite(&frame_weapon_combat, sizeof(frame_weapon_combat), 1, f) == 1;
  if (ok && frame_coop.initialized) ok = fwrite(&frame_coop,sizeof(frame_coop),1,f) == 1;
  return fclose(f) == 0 && ok;
}
bool MmxRendererLoadCapture(const char *path) {
  if (!path) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint32_t h[3] = {0};
  frame.valid = false;
  reset_pilot_overlay_alignment();
  memset(&frame_zero, 0, sizeof(frame_zero));
  memset(&frame_weapons, 0, sizeof(frame_weapons));
  memset(&frame_weapon_combat, 0, sizeof(frame_weapon_combat));
  memset(&frame_coop,0,sizeof(frame_coop));
  bool ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == 0x4d4d5843 &&
      ((h[1] == 2 && h[2] == sizeof(frame)) || (h[1] == 3 && h[2] == sizeof(frame) + MMX_ZERO_LEGACY_STATE_SIZE) ||
       (h[1] == 4 && h[2] == sizeof(frame) + MMX_ZERO_ANIMATION_STATE_SIZE) ||
       (h[1] == 5 && h[2] == sizeof(frame) + MMX_ZERO_COMBAT_STATE_SIZE) ||
       (h[1] == 6 && h[2] == sizeof(frame) + MMX_ZERO_SWAP_STATE_SIZE) ||
       (h[1] == 7 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE) ||
       (h[1] == 8 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + MMX_WEAPONS_LEGACY_STATE_SIZE) ||
       (h[1] == 9 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + MMX_WEAPONS_LEGACY_STATE_SIZE + MMX_WEAPON_COMBAT_LEGACY_SIZE) ||
       (h[1] == 10 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + sizeof(frame_weapons) + MMX_WEAPON_COMBAT_LEGACY_SIZE) ||
       (h[1] == 11 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + sizeof(frame_weapons) + MMX_WEAPON_COMBAT_DAMAGE_SIZE) ||
       (h[1] == 12 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + sizeof(frame_weapons) + sizeof(frame_weapon_combat)) ||
       (h[1] == 13 && h[2] == sizeof(frame) + MMX_ZERO_HEALTH_STATE_SIZE + sizeof(frame_weapons) + sizeof(frame_weapon_combat) + MMX_COOP_LEGACY_STATE_SIZE) ||
       (h[1] == 14 && h[2] == sizeof(frame) + sizeof(frame_zero) + sizeof(frame_weapons) + sizeof(frame_weapon_combat)) ||
       ((h[1] == 15 || h[1] == 17) && h[2] == sizeof(frame) + sizeof(frame_zero) + sizeof(frame_weapons) + sizeof(frame_weapon_combat) + sizeof(frame_coop)) ||
       (h[1] == 16 && h[2] == sizeof(frame) + sizeof(frame_zero) + sizeof(frame_weapons) + sizeof(frame_weapon_combat))) &&
      fread(&frame, sizeof(frame), 1, f) == 1 &&
      frame.captured == 224 && frame.piece_count <= MAX_PIECES && frame.expanded_count <= MAX_PIECES && frame.valid;
  size_t zero_size = h[1] == 3 ? MMX_ZERO_LEGACY_STATE_SIZE :
      h[1] == 4 ? MMX_ZERO_ANIMATION_STATE_SIZE :
      h[1] == 5 ? MMX_ZERO_COMBAT_STATE_SIZE :
      h[1] == 6 ? MMX_ZERO_SWAP_STATE_SIZE : h[1] < 14 ? MMX_ZERO_HEALTH_STATE_SIZE : sizeof(frame_zero);
  if (ok && h[1] >= 3) ok = fread(&frame_zero, zero_size, 1, f) == 1 &&
      frame_zero.active_x <= 1 && frame_zero.swap_phase <= 6 && frame_zero.swap_tick <= 30 &&
      frame_zero.swap_y >= -320 && frame_zero.swap_y <= 0 &&
      frame_zero.slash <= 46 && frame_zero.air <= 1 && frame_zero.anim_valid <= 1 && frame_zero.anim_pose < 117 &&
      frame_zero.burst <= 2 && (!frame_zero.burst || (frame_zero.burst_offset >= 272 &&
        frame_zero.burst_offset + 3 <= MMX_ZERO_ANIMATION_BYTES && frame_zero.burst_timer));
  if (ok && h[1] >= 8) ok = fread(&frame_weapons, h[1] >= 10 ? sizeof(frame_weapons) : MMX_WEAPONS_LEGACY_STATE_SIZE, 1, f) == 1 &&
      MmxWeaponsValidState(&frame_weapons);
  if (ok && h[1] >= 9) ok = fread(&frame_weapon_combat,
      h[1]>=12 ? sizeof(frame_weapon_combat) : h[1]==11 ? MMX_WEAPON_COMBAT_DAMAGE_SIZE : MMX_WEAPON_COMBAT_LEGACY_SIZE, 1, f) == 1 &&
      MmxWeaponsValidCombatState(&frame_weapon_combat);
  if (ok && h[1]<16) {
    /* Older captures only kept palette bits. Use their saved pose as the
     * best available approximation; all new captures retain submission. */
    for (unsigned list=0;list<2;++list) {
      Piece *pieces=list ? frame.expanded : frame.pieces;
      unsigned count=list ? frame.expanded_count : frame.piece_count;
      for (unsigned i=0;i<count;++i) {
        Piece *p=&pieces[i];
        if ((p->animation==0x0e || p->animation==0x9e) && p->object<0x1fe0) {
          unsigned pose=frame.ram[p->object+23]&31;
          p->palette_pose=(p->palette_pose&14)|(pose&1)|((pose&30)<<3);
        }
      }
    }
  }
  if (ok && (h[1] == 13 || h[1] == 15 || h[1] == 17)) {
    MmxCoopState coop;
    if (h[1] == 13) {
      uint8_t legacy[MMX_COOP_LEGACY_STATE_SIZE];
      ok = fread(legacy,sizeof(legacy),1,f) == 1;
      if (ok) MmxCoopImportLegacy(&coop,legacy);
    } else ok = fread(&coop,sizeof(coop),1,f) == 1;
    ok = ok && coop.initialized == 1;
    for (unsigned i=0;ok && i<2;++i) ok = coop.players[i].character <= MMX_COOP_ZERO &&
        coop.players[i].status <= MMX_COOP_FALLEN && MmxZeroValidState(&coop.players[i].zero) &&
        MmxWeaponsValidState(&coop.players[i].weapons) && MmxWeaponsValidCombatState(&coop.players[i].combat);
    if (ok) MmxRendererCoopFrame(&coop);
  }
  ok = ok && fgetc(f) == EOF;
  fclose(f); frame.valid = ok; return ok;
}

bool MmxRendererStageTile(const uint8_t ram[0x20000], unsigned layer,
                          int x, int y, uint16_t *entry) {
  if (!ram || !entry || layer > 1 || x < 0 || y < 0 || x >= 8192 || y >= 8192) return false;
  unsigned layout = layer ? 0xec00 : 0xe800;
  unsigned screens = layer ? 0xa600 : 0x2000;
  unsigned screen = ram[layout + (y >> 8) * 32 + (x >> 8)];
  unsigned address = (screens + screen * 512 + ((y & 0xf0) << 1) + ((x & 0xf0) >> 3)) & 0xffff;
  unsigned metatile = word(ram, address);
  unsigned pointer = 0xb95 + layer * 3;
  unsigned table = word(ram, pointer) | (ram[pointer + 2] << 16);
  address = (table & 0xff0000) | ((table + metatile * 8 + ((y & 8) ? 4 : 0) + ((x & 8) ? 2 : 0)) & 0xffff);
  const uint8_t *tile = rom_at(address, 2);
  if (!tile) return false;
  *entry = (uint16_t)word(tile, 0); return true;
}
static bool door_body(int x, int y) {
  if (x < 0 || y < 0 || x >= 8192 || y >= 8192) return false;
  unsigned key = (y >> 4) * 512 + (x >> 4);
  if (door_cache[key]) return door_cache[key] == 2;
  door_cache[key] = 1;
  x &= ~15; y &= ~15;
  for (int row = 0; row < 3; ++row) {
    uint16_t entries[3][4];
    bool valid = true;
    for (int r = 0; r < 3; ++r) for (int q = 0; q < 4; ++q)
      valid &= MmxRendererStageTile(frame.ram, 0, x + (q & 1) * 8,
                                     y + (r - row) * 16 + (q >> 1) * 8, &entries[r][q]);
    if (valid && MmxWidePolicy_IsBossDoorBody(entries, row)) { door_cache[key] = 2; return true; }
  }
  return false;
}
static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, unsigned bpp) {
  unsigned a = (address + y) & 0x7fff, shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) { bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8); }
  return pixel;
}
static void prepare_stage_planes(void) {
  airport_sky_width = 0; submarine_count = 0;
  if (frame.ram[0x1f7a] == 5 && frame.ram[0x1e89] == 0x0e &&
      word(frame.ram, 0x1e90) == 0 && word(frame.ram, 0x1e50) >= 0x300) {
    /* The airport panorama ends partway through screen 2 (640 pixels in
     * the retail map). Later cells belong to other mechanisms and contain
     * intentional holes. Discover the continuous sky band from its top row
     * and reflect that edge, instead of exposing those unpainted cells. */
    unsigned x; uint16_t tile;
    for (x = 0; x < 1024; x += 8)
      if (!MmxRendererStageTile(frame.ram, 1, x, 0, &tile) || !(tile & 1023)) break;
    if (x >= 256 && x < 1024) airport_sky_width = (int)x;
  }
  if (frame.ram[0x1f7a] != 1) return;
  for (unsigned d = 0xe68; d <= 0x1228; d += 64) {
    const uint8_t *r = frame.ram;
    if (!r[d] || r[d + 10] != 0x21 || !(r[d + 11] & 0x80) || r[d + 1] != 0) continue;
    unsigned variant = r[d + 11] & 0x7f;
    if (variant >= 3) continue;
    /* $82:AE81 / $86:CBEC describe the buried submarine's BG1 body.
     * State 4 starts its real rise. Before that, the source-art rectangle
     * must stay empty even when an adaptive margin can already see it. */
    const uint8_t *top = rom_at(0x86cbec + variant * 2, 2);
    const uint8_t *bottom = rom_at(0x86cbf2 + variant * 2, 2);
    if (!top || !bottom || word(bottom, 0) < word(top, 0)) continue;
    unsigned shift = word(r, d + 0x34);
    bool scrolling = (r[d + 2] == 2 || r[d + 2] == 4) && r[0xba1] == 2 &&
        word(r, 0x1f28) == word(top, 0) && word(r, 0x1f2a) == word(bottom, 0) &&
        shift > 0 && shift <= 0x50;
    submarine_bodies[submarine_count++] = (SubmarineBody){
        ((int)word(r, d + 5)) & ~31, (int)word(top, 0), (int)word(bottom, 0) + 1,
        shift, r[d + 2] != 4, scrolling};
  }
}
static unsigned menu_glyph(unsigned c) {
  if (c >= 'A' && c <= 'L') return 0xc4 + c - 'A';
  if (c >= 'M' && c <= 'P') return 0xd4 + c - 'M';
  if (c >= 'R' && c <= 'W') return 0xd8 + c - 'R';
  return c == '.' ? 0xdf : c == 'X' ? 0x9d : 0;
}
static uint16_t weapon_menu_tile(unsigned tx, unsigned ty, uint16_t original) {
  if (!frame_weapons.menu_page || !MmxWeaponsMenuVisible(frame.ram) || ty < 5 || ty > 14) return original;
  unsigned column = tx >= 17, local = tx - (column ? 13 : 0), item = (ty - 5) / 2 + column * 5;
  if (!item || item > 8) return original;
  if (local == 4 || local == 5) return 0; /* Icons use their original source-game palette below. */
  if (!(ty & 1) && local >= 6 && local <= 14) {
    unsigned flags = frame.ram[0x1ed2] == item ? 0x0800 : 0x2800;
    if (local == 6) return (uint16_t)(flags | 0x82);
    if (local == 14) return (uint16_t)(flags | 0x4082);
    int filled = frame_weapons.energy[(frame_weapons.menu_page - 1) * 8 + item - 1] - (int)(local - 7) * 4;
    if (filled < 0) filled = 0;
    if (filled > 4) filled = 4;
    return (uint16_t)(flags | (0x83 + filled));
  }
  if ((ty & 1) && local >= 7 && local <= 14) {
    const char *label = MmxWeaponsLabel(frame_weapons.menu_page, item);
    unsigned i = local - 7;
    return (uint16_t)(i < strlen(label) ? 0x1c00 | menu_glyph((unsigned char)label[i]) : 0);
  }
  return original;
}
static int weapon_menu_icon(int x, int y) {
  if (!frame_weapons.menu_page || !MmxWeaponsMenuVisible(frame.ram) || y < 40 || y >= 120) return -1;
  int column = x >= 136, left = column ? 136 : 32;
  if (x < left || x >= left + 16) return -1;
  unsigned row = (y - 40) / 16, item = row + column * 5;
  if (!item || item > 8) return -1;
  const MmxWeaponPose *pose = MmxWeaponsIcon(frame_weapons.menu_page, item);
  const uint16_t *colors = MmxWeaponsIconPalette(frame_weapons.menu_page, item);
  if (!pose || !colors) return -1;
  /* Icons occupy the original 16x16 cells, centered without resampling. */
  int px = x - left - (16 - (int)pose->width) / 2;
  int py = (y - 40) % 16 - (16 - (int)pose->height) / 2;
  if (px < 0 || py < 0 || px >= pose->width || py >= pose->height) return -1;
  unsigned pixel = pose->pixels[py * pose->width + px];
  return pixel ? colors[pixel] : -1;
}
static uint16_t background(const Ppu *p, const Raster *r, unsigned layer, int x, int y, bool stage, int *private_color) {
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  bool margin = x < 0 || x >= 256;
  /* Launch's BG3 water plane is blended over the world on the subscreen.
   * Repeat that plane while retaining the vertical waterline/scroll. Other
   * BG3 uses, including dialogue, stay within their native screen bounds. */
  bool water = frame.ram[0x1f7a] == 1 && (p->screenEnabled[0] & 4) &&
      !(p->screenEnabled[1] & 4) && (p->cgwsel & 2) && (p->cgadsub & 0x44) == 0x44;
  if (stage && layer == 2 && margin && !water) return 0;
  /* Thunder Slimer's moving BG2 surface is already resident in VRAM.
   * Draw it through the margins using its live scroll, rather than hiding
   * it at 4:3 or reconstructing dormant bubbles from the stage map. */
  bool slime_surface=stage && layer==1 && frame.ram[0x1f7a]==6 && frame.ram[0x1e89]==0x0c;
  int asset_x = -1;
  bool armadillo_lower_shaft = false;
  unsigned bpp = layer == 2 ? 2 : 4, size = PPU_bigTiles(p, layer) ? 16 : 8;
  int px = (x + p->hScroll[layer]) & 1023, py = (y + p->vScroll[layer]) & 1023;
  if(stage && layer==2 && water) py=(py+view_dy)&1023;
  unsigned sc = p->bgXsc[layer], tx = px / size, ty = py / size;
  unsigned a = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
  if ((sc & 1) && (tx & 32)) a += 1024;
  if ((sc & 2) && (ty & 32)) a += (sc & 1) ? 2048 : 1024;
  uint16_t tile = r->vram[a & 0x7fff];
  if (layer == 0 && !stage) {
    int icon = weapon_menu_icon(x, y);
    if (icon >= 0) { *private_color = icon; return 0x8001; }
    tile = weapon_menu_tile(tx, ty, tile);
  }
  if (stage && size == 8 && layer < 2 && !slime_surface && (x < 0 || x >= 256 || view_dx || view_dy)) {
    int wx, wy;
    if (layer == 0) {
      wx = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d), p->hScroll[0]) + view_dx + x;
      wy = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50), p->vScroll[0]) + view_dy + y;
      /* $81:F9B7 joins the shaft at camera $1F00,$0600 to $0100,$0800.
       * Project its continuation before that native-coordinate relocation,
       * so the wide sides already show the lower room while X falls. */
      if (frame.ram[0x1f7a] == 3 && word(frame.ram, 0x1e4d) == 0x1f00 &&
          word(frame.ram, 0x1e50) >= 0x500 && word(frame.ram, 0x1e50) <= 0x600 && wy >= 0x600) {
        wx -= 0x1e00; wy += 0x200; armadillo_lower_shaft = true;
      }
      for (unsigned i = 0; i < submarine_count; ++i) {
        const SubmarineBody *body = &submarine_bodies[i];
        /* $82:B414 subtracts the burial offset into $C4; the $80:84CB
         * raster IRQ applies it to the whole BG1 scanline. Only the body
         * occupies that band in 4:3. Restore the terrain's original scroll
         * outside its columns in the wider view, including during the rise.
         * Match the captured IRQ value so other raster bands stay intact. */
        if (body->scrolling && (p->vScroll[0] & 1023) == (word(frame.ram, 0xc4) & 1023)) {
          if (wx < body->left || wx >= body->left + 160) wy += body->shift;
          break;
        }
      }
    } else {
      int stream_x = (int16_t)word(frame.ram, 0x1e8d), stream_y = (int16_t)word(frame.ram, 0x1e90);
      wx = stream_x + (((p->hScroll[1] - stream_x + 512) & 1023) - 512) + x;
      wy = stream_y + (((p->vScroll[1] - stream_y + 512) & 1023) - 512) + y;
      /* $00:DF08 dispatches BG2 follow modes. Retain the raster offset and
       * apply the exact half/full camera relation; actor/script modes stay
       * canonical while their shared scene owns both views. */
      unsigned mode=frame.ram[0x1e89];
      int nx=word(frame.ram,0x1e4d),ny=word(frame.ram,0x1e50);
      if(mode==2 || mode==14) wx+=((nx+view_dx)>>1)-(nx>>1);
      else if(mode==4 || mode==16) wx+=view_dx;
      if(mode==2 || mode==16) wy+=((ny+view_dy)>>1)-(ny>>1);
      else if(mode==4 || mode==14) wy+=view_dy;
      /* The boat's BG2 body occupies one source screen; surrounding map
       * cells are staging art. The controller scrolls that screen into view. */
      if (frame.ram[0x1f7a] == 1 && frame.ram[0x1e89] == 0x0c &&
          (wx < 0xb00 || wx >= 0xc00 || wy < 0 || wy >= 256)) return 0;
      /* The sea floor behind the raised bank was never exposed by the
       * native camera: its $0370..038F band includes foreground scraps.
       * Continue the adjacent authored seabed across that occluded band. */
      if (frame.ram[0x1f7a] == 1 && frame.ram[0x1e89] == 0x0e &&
          wx >= 0x500 && wx < 0x700 && wy >= 0x370 && wy < 0x390)
        wx = 0x700 | (wx & 255);
      /* The serpent's sea backdrop begins at source $C00. Its preceding
       * actor staging cells are empty; extend the sea edge into the margin. */
      if (frame.ram[0x1f7a] == 1 && frame.ram[0x1e89] == 0x0e &&
          stream_x >= 0xc00 && stream_x < 0xe00 && wx < 0xc00) wx = 0xc00;
      /* Highway's final arena switches to the sky plane at BG2 x=$A00.
       * Earlier columns are intentionally empty at this vertical scroll;
       * extend the arena's sky edge when a wide view reaches behind it. */
      if (frame.ram[0x1f7a] == 0 && stream_x >= 0xa00 && wx < 0xa00) wx = 0xa00;
    }
    /* Reconstruct prepared map data independently of circular VRAM history.
     * Outside authored terrain, reflect only the background edge. */
    if (wx < 0) wx = -wx - 1;
    if (layer == 1 && airport_sky_width && wx >= airport_sky_width) {
      wx %= 2 * airport_sky_width;
      if (wx >= airport_sky_width) wx = 2 * airport_sky_width - wx - 1;
    }
    if (layer == 0) {
      unsigned stage_id = frame.ram[0x1f7a];
      if (stage_id < 13 && rom_size > 0x30d24 + stage_id * 3 + 2) {
        const uint8_t *t = rom + 0x30d24 + stage_id * 3;
        const uint8_t *bounds = rom_at(word(t, 0) | (t[2] << 16), 2);
        if (bounds && bounds[0] && bounds[0] <= 32) {
          int width = bounds[0] * 256;
          if (wx >= width) wx = width * 2 - wx - 1;
          if (wx < 0) wx = 0;
        }
      }
      for (unsigned i = 0; i < submarine_count; ++i) {
        const SubmarineBody *body = &submarine_bodies[i];
        if (body->hidden && wx >= body->left && wx < body->left + 160 && wy >= body->top && wy < body->bottom) {
          wx -= 256; break; /* The preceding water screen has no source body. */
        }
      }
      if (door_body(wx, wy)) {
        bool left = door_body(wx - 16, wy), right = door_body(wx + 16, wy);
        if (left || right) {
          int boundary = (wx & ~15) + (right ? 16 : 0);
          bool view_left = view_camera(frame.ram, 0x1e4d) + 128 < boundary;
          /* Retain the column facing the current room. Only its duplicate
           * samples the neighboring wall; a distant closed door stays visible. */
          if (view_left && left) wx += 16;
          if (!view_left && right) wx -= 16;
        }
      }
    }
    uint16_t mapped;
    if (MmxRendererStageTile(frame.ram, layer, wx, wy, &mapped)) {
      tile = mapped; px = wx; py = wy;
      /* These backdrops move at half speed. Express the map column as the player
       * X at which it crosses the native view's center (camera+128). */
      asset_x = layer == 1 && (frame.ram[0x1f7a] <= 2 || frame.ram[0x1f7a] == 4) ? wx * 2 - 128 : wx;
      /* Late Launch BG2 remains ocean/ruins behind the hallway. The later
       * foreground palette events belong to the cliff and boss room. */
      if (layer == 1 && frame.ram[0x1f7a] == 1 && frame.ram[0x1e89] == 0x0e &&
          word(frame.ram, 0x1e8d) >= 0xc00 && asset_x >= 0x1a20) asset_x = 0x1a1f;
      /* Chill's BG2 sky palette also changes with elevation. Its cave-exit
       * X transition owns foreground art only; keep the live sky colors. */
      if (layer == 1 && frame.ram[0x1f7a] == 8) asset_x = -1;
      /* Highway's incoming airship uses BG2 as a moving actor surface.
       * Its source columns are not terrain coordinates: projecting them
       * selects earlier road palettes/CHR across the native view seams.
       * The complete ship resources are resident; retain its live binding. */
      if (layer == 1 && frame.ram[0x1f7a] == 0 && frame.ram[0x1e89] == 0x0c)
        asset_x = -1;
    }
  }
  int cx = px & (size - 1), cy = py & (size - 1);
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + cy / 8 * 16) & 1023;
  unsigned address = (PPU_bgTileAdr(p, layer) + number * bpp * 4) & 0x7fff;
  const uint8_t *bits = stage && g_mmx_render_asset_repairs && bpp == 4 && !(frame.ram[0x1f7a] == 1 && layer == 1) ?
      MmxRenderAssetsBackgroundTile(frame.ram, asset_x, address) : NULL;
  unsigned pixel;
  if (bits) {
    bits += (cy & 7) * 2;
    unsigned shift = 7 - (cx & 7);
    pixel = ((bits[0] >> shift) & 1) | (((bits[1] >> shift) & 1) << 1) |
        (((bits[16] >> shift) & 1) << 2) | (((bits[17] >> shift) & 1) << 3);
  } else pixel = tile_pixel(r->vram, address, cx & 7, cy & 7, bpp);
  if (!pixel) return 0;
  unsigned index = (((tile >> 10) & 7) << bpp) | pixel;
  const MmxBackgroundPalette *palette = stage && g_mmx_render_asset_repairs ?
      (armadillo_lower_shaft ? MmxRenderAssetsBackgroundPalettePhase(frame.ram, 1) :
       MmxRenderAssetsBackgroundPalette(frame.ram, asset_x)) : NULL;
  if (palette && palette->valid[index]) *private_color = palette->colors[index];
  unsigned priority = tile & 0x2000 ? (layer == 2 && (p->bgmode & 8) ? 15 : high[layer]) : low[layer];
  return (uint16_t)((priority << 12) | (layer << 8) | index);
}
static void sprite(const Ppu *p, const Raster *r, int x, int sy, unsigned attr, int size,
                    int y, MmxRenderView view, uint16_t *out, bool margins_only,
                    const MmxSpriteAsset *asset, unsigned raw_tile, int *object_color,
                    bool full_coordinates, unsigned zero_icon, bool red_tint) {
  int row = full_coordinates ? y - sy : (y - sy) & 255;
  if (row < 0 || row >= size) return;
  if (attr & 0x8000) row = size - 1 - row;
  unsigned base = (p->obsel & 7) * 8192;
  if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
  unsigned z = ((((attr >> 12) & 3) * 4 + 2) << 12) |
               ((attr & 0x800 ? 4 : 6) << 8) | (128 + ((attr >> 9) & 7) * 16);
  for (int c = 0; c < size; ++c) {
    int dx = x + c, dest = dx + view.extra;
    if (dest < 0 || dest >= view.width || (margins_only && dx >= 0 && dx < 256)) continue;
    int cx = attr & 0x4000 ? size - 1 - c : c;
    unsigned number = asset && !asset->live_tiles ? raw_tile : attr & 255;
    unsigned tile = ((((number >> 4) + row / 8) & 15) << 4) | (((number & 15) + cx / 8) & 15);
    unsigned pixel;
    if (asset && !asset->live_tiles) {
      const uint8_t *bits = asset->tiles + tile * 32 + (row & 7) * 2;
      unsigned shift = 7 - (cx & 7);
      pixel = ((bits[0] >> shift) & 1) | (((bits[1] >> shift) & 1) << 1) |
          (((bits[16] >> shift) & 1) << 2) | (((bits[17] >> shift) & 1) << 3);
    } else pixel = tile_pixel(r->vram, base + tile * 16, cx & 7, row & 7, 4);
    int hud_color = zero_icon ? MmxZeroHudColor(cx, row) : -1;
    if (hud_color >= 0) pixel = 1;
    else if (hud_color == -2) pixel = 0;
    if (pixel) {
      out[dest] = (uint16_t)(z | pixel);
      object_color[dest] = hud_color >= 0 ? hud_color :
          asset && !asset->live_colors ? asset->colors[pixel] : -1;
      if (red_tint) {
        unsigned color=object_color[dest]>=0 ? (unsigned)object_color[dest] : r->palette[(z|pixel)&255];
        unsigned red=color&31,green=(color>>5)&31,blue=(color>>10)&31;
        /* Keep the original highlights/neutral outline. Convert only the
         * blue ramp to red, using its existing dark-to-light shading. */
        if (blue>red && blue>green)
          object_color[dest]=(int)(blue | ((red<green ? red : green)<<5) | (red<<10));
      }
      if (x + c < 0 || x + c >= 256) ++stats.margin_sprite_pixels;
    }
  }
}
static bool window(const Ppu *p, int layer, int x, int extra) {
  unsigned flags = (p->windowsel >> (layer * 4)) & 15;
  int l1 = p->window1left == 0 ? -extra : p->window1left;
  int r1 = p->window1right == 255 ? 255 + extra : p->window1right;
  int l2 = p->window2left == 0 ? -extra : p->window2left;
  int r2 = p->window2right == 255 ? 255 + extra : p->window2right;
  bool a = (x >= l1 && x <= r1) != ((flags & 1) != 0);
  bool b = (x >= l2 && x <= r2) != ((flags & 4) != 0);
  if (!(flags & 2)) return (flags & 8) && b;
  if (!(flags & 8)) return a;
  switch ((p->wbgobjlog >> (layer * 2)) & 3) {
    case 0: return a || b; case 1: return a && b; case 2: return a != b; default: return a == b;
  }
}
typedef struct LightBeam { int left[224], right[224]; } LightBeam;
static unsigned spark_lights(LightBeam beams[2], int extra) {
  /* $87:A7F6 builds 8-bit HDMA windows from the ROM's rounded beam profile.
   * Rebuild only that color window in signed host coordinates. The native
   * generator clamps a left-moving light at zero and waits for a right-side
   * arrival to enter 256 pixels; neither limitation describes a wide view. */
  if (!g_mmx_render_asset_repairs || frame.ram[0x1f7a] != 6 ||
      frame.ram[0x1f0a] != 4 || frame.ram[0x1e89] != 2) return 0;
  const uint8_t *curve = rom_at(0x86d136, 25);
  if (!curve) return 0;
  /* Sprite/HDMA submission precedes the next camera update in frame.ram.
   * Use the captured raster scroll, as terrain reconstruction does. */
  int camera_x = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d),
      (uint16_t)word(frame.lines[0].registers, 14));
  int camera_y = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50),
      (uint16_t)word(frame.lines[0].registers, 22));
  camera_x+=view_dx;camera_y+=view_dy;
  unsigned count = 0;
  for (unsigned d = 0xe68; d <= 0x1228 && count < 2; d += 64) {
    const uint8_t *r = frame.ram;
    if (!r[d] || r[d + 10] != 0x37 || r[d + 1] != 2 || r[d + 0x1c] ||
        (r[d + 0x2d] != 0x40 && r[d + 0x2d] != 0x80)) continue;
    bool right = r[d + 0x0b] == 1, fading = r[d + 3] != 0;
    int tip = (int16_t)(word(r, d + 0x22) - camera_x) + (right ? -24 : 24);
    int top = fading ? (int16_t)word(r, d + 0x36) :
        (int16_t)(word(r, d + 0x24) - camera_y) - 24;
    unsigned height = fading ? r[d + 0x3b] : 49;
    int inset = fading ? r[d + 0x1f] : 0;
    LightBeam *beam = &beams[count++];
    for (int y = 0; y < 224; ++y) { beam->left[y] = 1; beam->right[y] = 0; }
    unsigned index = 0, remaining = curve[0]; int edge = fading ? 0 : 12;
    for (unsigned row = 0; row < height && row < 49; ++row) {
      int y = top + (int)row;
      if (y >= 0 && y < 224) {
        beam->left[y] = right ? -extra + inset : tip + edge;
        beam->right[y] = right ? tip - edge : 255 + extra - inset;
      }
      if (!fading) {
        if (remaining) --remaining;
        else if (index < 25) {
          remaining = curve[index++];
          if (remaining >= 5) { remaining -= 5; ++edge; } else --edge;
        }
      }
    }
  }
  return count;
}
static bool condition(unsigned mode, bool inside) { return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside); }
static uint32_t colour(const Ppu *p, const uint16_t *palette, const uint8_t brightness[32], uint16_t main, uint16_t sub, bool inside, int object_color, const int bg_colors[3], bool dialogue_margin) {
  unsigned rgb = palette[main & 255], layer = (main >> 8) & 15;
  if (object_color >= 0 && (layer == 4 || layer == 6)) rgb = (unsigned)object_color;
  if (layer < 3 && bg_colors[layer] >= 0) rgb = (unsigned)bg_colors[layer];
  bool clipped = condition(p->cgwsel >> 6, inside);
  bool math = !condition((p->cgwsel >> 4) & 3, inside) && ((p->cgadsub & 63) & (1u << layer));
  if (dialogue_margin && layer == 5) math = false;
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if (sub & 255) {
      unsigned sub_layer = (sub >> 8) & 15;
      other = object_color >= 0 && (sub_layer == 4 || sub_layer == 6) ? (unsigned)object_color : palette[sub & 255];
      if (sub_layer < 3 && bg_colors[sub_layer] >= 0) other = (unsigned)bg_colors[sub_layer];
    } else half = false;
  }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) { int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    result |= (uint32_t)brightness[c] << (16 - component * 8);
  }
  return result;
}
static void weapon_sprite_row(const MmxWeaponShot *s, unsigned pose, int x, int sy,
                              int y, MmxRenderView view, uint16_t *objects, int *colors) {
  const MmxWeaponPose *p = MmxWeaponsPose(s->page,s->weapon,s->group,pose);
  const uint16_t *palette = MmxWeaponsGroupPalette(s->page,s->weapon,s->group);
  if (!p || !palette) return;
  int row = (s->facing&128 ? -(y-sy)-1 : y-sy) - p->top;
  if (row < 0 || row >= p->height) return;
  for (int col=0;col<p->width;++col) {
    unsigned pixel=p->pixels[row*p->width+col];
    int dx=x+((s->facing&64) ? -1-p->left-col : p->left+col)+view.extra;
    if (pixel && dx>=0 && dx<view.width) {
      objects[dx]=(uint16_t)(0xa680|pixel); colors[dx]=palette[pixel];
    }
  }
}
static void moved_enemy_row(const MmxWeaponShot *s,const Ppu *p,const Raster *r,int y,
                            MmxRenderView view,uint16_t *objects,int *colors) {
  unsigned d=(unsigned)s->origin_x;
  if(d<0xe68 || d>=0x1228 || !frame.ram[d] || frame.ram[d+10]!=s->origin_y) return;
  unsigned group=frame.ram[d+22];
  const uint8_t *a=sprite_arrangement(group,frame.ram[d+23]&127);if(!a) return;
  const MmxSpriteAsset *asset=MmxRenderAssetsObjectSprite(frame.ram,d,group);
  int x=(s->x>>8)-(int)view_camera(frame.ram,0x1e4d);
  int sy=(s->y>>8)+(int8_t)frame.ram[d+25]-(int)view_camera(frame.ram,0x1e50);
  for(int i=(int)a[0]-1;i>=0;--i) {
    Piece piece=make_piece(a+i*4,x,sy,frame.ram[d+17]&64,
        frame.ram[d+17]&63,frame.ram[d+24],group,d,frame.ram[d+23]&127);
    unsigned attr=asset ? (piece.attr&0xf000)|((asset->attributes&15)<<8)|
        (asset->live_tiles?piece.attr&255:0) : piece.attr;
    if(y<piece.y || y>=piece.y+piece.size) continue;
    if(s->weapon==6 && !s->charged && s->muzzle_pose!=3 && s->age>30 && (s->age&1)) {
      uint16_t flash[MMX_RENDER_MAX_WIDTH]={0};int flash_colors[MMX_RENDER_MAX_WIDTH];
      sprite(p,r,piece.x,piece.y,attr,piece.size,y,view,flash,false,asset,piece.tile,flash_colors,true,false,false);
      for(int x=0;x<view.width;++x) if(flash[x]) {
        objects[x]=flash[x];colors[x]=r->palette[128+(flash[x]&15)];
      }
    } else sprite(p,r,piece.x,piece.y,attr,piece.size,y,view,objects,false,asset,piece.tile,colors,true,false,false);
  }
}
static void weapon_effects_row(const MmxWeaponCombatState *combat,const Ppu *p,const Raster *r,
                               int y,MmxRenderView view,uint16_t *objects,int *object_colors) {
  for (unsigned i=0;i<24;++i) {
    const MmxWeaponShot *s=i<8 ? combat->shots+i : combat->effects+i-8;
    if (!s->active || !s->age) continue;
    if(i<8 && s->page==1 && s->weapon==1 && !s->charged &&
        s->muzzle_pose>=1 && s->muzzle_pose<=4)
      moved_enemy_row(s,p,r,y,view,objects,object_colors);
    if(s->page==2 && s->weapon==2) {
      if(s->variant==1) moved_enemy_row(s,p,r,y,view,objects,object_colors);
      if(s->variant==2 && s->muzzle_pose==4) continue;
    }
    if(s->page==2 && s->weapon==6) {
      if(i>=8 && s->variant==2) {moved_enemy_row(s,p,r,y,view,objects,object_colors);continue;}
      if(i<8 && (s->variant==1 || (s->charged && s->muzzle_pose!=1))) continue;
    }
    if (s->page==2 && s->weapon==3 && s->charged && (!s->variant || !s->muzzle_pose)) continue;
    if (s->page==1 && s->weapon==1 && s->charged) continue;
    if (i>=8 && s->page==1 && !(s->weapon==8 && s->variant==5) && (s->weapon==1 ? s->age==1 || !((s->age-1+s->tether_pose)&1) :
        !(combat->tick&1))) continue; /* Original debris/sparkle flicker. */
    if (i<8 && s->page==1 && s->weapon==1 && s->muzzle_pose==5) continue;
    if (s->page==1 && s->weapon==8 && s->charged && s->tether_pose) continue;
    if (s->page==1 && s->weapon==2 && s->charged && (!s->variant || !s->muzzle_pose)) continue;
    if (s->page==2 && s->weapon==7 && s->variant!=2 && s->muzzle_pose==(s->charged ? 4 : 7)) continue;
    if (s->page==1 && s->weapon==4) {
      if (s->charged && s->muzzle_pose && s->radius) {
        static const uint8_t flash[6]={15,16,16,17,17,18};
        MmxWeaponShot center=*s;center.facing=s->tether_pose ? 64 : 0;
        weapon_sprite_row(&center,flash[6-s->radius],s->origin_x-view_camera(frame.ram,0x1e4d),
            s->origin_y-view_camera(frame.ram,0x1e50),y,view,objects,object_colors);
      } else if (!s->charged && (s->muzzle_pose==2 || s->muzzle_pose==3 || s->muzzle_pose==5)) {
        unsigned duration=s->origin_x>=1024 ? 1 : s->origin_x>=512 ? 2 : 3;
        weapon_sprite_row(s,9+(unsigned)s->origin_y/duration%5,(s->x>>8)-view_camera(frame.ram,0x1e4d),
            (s->y>>8)+8-view_camera(frame.ram,0x1e50),y,view,objects,object_colors);
      }
    }
    if (s->page==1 && s->weapon==6 && s->variant) {
      /* X2 $81:849D and $9ABF: nine links, every eighth of the span. */
      unsigned pose=(s->charged ? 20 : 5)+s->tether_pose/3;
      for (int link=8;link>=0;--link) {
        int x=(s->origin_x*(8-link)+(s->x>>8)*link)/8;
        weapon_sprite_row(s,pose,x-view_camera(frame.ram,0x1e4d),
            (s->y>>8)-view_camera(frame.ram,0x1e50),y,view,objects,object_colors);
      }
    }
    if (s->charged && s->page == 2 && s->weapon == 4 && s->variant!=4) {
      int ox=s->origin_x-view_camera(frame.ram,0x1e4d), oy=s->origin_y-view_camera(frame.ram,0x1e50);
      weapon_sprite_row(s,s->tether_pose,ox,oy,y,view,objects,object_colors);
      weapon_sprite_row(s,s->muzzle_pose,ox,oy,y,view,objects,object_colors);
    }
    if (s->page == 2 && s->weapon == 5 && s->variant >= 2) {
      /* Original X3 trail actors use poses 14/15 and delayed positions.
       * Rays have constant velocity, so their history is reconstructed
       * exactly without extra transient slots or unsaved renderer state. */
      for (int delay=2;delay>=1;--delay) {
        int remaining = delay - (s->variant == 3 ? s->radius : 0);
        if (remaining > 0 && s->age > (unsigned)delay+1)
          weapon_sprite_row(s,13+delay,((s->x-s->vx*remaining)>>8)-view_camera(frame.ram,0x1e4d),
              ((s->y-s->vy*remaining)>>8)-view_camera(frame.ram,0x1e50),y,view,objects,object_colors);
      }
      if (s->variant == 3) continue;
    }
    weapon_sprite_row(s,s->pose,(s->x>>8)-view_camera(frame.ram,0x1e4d),
        (s->y>>8)-view_camera(frame.ram,0x1e50),y,view,objects,object_colors);
  }
}
static void teleport_actor_row(const uint8_t *ram,const MmxZeroState *zero,const Ppu *p,
                               const Raster *r,int y,MmxRenderView view,uint16_t *objects,int *object_colors) {
  unsigned pose = MmxZeroSwapPose(zero);
  int x = (int16_t)(word(ram,0xbad) - view_camera(ram,0x1e4d));
  int sy = (int16_t)(word(ram,0xbb0) - view_camera(ram,0x1e50)) + zero->swap_y;
  unsigned flip = ram[0xbb9] & 64;
  if (!zero->active_x) {
    const uint8_t *pixels = MmxZeroTeleportPose(pose);
    int row = y - (sy - 8) + 64;
    if (pixels && row >= 0 && row < 128) for (int col = 0; col < 128; ++col) {
      unsigned pixel = pixels[row * 128 + col];
      int dx = x + (flip ? 63 - col : col - 64) + view.extra;
      if (pixel && dx >= 0 && dx < view.width) {
        objects[dx] = (uint16_t)(0xa680 | (pixel & 15));
        object_colors[dx] = MmxZeroColors()[pixel];
      }
    }
  } else {
    const MmxSpriteAsset *a = MmxRenderAssetsTeleportX(pose);
    const uint8_t *layout = a ? sprite_arrangement(0,pose) : NULL;
    if (layout) for (int i = layout[0] - 1; i >= 0; --i) {
      Piece s = make_piece(layout + i * 4,x,sy,flip,0x22,0,0,0xba8,pose);
      sprite(p,r,s.x,s.y,s.attr,s.size,y,view,objects,false,a,s.tile,object_colors,true,false,false);
    }
  }
}
static void x1_weapon_palette(MmxSpriteAsset *asset,const uint16_t *colors,unsigned page) {
  if(!colors) return;
  memcpy(asset->colors,colors,32);asset->live_tiles=true;
  /* X1's face uses index 1 for eye whites. X2 puts an unused dark red
   * there ($048E); its corresponding white is index 13. Adapt only the
   * palette overlay on X1 tiles, not the original source special poses. */
  if(page==1) asset->colors[1]=colors[13];
}
static void coop_sting_palette(const uint8_t *ram,MmxSpriteAsset *asset) {
  if(!ram[0xc31]) return;
  /* The active projectile owns the phase ($83:9C22), independently for each
   * seat. The ordinary weapon palette must not erase this native effect. */
  for(unsigned d=0x1228;d<0x1428;d+=64) if(ram[d] && ram[d+10]==0x11) {
    MmxRenderAssetsStingPalette(ram[d+0x39],asset->colors);return;
  }
}
static void prepare_partner_graphics(void) {
  /* The partner is drawn from this tick's body, unlike the anchor's latched
   * OAM. Native $84:8FCA queues pose CHR at $0500 for the NEXT NMI ($80:8332).
   * Preview those pending OBJ transfers only in the partner's private raster
   * so a new X arrangement never reads the preceding pose's tiles. This uses
   * captured state, including WRAM-backed armor/effects, and changes no guest
   * memory, transfer timing, or save/rollback layout. */
  partner_raster = frame.lines[0];
  unsigned end = frame.ram[0xa3] & ~7u;
  for (unsigned offset=0;offset<end;offset+=8) {
    const uint8_t *dma=frame.ram+0x500+offset;
    unsigned dest=word(dma,1),size=word(dma,3),source=word(dma,5),bank=dma[7];
    /* Sprite records use contiguous word writes. Leave tilemap/remapped
     * transfers to the native raster rather than predicting their effects. */
    if(dma[0]!=0x80 || dest<0x6000 || dest>=0x8000 || !size || (size&1) ||
        size>(0x8000-dest)*2 || size>0x10000-source) continue;
    const uint8_t *bytes=bank==0x7e || bank==0x7f ?
        frame.ram+(bank-0x7e)*0x10000+source : rom_at(bank<<16|source,size);
    if(!bytes) continue;
    for(unsigned i=0;i<size;i+=2) partner_raster.vram[dest+i/2]=(uint16_t)word(bytes,i);
  }
}
static void player_overlay_plane_row(
    const MmxRenderPlayerOverlayPlane *plane, const uint16_t *palette,
    unsigned palette_count, int y, int zx, int zy, bool mirror, unsigned z,
    MmxRenderView view, uint16_t *objects, int *object_colors);
static void player_overlay_row(const MmxRenderPlayerOverlay *overlay, int y,
                               int zx, int zy, unsigned z, MmxRenderView view,
                               uint16_t *objects, int *object_colors) {
  if (overlay->blade_layer == 1)
    player_overlay_plane_row(&overlay->blade, overlay->palette, overlay->palette_count,
        y, zx, zy, overlay->facing_left, z, view, objects, object_colors);
  player_overlay_plane_row(&overlay->body, overlay->palette, overlay->palette_count,
      y, zx, zy, overlay->facing_left, z, view, objects, object_colors);
  if (overlay->blade_layer == 2)
    player_overlay_plane_row(&overlay->blade, overlay->palette, overlay->palette_count,
        y, zx, zy, overlay->facing_left, z, view, objects, object_colors);
}
static void coop_partner_row(const Ppu *ppu,const Raster *r,int y,MmxRenderView view,
                             uint16_t *objects,int *colors) {
  const MmxCoopPlayer *partner = &frame_coop.players[frame_coop.current^1];
  if (!frame_coop.initialized || partner->status != MMX_COOP_ALIVE || frame_coop.scene_phase==2) return;
  const uint8_t *ram = partner_ram;
  if (partner->zero.swap_phase) {
    teleport_actor_row(ram,&partner->zero,ppu,r,y,view,objects,colors);return;
  }
  bool zero = partner->character == MMX_COOP_ZERO;
  const MmxWeaponShot *cast=NULL;
  for (unsigned i=0;i<8;++i) {
    const MmxWeaponShot *s=partner->combat.shots+i;
    if (s->active && s->charged && s->page==2 &&
        ((s->weapon==3 && !s->variant && s->muzzle_pose) || (s->weapon==6 && s->variant==1))) cast=s;
  }
  MmxSpriteAsset weapon_palette={0};
  const uint16_t *weapon_colors=MmxWeaponsPalette(partner->weapons.page,partner->weapons.weapon,true);
  if(!partner->weapons.page) {
    const MmxSpriteAsset *native=MmxRenderAssetsWeaponX(ram[0xbdb]/2,true);
    if(native) weapon_colors=native->colors;
  }
  x1_weapon_palette(&weapon_palette,weapon_colors,partner->weapons.page);
  coop_sting_palette(ram,&weapon_palette);
  if(!zero && MmxKncBugfixActive(frame_coop.current^1))
    MmxRenderAssetsStingPalette(MmxKncBugfixPhase(),weapon_palette.colors);
  if (zero && ram[0xbb6]) {
    const uint8_t *body = MmxZeroPose(ram,&partner->zero);
    const uint8_t *blade = MmxZeroBlade(&partner->zero);
    const uint8_t *charge = !ram[0xbdb] && !partner->weapons.weapon ? MmxZeroChargePose(&partner->zero) : NULL;
    int x = (int16_t)(word(ram,0xbad)-view_camera(ram,0x1e4d));
    int sy = (int16_t)(word(ram,0xbb0)-view_camera(ram,0x1e50))+MmxZeroPoseOffsetY(ram);
    if (cast) {
      MmxZeroState z=partner->zero;z.burst=z.air=0;blade=NULL;
      if (cast->weapon==3) z.slash=cast->pose==39?1:cast->pose==41?3:cast->pose==29?6:
          cast->pose==30?9:cast->pose==31?12:cast->pose==32?15:21;
      else {
        z.slash=0;z.anim_valid=1;
        z.anim_pose=cast->pose==39 || cast->pose==40?0:
            cast->pose==41 || cast->pose==44?0x43:cast->pose==29 || cast->pose==45?0x44:
            cast->pose==35?0x45:cast->pose==36?0x46:0x47;
      }
      body=MmxZeroPose(ram,&z);
    }
    bool pilot=ram[0xbbe]==0x6a || ram[0xbbe]==0x6b;
    if (pilot) {
      const uint8_t *a=sprite_arrangement(ram[0xbbe],ram[0xbbf]&127);
      if (a && a[0]) {int dx=(int8_t)a[1]+5;x+=(ram[0xbb9]&64)?-dx:dx;sy+=8+(int8_t)a[2]+20;}
      body=MmxZeroMenuPose();blade=charge=NULL;
    }
    int row = y-sy+64;
    unsigned priority = ((ram[0xbb9]>>4 & 3)*4+2)<<12;
    /* A Saber attack replaces the body; the partner's Ride Armor pilot keeps
     * the native cockpit art, which has no OAM here to align the sheet to. */
    bool overlay = frame_partner_overlay.active && !pilot && !cast;
    if (overlay) player_overlay_row(&frame_partner_overlay,y,x,sy,priority|0x680,view,objects,colors);
    if (!overlay && body && row>=0 && row<128 && (!pilot || (row>=44 && row<64))) for (int col=0;col<128;++col) {
      unsigned pixel = body[row*128+col];
      if (blade && blade[row*128+col]) pixel = blade[row*128+col];
      int dest = x + ((ram[0xbb9]&64) ? 63-col : col-64) + view.extra;
      if (pixel && dest>=0 && dest<view.width) {
        objects[dest] = (uint16_t)(priority|0x680|(pixel&15));
        colors[dest] = pixel>=16 && pixel<32 ? MmxZeroBodyColors(&partner->zero)[pixel-16] : MmxZeroColors()[pixel];
      }
      if (charge && charge[row*128+col] && dest>=0 && dest<view.width) {
        unsigned pixel = charge[row*128+col];
        unsigned palette = partner->zero.charge>=81 && partner->zero.charge<201 ? 32 : 0;
        objects[dest] = (uint16_t)(0xe680|pixel); colors[dest] = MmxZeroColors()[palette+pixel];
      }
    }
  }
  if (!zero && cast && ram[0xbb6]) weapon_sprite_row(cast,cast->pose,
      (int16_t)(word(ram,0xbad)-view_camera(ram,0x1e4d)),
      (int16_t)(word(ram,0xbb0)-view_camera(ram,0x1e50)),y,view,objects,colors);
  for (unsigned i=0;i<24;++i) {
    unsigned d = i==0 ? 0xba8 : i<16 ? 0xc38+(i-1)*32 : 0x1228+(i-16)*64;
    /* Armor parts ($0C38/$0C58/$0C78) are visible when .0E is nonzero, as
     * native D56F submits them (expand_queues); bit 7 alone hid a partner X's
     * helmet, arms and boots whenever Zero drove the world. */
    bool armor = d>=0xc38 && d<=0xc78;
    if (d==0xba8 ? !ram[d+1] || !ram[d+14] :
        !ram[d] || !(armor ? ram[d+14] : ram[d+14]&128)) continue;
    if(d<0xc98 && !ram[0xbb6]) continue; /* Armor follows its owner's Sting blink. */
    if (zero && (d==0xba8 || d<0xc98 || MmxZeroNativeChargeObject(d,ram[d+10]))) continue;
    if (cast && d<0xc98) continue;
    if (d>=0x1228 && partner->combat.shots[(d-0x1228)/64].active) continue;
    unsigned group = ram[d+22];
    const uint8_t *a = sprite_arrangement(group,ram[d+23]&127);
    if (!a) continue;
    int x = (int16_t)(word(ram,d+5)-view_camera(ram,0x1e4d));
    int sy = (int16_t)(word(ram,d+8)+(int8_t)ram[d+25]-view_camera(ram,0x1e50));
    for (int j=(int)a[0]-1;j>=0;--j) {
      Piece s = make_piece(a+j*4,x,sy,ram[d+17]&64,ram[d+17]&63,ram[d+24],group,d,ram[d+23]&127);
      const MmxSpriteAsset *asset=!zero && weapon_colors && zero_actor(d,group) &&
          (s.attr&0x0e00) ? &weapon_palette : NULL;
      /* Normal Spark and its two wall fragments share original group $47.
       * Another seat can replace the shared $6200/$6300 weapon upload. */
      if(d>=0x1228 && ram[d+10]==0x0c && group==0x47 && (s.attr&0x0e00)==0x0600)
        asset=MmxRenderAssetsWeaponX(6,false);
      if(d>=0x1228 && ((ram[d+10]==3 && group==0x9e) ||
          ((ram[d+10]==1 || ram[d+10]==2) && group==0x0e))) {
        const MmxSpriteAsset *beam=MmxRenderAssetsChargedBuster(group,ram[d+23]&127);
        if(beam) asset=beam;
      }
      sprite(ppu,zero?r:&partner_raster,s.x,s.y,s.attr,s.size,y,view,objects,false,asset,s.tile,colors,true,false,false);
    }
  }
}
static void coop_meter_row(const Ppu *ppu,const Raster *r,int y,MmxRenderView view,
                           uint16_t *objects,int *colors,int x,unsigned value,unsigned maximum,
                           unsigned palette,const MmxSpriteAsset *art,int icon_y,bool inverted) {
  /* The native $D82C/$D94A meter uses overlapping 16px strips. Retain its
   * partial-strip placement and OAM order, including the cap above max HP. */
  typedef struct {int y;unsigned tile;} Strip;
  Strip strips[8];unsigned count=0;
  if(maximum>32) maximum=32;
  if(value>maximum) value=maximum;
  /* Mirror Zero's frame, but keep remaining energy at the bottom. */
  if(inverted) value=maximum-value;
  int top=64,remaining=(int)value;
  while(remaining>0) {
    remaining-=8;int sy=top-(remaining<0?remaining*2:0);
    strips[count++]=(Strip){sy,0x80};top=sy-16;
  }
  remaining=(int)maximum-(int)value;
  do {
    remaining-=8;int sy=top-(remaining<0?remaining*2:0);
    strips[count++]=(Strip){sy,0x82};top=sy-16;
  } while(remaining>=0);
  strips[count++]=(Strip){top,0x84};
  for(int i=(int)count-1;i>=0;--i) {
    unsigned tile=strips[i].tile;
    int sy=strips[i].y+icon_y-80;
    unsigned attr=0x3000|(palette<<9);
    if(inverted) {
      sy=icon_y+80-strips[i].y;
      if(tile==0x80) tile=0x82;
      else if(tile==0x82) tile=0x80;
      attr|=0x8000;
    }
    sprite(ppu,r,x,sy,attr|tile,16,y,view,
        objects,false,art,tile,colors,true,false,false);
  }
}
static void coop_inverted_badge_row(const Ppu *ppu,const Raster *r,int y,MmxRenderView view,
                                    uint16_t *objects,int *colors,int x,int icon_y,unsigned palette,
                                    const MmxSpriteAsset *frame_art,const MmxSpriteAsset *icon_art,
                                    const MmxWeaponPose *icon,bool zero) {
  /* Use the meter's own frame/palette for the top cap. Replace only the
   * upright symbol panel, leaving its sides continuous with the meter. */
  sprite(ppu,r,x,icon_y,0xb086|(palette<<9),16,y,view,objects,false,
      frame_art,0x86,colors,true,false,false);
  int row=y-icon_y;
  int panel_bottom=palette==3?14:13;
  if(row<3 || row>panel_bottom) return;
  /* Leave the cap's white highlight intact above the dark weapon panel. */
  int source_row=row-(palette==3);
  for(unsigned col=2;col<14;++col) {
    if(row==panel_bottom && (col==2 || col==13)) continue;
    int dx=x+(int)col+view.extra;
    if(dx<0 || dx>=view.width) continue;
    int color=zero?MmxZeroHudColor(col,(unsigned)source_row):-1;
    if(color<0) {
      unsigned tile=palette==2?0x86:0x20;
      unsigned number=(((tile>>4)+(unsigned)source_row/8)<<4)|((tile&15)+col/8);
      unsigned pixel;
      if(icon) pixel=icon->pixels[source_row*16+col];
      else if(icon_art && !icon_art->live_tiles) {
        const uint8_t *bits=icon_art->tiles+number*32+(source_row&7)*2;
        unsigned shift=7-(col&7);
        pixel=((bits[0]>>shift)&1)|(((bits[1]>>shift)&1)<<1)|
            (((bits[16]>>shift)&1)<<2)|(((bits[17]>>shift)&1)<<3);
      } else pixel=tile_pixel(r->vram,(ppu->obsel&7)*8192+number*16,col&7,source_row&7,4);
      color=frame_art?frame_art->colors[pixel]:r->palette[128+palette*16+pixel];
    }
    objects[dx]=0xe6a1;colors[dx]=color;
  }
}
static void coop_hud_row(const Ppu *ppu,const Raster *r,int y,MmxRenderView view,bool anchored,
                         uint16_t *objects,int *colors) {
  for(unsigned seat=0;seat<2;++seat) {
    const MmxCoopPlayer *player=&frame_coop.players[seat];
    if(seat && player->status==MMX_COOP_ABSENT) continue;
    bool inverted=coop_hud_compact && player->character==MMX_COOP_ZERO;
    int x=8+(coop_hud_compact?0:(int)seat*32)-(anchored?view.extra:0),icon_y=inverted?100:80;
    unsigned hp=player->status==MMX_COOP_FALLEN?0:player->body[0x27]&127;
    coop_meter_row(ppu,r,y,view,objects,colors,x,hp,frame.ram[0x1f9a],2,NULL,icon_y,inverted);
    if(inverted) coop_inverted_badge_row(ppu,r,y,view,objects,colors,x,icon_y,2,NULL,NULL,NULL,true);
    else sprite(ppu,r,x,icon_y,0x3486,16,y,view,objects,false,NULL,0,colors,true,
        player->character==MMX_COOP_ZERO,false);
    unsigned page=player->weapons.page;
    unsigned weapon=page?player->weapons.weapon:player->body[0x33]/2;
    if(!weapon || weapon>8) continue; /* Keep this seat's reserved blank column. */
    unsigned energy=page?player->weapons.energy[(page-1)*8+weapon-1]:player->energy[weapon*2-1];
    const MmxSpriteAsset *native=page?NULL:MmxRenderAssetsWeaponX(weapon,false);
    const uint16_t *palette=page?MmxWeaponsPalette(page,weapon,false):native?native->colors:NULL;
    MmxSpriteAsset bar={0};bar.live_tiles=true;
    if(palette) memcpy(bar.colors,palette,sizeof(bar.colors));
    coop_meter_row(ppu,r,y,view,objects,colors,x+16,energy,28,3,palette?&bar:NULL,icon_y,inverted);
    if(inverted) {
      coop_inverted_badge_row(ppu,r,y,view,objects,colors,x+16,icon_y,3,palette?&bar:NULL,
          native,page?MmxWeaponsHudIcon(page,weapon):NULL,false);
    } else if(!page) {
      sprite(ppu,r,x+16,icon_y,0x3620,16,y,view,objects,false,native,0x20,colors,true,false,false);
    } else {
      const MmxWeaponPose *icon=MmxWeaponsHudIcon(page,weapon);int row=y-icon_y;
      if(icon && palette && row>=0 && row<16) for(int col=0;col<16;++col) {
        unsigned pixel=icon->pixels[row*16+col];int dx=x+16+col+view.extra;
        if(pixel && dx>=0 && dx<view.width) {objects[dx]=(uint16_t)(0xe6b0|pixel);colors[dx]=palette[pixel];}
      }
    }
  }
}
static void player_overlay_plane_row(
    const MmxRenderPlayerOverlayPlane *plane, const uint16_t *palette,
    unsigned palette_count, int y, int zx, int zy, bool mirror, unsigned z,
    MmxRenderView view, uint16_t *objects, int *object_colors) {
  if (!plane || !plane->pixels || !plane->width || !plane->height ||
      !palette || !palette_count) return;
  int row = y - zy + 64 - plane->origin_y;
  if (row < 0 || row >= plane->height) return;
  for (int col = 0; col < plane->width; ++col) {
    unsigned pixel = plane->pixels[row * plane->width + col];
    int canvas_col = plane->origin_x + col;
    int dx;
    if (!pixel || pixel >= palette_count) continue;
    dx = zx + (mirror ? 63 - canvas_col : canvas_col - 64) + view.extra;
    if (dx >= 0 && dx < view.width) {
      objects[dx] = (uint16_t)(z | (pixel & 15));
      object_colors[dx] = palette[pixel];
    }
  }
}
static void world_sprite_row(const MmxRenderWorldSprite *sprite, int y,
                             MmxRenderView view, uint16_t *objects,
                             int *object_colors) {
  int sx, sy, row;
  if (!sprite || !sprite->pixels || !sprite->width || !sprite->height ||
      !sprite->palette || !sprite->palette_count) return;
  sx = (int)(sprite->world_x - (int32_t)view_camera(frame.ram, 0x1e4d));
  sy = (int)(sprite->world_y - (int32_t)view_camera(frame.ram, 0x1e50));
  row = y - sy - sprite->origin_y;
  if (row < 0 || row >= sprite->height) return;
  for (int col = 0; col < sprite->width; ++col) {
    unsigned pixel = sprite->pixels[row * sprite->width + col];
    int dx = sx + (sprite->facing_left ?
        -1 - sprite->origin_x - col : sprite->origin_x + col) + view.extra;
    if (!pixel || pixel >= sprite->palette_count ||
        dx < 0 || dx >= view.width) continue;
    objects[dx] = (uint16_t)(sprite->z | pixel);
    object_colors[dx] = sprite->palette[pixel];
  }
}
static uint32_t debug_rgb555(uint16_t color) {
  unsigned red = color & 31;
  unsigned green = (color >> 5) & 31;
  unsigned blue = (color >> 10) & 31;
  red = (red << 3) | (red >> 2);
  green = (green << 3) | (green >> 2);
  blue = (blue << 3) | (blue >> 2);
  return (red << 16) | (green << 8) | blue;
}
static void draw_debug_rect(uint32_t *out, MmxRenderView view,
                            const MmxRenderDebugRect *rect) {
  int64_t left, top, right, bottom;
  int x0, x1;

  if (!out || !rect || !rect->w || !rect->h) return;
  left = (int64_t)rect->world_x - view_camera(frame.ram, 0x1e4d) + view.extra;
  top = (int64_t)rect->world_y - view_camera(frame.ram, 0x1e50);
  right = left + rect->w - 1;
  bottom = top + rect->h - 1;
  x0 = left < 0 ? 0 : left >= view.width ? view.width - 1 : (int)left;
  x1 = right < 0 ? 0 : right >= view.width ? view.width - 1 : (int)right;
  if (left >= view.width || right < 0 || top >= MMX_RENDER_HEIGHT ||
      bottom < 0 || x0 > x1)
    return;

  const uint32_t color = debug_rgb555(rect->rgb555);
  if (top >= 0 && top < MMX_RENDER_HEIGHT)
    for (int x = x0; x <= x1; ++x) out[(int)top * view.width + x] = color;
  if (bottom >= 0 && bottom < MMX_RENDER_HEIGHT && bottom != top)
    for (int x = x0; x <= x1; ++x) out[(int)bottom * view.width + x] = color;
  if (left >= 0 && left < view.width && bottom - top > 1) {
    int y0 = top + 1 < 0 ? 0 : top + 1 >= MMX_RENDER_HEIGHT ? MMX_RENDER_HEIGHT : (int)top + 1;
    int y1 = bottom - 1 >= MMX_RENDER_HEIGHT ? MMX_RENDER_HEIGHT - 1 : (int)bottom - 1;
    for (int y = y0; y <= y1; ++y) out[y * view.width + (int)left] = color;
  }
  if (right >= 0 && right < view.width && right != left && bottom - top > 1) {
    int y0 = top + 1 < 0 ? 0 : top + 1 >= MMX_RENDER_HEIGHT ? MMX_RENDER_HEIGHT : (int)top + 1;
    int y1 = bottom - 1 >= MMX_RENDER_HEIGHT ? MMX_RENDER_HEIGHT - 1 : (int)bottom - 1;
    for (int y = y0; y <= y1; ++y) out[y * view.width + (int)right] = color;
  }
}
bool MmxRendererDraw(uint32_t *out, MmxRenderView view, bool hud) {
  if (!out || !frame.valid || view.width < 256 || view.width > MMX_RENDER_MAX_WIDTH ||
      view.extra != (view.width - 256) / 2 || (view.width & 1)) return false;
  memset(&stats, 0, sizeof(stats)); stats.pieces = frame.piece_count;
  memset(door_cache, 0, sizeof(door_cache));
  memset(out, 0, (size_t)view.width * 224 * sizeof(*out));
  bool zero_menu = zero_weapons_menu();
  bool weapon_menu = MmxWeaponsMenuVisible(frame.ram);
  bool menu = zero_menu || weapon_menu;
  bool zero_title = zero_title_menu();
  bool stage = MmxWidePolicy_IsStageScene(frame.ram) && !menu;
  view_dx=view_dy=0;
  if(stage && peer_seat>=0 && frame_coop.initialized && MmxCoopViewsOnline()) {
    MmxCoopView camera=MmxCoopViewForPlayer(frame.ram,&frame_coop,(unsigned)peer_seat);
    view_dx=camera.x-(int)word(frame.ram,0x1e4d);
    view_dy=camera.y-(int)word(frame.ram,0x1e50);
  }
  if(stage && frame_coop.initialized &&
      frame_coop.players[frame_coop.current^1].character==MMX_COOP_X)
    prepare_partner_graphics();
  bool swapping = stage && MmxZeroEnabled() && frame_zero.swap_phase;
  unsigned weapon_page = menu ? frame_weapons.menu_page : frame_weapons.page;
  unsigned weapon_item = menu ? frame.ram[0x1ed2] : frame_weapons.weapon;
  const uint16_t *weapon_colors = MmxWeaponsPalette(weapon_page, weapon_item, true);
  if(stage && frame_coop.initialized && !weapon_page) {
    const MmxSpriteAsset *native=MmxRenderAssetsWeaponX(frame.ram[0xbdb]/2,true);
    if(native) weapon_colors=native->colors;
  }
  MmxSpriteAsset x_weapon_palette = {0};
  x1_weapon_palette(&x_weapon_palette,weapon_colors,weapon_page);
  if(stage && frame_coop.initialized) coop_sting_palette(frame.ram,&x_weapon_palette);
  unsigned knc_bugfix_seat=frame_coop.initialized?frame_coop.current:0;
  bool knc_bugfix=stage && (!MmxZeroEnabled() || frame_zero.active_x) &&
      MmxKncBugfixActive(knc_bugfix_seat);
  if(knc_bugfix) {
    if(!weapon_colors) {
      const MmxSpriteAsset *native=MmxRenderAssetsWeaponX(frame.ram[0xbdb]/2,true);
      if(native) x1_weapon_palette(&x_weapon_palette,native->colors,0);
    }
    MmxRenderAssetsStingPalette(MmxKncBugfixPhase(),x_weapon_palette.colors);
    weapon_colors=x_weapon_palette.colors;
  }
  prepare_stage_planes();
  LightBeam beams[2];
  unsigned beam_count = stage ? spark_lights(beams, view.extra) : 0;
  unsigned palette_fade = stage && g_mmx_render_asset_repairs ?
      MmxRenderAssetsDeathPaletteFade(frame.ram, frame.lines[0].palette) : 0;
  const Piece *pieces = frame.expand && g_mmx_render_asset_repairs ? frame.expanded : frame.pieces;
  unsigned piece_count = frame.expand && g_mmx_render_asset_repairs ? frame.expanded_count : frame.piece_count;
  const uint8_t *zero = stage || zero_menu || zero_title ? MmxZeroPose(frame.ram, &frame_zero) : NULL;
  const uint8_t *blade = stage && zero ? MmxZeroBlade(&frame_zero) : NULL;
  const uint8_t *charge = stage && zero ? MmxZeroChargePose(&frame_zero) : NULL;
  const MmxWeaponShot *triad=NULL,*gravity=NULL;
  if (stage && !swapping) for (unsigned i=0;i<8;++i) {
    const MmxWeaponShot *s=frame_weapon_combat.shots+i;
    if (s->active && s->page==2 && s->weapon==3 && s->charged && !s->variant && s->muzzle_pose) triad=s;
    if (s->active && s->page==2 && s->weapon==6 && s->charged && s->variant==1) gravity=s;
  }
  if (triad && zero) {
    /* X3 Zero has no ground-punch action. Reuse his original windup and
     * crouched strike body poses, with the X3 Triad action's own timing. */
    MmxZeroState strike=frame_zero;strike.burst=0;strike.air=0;
    strike.slash=triad->pose==39?1:triad->pose==41?3:triad->pose==29?6:
        triad->pose==30?9:triad->pose==31?12:triad->pose==32?15:21;
    zero=MmxZeroPose(frame.ram,&strike);blade=NULL;
  }
  if(gravity && zero) {
    /* Zero never used Gravity Well in X3. Adapt original group-4A raised-arm
     * body poses to the source cast timing, including its held pose. */
    MmxZeroState cast=frame_zero;cast.burst=cast.slash=0;cast.anim_valid=1;
    cast.anim_pose=gravity->pose==39 || gravity->pose==40?0:
        gravity->pose==41 || gravity->pose==44?0x43:gravity->pose==29 || gravity->pose==45?0x44:
        gravity->pose==35?0x45:gravity->pose==36?0x46:0x47;
    zero=MmxZeroPose(frame.ram,&cast);blade=NULL;
  }
  const MmxWeaponShot *cast_body=triad?triad:gravity;
  Piece waiting[128];
  unsigned waiting_count = stage && g_mmx_render_asset_repairs ?
      fortress_waiting_pieces(waiting, pieces, piece_count) : 0;
  const MmxSpriteAsset *waiting_zero = waiting_count ? MmxRenderAssetsCaptiveZero() : NULL;
  const MmxSpriteAsset *piece_assets[MAX_PIECES] = {0};
  if (stage && g_mmx_render_asset_repairs) for (unsigned i = 0; i < piece_count; ++i) {
    const Piece *s = &pieces[i];
    const MmxSpriteAsset *a = MmxRenderAssetsObjectSprite(frame.ram, s->object, s->animation);
    /* OBJ palette 0 is the shared hit flash. If the current CHR binding
     * still matches, that deliberate palette change must remain live. */
    bool hit_flash = a && a->current &&
        (s->attr & 255) == ((s->tile + a->tile_base) & 255) &&
        ((s->attr >> 8) & 15) == (a->attributes & 1);
    /* Keep current allocations and their live flashes/animation. Repair
     * missing or stale bindings using the ROM resource's own palette. */
    if (a && !hit_flash && (!a->current || (s->attr & 255) != ((s->tile + a->tile_base) & 255) ||
        ((s->attr >> 8) & 15) != (unsigned)((a->attributes & 15) | (s->palette_pose&14)) ||
        (s->object == 0xe18 && MmxRenderAssetsRideArmorPalettePending(frame.ram,
            frame.lines[0].palette + 128 + ((s->attr >> 9) & 7) * 16)))) piece_assets[i] = a;
  }
  int16_t crystal_ripple[224];MmxWeaponsTimeRipple(&frame_weapon_combat,crystal_ripple);
  if (frame_coop.initialized && MmxWeaponsTimePhase(&frame_coop.players[frame_coop.current^1].combat)==1 &&
      MmxWeaponsTimePhase(&frame_weapon_combat)!=1)
    MmxWeaponsTimeRipple(&frame_coop.players[frame_coop.current^1].combat,crystal_ripple);
  for (int y = 0; y < 224; ++y) {
    const Raster *r = &frame.lines[y]; Ppu p;
    memcpy(&p, r->registers, PPU_SAVESTATE_REGS_SIZE);
    if (beam_count) p.cgwsel = (p.cgwsel & 0xcf) | 0x20;
    if ((p.bgmode & 7) != 1 || (!stage && !menu && !zero_title)) {
      memcpy(out + y * view.width + view.extra, frame.stock + y * 256, 256 * sizeof(*out));
      ++stats.fallback_lines; continue;
    }
    ++stats.custom_lines;
    if (p.inidisp & 128) continue;
    uint8_t brightness[32];
    for (int c = 0; c < 32; ++c) brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (p.inidisp & 15) / 15);
    /* Dialogue masks BG1/BG2 for the panel and subtracts white from the
     * exposed backdrop ($81:915E). That global subtraction also blacks out
     * Storm's transparent sky between clouds in the newly exposed margins.
     * Recognize the captured panel setup across stages; preserve its native
     * window/text and every other color effect, including transition fades. */
    bool dialogue_backdrop = g_mmx_render_asset_repairs && p.cgadsub == 0xa0 &&
        p.cgwsel == 0x80 && p.fixedColor == 0x7fff && p.windowsel == 0x22 &&
        p.screenWindowed[0] == 3 && (p.screenEnabled[0] & 7) == 7;
    uint16_t objects[MMX_RENDER_MAX_WIDTH] = {0};
    int object_colors[MMX_RENDER_MAX_WIDTH];
    for (int x = 0; x < view.width; ++x) object_colors[x] = -1;
    for (int i = (int)waiting_count - 1; i >= 0; --i) {
      Piece s = waiting[i];
      const MmxSpriteAsset *asset = s.animation == 0x53 ? waiting_zero : NULL;
      sprite(&p, r, s.x, s.y, s.attr, s.size, y, view, objects, true, asset, s.tile, object_colors, true, false, false);
    }
    bool replaced[128] = {false};
    bool zero_drawn[2] = {false,false};
    for (int i = (int)piece_count - 1; i >= 0; --i) {
      Piece s = pieces[i]; const MmxSpriteAsset *asset = piece_assets[i];
      bool frozen_enemy=stage && (MmxWeaponsFrozenEnemy(&frame_weapon_combat,s.object) ||
          MmxWeaponsMovedEnemy(&frame_weapon_combat,s.object) || (frame_coop.initialized &&
          (MmxWeaponsFrozenEnemy(&frame_coop.players[frame_coop.current^1].combat,s.object) ||
           MmxWeaponsMovedEnemy(&frame_coop.players[frame_coop.current^1].combat,s.object))));
      if (g_mmx_render_asset_repairs && fortress_sound_actor(s.object) &&
          (s.x >= 256 || s.x + s.size <= 0)) continue;
      /* Recorded pieces already obey the retail submission budget. Draw
       * their entire footprint, including x=255 which native D76A clips.
       * Only the explicit expanded list can add pieces beyond that budget. */
      bool center = g_mmx_render_asset_repairs;
      /* $80:9A3E spawns READY in $1CE8; $81:F091 selects group $19.
       * Restrict the recolor to this actor during the arrival phase. */
      bool red_ready=stage && MmxZeroEnabled() && !frame_zero.active_x &&
          frame.ram[0xd3]==2 && s.object==0x1ce8 && s.animation==0x19;
      bool red_death = stage && s.animation == 0x1d && MmxZeroDeathOrbRed(frame.ram,s.object);
      bool coop_buster=stage && frame_coop.initialized && s.object>=0x1228 && s.object<0x1428 &&
          ((frame.ram[s.object+10]==3 && s.animation==0x9e) ||
           ((frame.ram[s.object+10]==1 || frame.ram[s.object+10]==2) && s.animation==0x0e));
      bool menu_body = s.object == 0x1988 && (s.animation == 0 || s.animation == 0x18);
      bool zero_body = zero && (s.object == 0xba8 || menu_body);
      bool triad_x_body=cast_body && !zero && s.object==0xba8;
      bool triad_armor=cast_body && (s.object==0xc38 || s.object==0xc58 || s.object==0xc78);
      bool swap_actor = swapping && (s.object == 0xba8 || s.object == 0xc38 ||
          s.object == 0xc58 || s.object == 0xc78 || s.object == 0xc98);
      bool zero_charge = zero && stage && MmxZeroNativeChargeObject(s.object,frame.ram[s.object+10]);
      bool zero_armor = zero && (s.object == 0xc38 || s.object == 0xc58 || s.object == 0xc78 ||
          (zero_menu && (s.object == 0x1928 || s.object == 0x1948 || s.object == 0x1968)));
      bool oam_match = false;
      if (g_mmx_render_asset_repairs || coop_buster || zero_body || zero_armor || zero_charge || swap_actor || red_ready || red_death || triad_x_body || triad_armor || frozen_enemy) for (int slot = 16; slot < 128; ++slot) {
        unsigned pos = r->oam[slot * 2], hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
        int ox = (pos & 255) | ((hi & 1) << 8); if (ox >= 256) ox -= 512;
        if (ox == s.x && (pos >> 8) == ((unsigned)s.y & 255) && r->oam[slot * 2 + 1] == s.attr) {
          oam_match = true;
          replaced[slot] = true; center = true;
        }
      }
      /* Match original OAM before reprojection. HUD and native script panels
       * keep screen coordinates; world pieces use the selected peer origin. */
      if(stage) {s.x-=view_dx;s.y-=view_dy;}
      /* Resource bindings own CHR/palette, not OBJ priority. Kuwanger's
       * platform backs deliberately sit behind the tower even though their
       * foreground pieces use the same resource. Preserve both priority bits
       * when substituting art, including a missing section's resource. */
      unsigned attr = asset ? (s.attr & 0xf000) | ((asset->attributes & 15) << 8) |
          (asset->live_tiles ? s.attr & 255 : 0) : s.attr;
      if (asset && asset->live_colors) attr = (attr & ~0x0e00u) | (s.attr & 0x0e00u);
      if (zero_armor || swap_actor || triad_armor || frozen_enemy) continue;
      if (zero_charge && MmxZeroHasChargeArt() && !frame.ram[0xbdb] && !weapon_item) continue;
      if (triad_x_body) {
        if (oam_match && !zero_drawn[0]) {
          zero_drawn[0]=true;
          weapon_sprite_row(cast_body,cast_body->pose,
              (int16_t)(word(frame.ram,0xbad)-view_camera(frame.ram,0x1e4d)),
              (int16_t)(word(frame.ram,0xbb0)-view_camera(frame.ram,0x1e50)),y,view,objects,object_colors);
        }
        continue;
      }
      if (zero_body && !oam_match && !(frame.expand && frame.ram[s.object + 14] &&
          (view_dx || view_dy || s.x + s.size <= 0 || s.x >= 256))) continue;
      if(stage && frame_coop.initialized && frame_zero.active_x && frame.ram[0xc31] &&
          s.object>=0xba8 && s.object<0xc98 && !oam_match && !view_dx && !view_dy &&
          s.x<256 && s.x+s.size>0) continue;
      if (zero_body) {
        if (!zero_drawn[menu_body]) {
          zero_drawn[menu_body] = true;
          const uint8_t *body = menu_body ? MmxZeroMenuPose() : zero;
          int zx = menu_body ? 128 : (int16_t)(word(frame.ram, 0xbad) - view_camera(frame.ram,0x1e4d));
          int zy = menu_body ? 152 : (int16_t)(word(frame.ram, 0xbb0) - view_camera(frame.ram,0x1e50)) + MmxZeroPoseOffsetY(frame.ram);
          bool pilot = !menu_body && (s.animation==0x6a || s.animation==0x6b);
          bool player_overlay = !menu_body && frame_player_overlay.active;
          if(pilot && !player_overlay) {
            /* X1 switches to a separate pilot group on boarding. Its pose
             * numbers are not movement poses. Keep its authored entry/walk/
             * punch offsets and expose Zero's original helmet/shoulders over
             * the cockpit. Vanilla X3 Zero has no Ride Armor pilot artwork. */
            const uint8_t *layout=sprite_arrangement(s.animation,frame.ram[0xbbf]&127);
            if(layout && layout[0]) {
              int dx=(int8_t)layout[1]+5;
              zx+=(frame.ram[0xbb9]&64)?-dx:dx;
              zy+=8+(int8_t)layout[2]+20;
            }
            body=MmxZeroMenuPose();
          }
          if (player_overlay) {
            if (pilot)
              align_wide_pilot_overlay(&frame_player_overlay, s.animation,
                                       &zx, &zy);
            unsigned z = ((((s.attr >> 12) & 3) * 4 + 2) << 12) | 0x680;
            player_overlay_row(&frame_player_overlay, y, zx, zy, z, view,
                               objects, object_colors);
          }
          int row = y - zy + 64;
          if (!player_overlay && row >= 0 && row < MMX_ZERO_HEIGHT && (!pilot || (row>=44 && row<64))) {
            const uint16_t *colors = MmxZeroColors();
            unsigned z = ((((s.attr >> 12) & 3) * 4 + 2) << 12) | 0x680;
            for (int col = 0; col < MMX_ZERO_WIDTH; ++col) {
              unsigned pixel = body[row * MMX_ZERO_WIDTH + col];
              if (!menu_body && blade && blade[row * MMX_ZERO_WIDTH + col]) pixel = blade[row * MMX_ZERO_WIDTH + col];
              int dx = zx + ((!menu_body && (frame.ram[0xbb9] & 64)) ? 63 - col : col - 64) + view.extra;
              if (pixel && dx >= 0 && dx < view.width) {
                objects[dx] = (uint16_t)(z | (pixel & 15));
                /* Charged Sting cycles X1's live player palette. Map Zero's
                 * material/shade roles to that palette instead of discarding
                 * the native cycle; skin and the dark outline remain legible. */
                static const uint8_t sting_shade[16] = {0,9,4,6,1,14,15,7,8,9,11,10,4,2,3,12};
                bool sting = !menu_body && frame.ram[0xc31] && pixel >= 16 && pixel < 32;
                if (sting) {
                  objects[dx] = (uint16_t)((z & ~255u) | (144 + sting_shade[pixel - 16]));
                  object_colors[dx] = -1; /* Live CGRAM already includes fades. */
                } else object_colors[dx] = !menu_body && pixel >= 16 && pixel < 32 ?
                    MmxZeroBodyColors(&frame_zero)[pixel - 16] : colors[pixel];
              }
            }
          }
        }
        continue;
      }
      if (zero_charge) {
        /* Spread the original motes around Zero's taller body. Keep each
         * sparkle's pixel art and native animation, timing and visibility. */
        int cx = (int16_t)(word(frame.ram,0xbad) - view_camera(frame.ram,0x1e4d));
        int cy = (int16_t)(word(frame.ram,0xbb0) - view_camera(frame.ram,0x1e50));
        s.x += (s.x + s.size / 2 - cx) / 4;
        s.y += (s.y + s.size / 2 - cy) / 4 - 6;
      }
      /* Extended choices have their own original X palette. Their native
       * buster proxy must not show the palette of an unrelated locked X1
       * weapon while navigating the pause screen. Preserve hit flashes. */
      /* The weapon palette is OBJ palette 1 (CGRAM 144..159, $81:9E8F).
       * Pieces drawn with another palette (the charge glow, armor parts)
       * keep live CGRAM: substituting them lost the glow and flattened
       * the boots. */
      if ((frame_zero.active_x || knc_bugfix) && weapon_colors && zero_actor(s.object, s.animation) &&
          (menu || ((attr & 0x0e00) == 0x0200 && (!x_charging(frame.ram) || knc_bugfix)))) asset = &x_weapon_palette;
      if(stage && frame_coop.initialized && s.object>=0x1228 && s.object<0x1428 &&
          frame.ram[s.object+10]==0x0c && s.animation==0x47 && (s.attr&0x0e00)==0x0600)
        asset=MmxRenderAssetsWeaponX(6,false);
      if(coop_buster) {
        const MmxSpriteAsset *beam=MmxRenderAssetsChargedBuster(s.animation,piece_pose(&s));
        if(beam) asset=beam;
      }
      sprite(&p, r, s.x, s.y, attr, s.size, y, view, objects, !center, asset, s.tile, object_colors, true, false, red_ready || red_death);
    }
    if (stage) coop_partner_row(&p,r,y,view,objects,object_colors);
    int bar_first = -1, bar_count = 0;
    if (hud) for (int slot = 16; slot <= 48; ++slot) {
      unsigned pos = r->oam[slot * 2], attr = r->oam[slot * 2 + 1];
      unsigned tile = attr & 255, hi = (r->high_oam[slot / 4] >> (slot % 4 * 2)) & 1;
      bool bar = !hi && (pos & 255) >= 216 && (pos >> 8) < 96 && ((attr >> 9) & 7) == 2 &&
          (tile == 128 || tile == 130 || tile == 132 || tile == 134 || tile == 170);
      if (bar) { if (bar_first < 0) bar_first = slot; ++bar_count; }
      else if (bar_first >= 0) break;
    }
    static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
    bool coop_hud=stage && frame_coop.initialized && r->oam[0]==0x5008 && r->oam[1]==0x3486;
    for (int slot = 127; slot >= 0; --slot) {
      if(coop_hud && slot<16) continue;
      if (replaced[slot]) continue;
      unsigned pos = r->oam[slot * 2], attr = r->oam[slot * 2 + 1];
      unsigned hi = r->high_oam[slot / 4] >> (slot % 4 * 2);
      int x = (pos & 255) | ((hi & 1) << 8), sy = pos >> 8;
      if (x >= 256) x -= 512;
      int size = sizes[p.obsel >> 5][(hi >> 1) & 1];
      if (x + size <= 0 || x >= 256) continue;
      /* Only the player's HUD badge: not weapon icons, bosses or actors.
       * This remains visible while the player blinks or has no body pieces. */
      bool zero_icon = (stage || zero_menu) && MmxZeroEnabled() && !frame_zero.active_x && slot == 0 &&
          x == 8 && sy == 80 && attr == 0x3486 && size == 16;
      bool anchored = stage && hud && sy < 96 && (slot < 16 || (bar_count >= 4 && slot >= bar_first && slot < bar_first + bar_count));
      if(stage && !anchored) {x-=view_dx;sy-=view_dy;}
      if (anchored) { if (x < 25) x -= view.extra; else if (x >= 216) x += view.extra; }
      if (stage && frame_weapons.page && frame_weapons.weapon && slot == 7 && (pos & 255) == 24 && sy == 80 && attr == 0x3620) {
        /* Dedicated source gameplay footer, with its original frame,
         * pixels and weapon palette. Menu icons are a different asset. */
        const MmxWeaponPose *icon=MmxWeaponsHudIcon(frame_weapons.page,frame_weapons.weapon);
        const uint16_t *colors=MmxWeaponsPalette(frame_weapons.page,frame_weapons.weapon,false);
        int row=y-sy;
        if (icon && colors && row>=0 && row<16) for(int col=0;col<16;++col) {
          unsigned pixel=icon->pixels[row*16+col];
          int dx=x+col+view.extra;
          if(pixel && dx>=0 && dx<view.width) {
            objects[dx]=(uint16_t)(0xe6b0|pixel);object_colors[dx]=colors[pixel];
          }
        }
        continue;
      }
      sprite(&p, r, x, sy, attr, size, y, view, objects, false, NULL, 0, object_colors, false, zero_icon, false);
    }
    if (charge && zero_drawn[0] && !swapping) {
      int zx=(int16_t)(word(frame.ram,0xbad)-view_camera(frame.ram,0x1e4d));
      int zy=(int16_t)(word(frame.ram,0xbb0)-view_camera(frame.ram,0x1e50))-8;
      int row=y-zy+64;
      unsigned palette=frame_zero.charge>=81 && frame_zero.charge<201 ? 32 : 0;
      if(row>=0 && row<MMX_ZERO_HEIGHT) for(int col=0;col<MMX_ZERO_WIDTH;++col) {
        unsigned pixel=charge[row*MMX_ZERO_WIDTH+col];
        int dx=zx+((frame.ram[0xbb9]&64)?63-col:col-64)+view.extra;
        if(pixel && dx>=0 && dx<view.width) {
          objects[dx]=(uint16_t)(0xe680|pixel); object_colors[dx]=MmxZeroColors()[palette+pixel];
        }
      }
    }
    if (stage && !swapping) {
      weapon_effects_row(&frame_weapon_combat,&p,r,y,view,objects,object_colors);
      if (frame_coop.initialized && frame_coop.players[frame_coop.current^1].status==MMX_COOP_ALIVE && !frame_coop.scene_owner)
        weapon_effects_row(&frame_coop.players[frame_coop.current^1].combat,&p,r,y,view,objects,object_colors);
    }
    if (stage) for (unsigned i = 0; i < frame_world_sprite_count; ++i)
      world_sprite_row(&frame_world_sprites[i], y, view, objects, object_colors);
    if (swapping) teleport_actor_row(frame.ram,&frame_zero,&p,r,y,view,objects,object_colors);
    if(coop_hud) coop_hud_row(&p,r,y,view,hud,objects,object_colors);
    for (int sx = 0; sx < view.width; ++sx) {
      int x = sx - view.extra;
      if (menu && (x < 0 || x >= 256)) { out[y * view.width + sx] = 0; continue; }
      uint16_t screens[2] = {0x500, 0x500}, bg[3] = {0};
      int bg_colors[3] = {-1, -1, -1};
      for (int layer = 0; layer < 3; ++layer) if ((p.screenEnabled[0] | p.screenEnabled[1]) & (1 << layer)) {
        int bx = x, by = y + 1;
        if (stage && layer==0) by+=crystal_ripple[y];
        if (p.mosaic & (1 << layer)) { int size = (p.mosaic >> 4) + 1;
          bx -= ((bx % size) + size) % size; by -= by % size; }
        bg[layer] = background(&p, r, layer, bx, by, stage, &bg_colors[layer]);
      }
      if (palette_fade) {
        for (int layer = 0; layer < 3; ++layer) if (bg_colors[layer] >= 0)
          bg_colors[layer] = MmxRenderAssetsFadeColor((uint16_t)bg_colors[layer], palette_fade);
        if (object_colors[sx] >= 0)
          object_colors[sx] = MmxRenderAssetsFadeColor((uint16_t)object_colors[sx], palette_fade);
      }
      for (int sub = 0; sub < 2; ++sub) {
        for (int layer = 0; layer < 3; ++layer)
          if ((p.screenEnabled[sub] & (1 << layer)) &&
              (!(p.screenWindowed[sub] & (1 << layer)) || !window(&p, layer, x, view.extra)) && bg[layer] > screens[sub]) screens[sub] = bg[layer];
        if ((p.screenEnabled[sub] & 16) && (!(p.screenWindowed[sub] & 16) || !window(&p, 4, x, view.extra)) &&
            objects[sx] > screens[sub]) screens[sub] = objects[sx];
      }
      bool color_window = window(&p, 5, x, view.extra);
      if (beam_count) {
        color_window = false;
        for (unsigned i = 0; i < beam_count; ++i)
          color_window |= x >= beams[i].left[y] && x <= beams[i].right[y];
      }
      out[y * view.width + sx] = colour(&p, r->palette, brightness, screens[0], screens[1], color_window, object_colors[sx], bg_colors,
                                       dialogue_backdrop && (x < 0 || x >= 256));
    }
  }
  for (unsigned i = 0; i < frame_debug_rect_count; ++i)
    draw_debug_rect(out, view, &frame_debug_rects[i]);
  return true;
}
