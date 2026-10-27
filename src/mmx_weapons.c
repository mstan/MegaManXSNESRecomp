#include "mmx_weapons.h"
#include "mmx_weapon_combat.h"
#include "mmx_armor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WEAPONS = 16, GROUPS = 3, POSES = 128 };
typedef struct WeaponGroup {
  unsigned id, count, animation_size;
  const uint8_t *animation;
  uint16_t colors[16];
  MmxWeaponPose pose[POSES];
} WeaponGroup;
typedef struct WeaponArt {
  uint16_t body[16], colors[16];
  uint16_t icon_colors[16];
  MmxWeaponPose icon;
  MmxWeaponPose hud_icon;
  unsigned groups;
  WeaponGroup group[GROUPS];
} WeaponArt;
static WeaponArt art[WEAPONS];
static uint8_t *asset;
static uint8_t *page_assets[2];
static MmxWeaponsState state;
_Static_assert(sizeof(MmxWeaponsState) == 40 && offsetof(MmxWeaponsState,fraction) == MMX_WEAPONS_LEGACY_STATE_SIZE, "Weapon save ABI");
static unsigned word(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned weapon_index(unsigned page, unsigned weapon) { return (page - 1) * 8 + weapon - 1; }
static bool valid_weapon(unsigned page, unsigned weapon) { return page >= 1 && page <= 2 && weapon >= 1 && weapon <= 8; }
static void initialize(void) {
  if (!state.initialized) { memset(&state, 0, sizeof(state)); memset(state.energy, 28, 16); state.initialized = 1; }
}
bool MmxWeaponsEnabled(void) { return asset || page_assets[0] || page_assets[1]; }
bool MmxWeaponsPageEnabled(unsigned page) { return page>=1 && page<=2 && art[(page-1)*8].groups; }
bool MmxWeaponsActive(void) { return MmxWeaponsPageEnabled(state.page) && valid_weapon(state.page, state.weapon); }
MmxWeaponsState MmxWeaponsGetState(void) { return state; }
bool MmxWeaponsValidState(const MmxWeaponsState *s) {
  if (!s || s->page > 2 || s->weapon > 8 || s->menu_page > 2 || s->initialized > 1 ||
      s->charge > 201 || s->reserved) return false;
  for (unsigned i = 0; i < 16; ++i) if (s->energy[i] > 28 || (s->energy[i] == 28 && s->fraction[i])) return false;
  return true;
}
void MmxWeaponsSetState(MmxWeaponsState s) {
  memset(&state, 0, sizeof(state));
  if (MmxWeaponsValidState(&s)) state = s;
  if (MmxWeaponsEnabled()) {
    initialize();
    if (state.page && !MmxWeaponsPageEnabled(state.page)) state.page=state.weapon=0;
    if (state.menu_page && !MmxWeaponsPageEnabled(state.menu_page)) state.menu_page=0;
  }
}
void MmxWeaponsDisable(void) {
  MmxWeaponsCancelShots(NULL);
  free(asset); asset = NULL; memset(art, 0, sizeof(art)); memset(&state, 0, sizeof(state));
  for (unsigned i=0;i<2;++i) { free(page_assets[i]);page_assets[i]=NULL; }
}
static bool load(const char *path, unsigned page) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return false;
  if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
  long size = ftell(f); rewind(f);
  if (size < 12 || size > 2 * 1024 * 1024) { fclose(f); return false; }
  uint8_t *data = malloc((size_t)size);
  WeaponArt *candidate = calloc(WEAPONS, sizeof(*candidate));
  bool ok = data && candidate && fread(data, (size_t)size, 1, f) == 1;
  fclose(f);
  if (!ok) { free(data); free(candidate); return false; }
  unsigned count=page ? 8 : 16,first=page ? (page-1)*8 : 0;
  ok = !memcmp(data, "MMXWEAP5", 8) && word(data+8)==count && !word(data+10);
  size_t pos = 12;
  for (unsigned i = first; i < first+count && ok; ++i) {
    if (pos + 612 > (size_t)size) { ok = false; break; }
    const uint8_t *p = data + pos;
    WeaponArt *w = candidate + i;
    ok = p[0] == 2 + i / 8 && p[1] == 1 + i % 8 && p[2] >= 1 && p[2] <= GROUPS && !p[3];
    w->groups = p[2];
    for (unsigned c = 0; c < 16; ++c) { w->body[c] = (uint16_t)word(p + 4 + c * 2); w->colors[c] = (uint16_t)word(p + 36 + c * 2); }
    for (unsigned c = 0; c < 16; ++c) w->icon_colors[c] = (uint16_t)word(p + 68 + c * 2);
    w->icon = (MmxWeaponPose){-8,-8,16,16,p + 100};
    w->hud_icon = (MmxWeaponPose){0,0,16,16,p + 356};
    for (unsigned n = 0; n < 512; ++n) if (p[100 + n] > 15) ok = false;
    pos += 612;
    for (unsigned g = 0; g < w->groups && ok; ++g) {
      if (pos + 38 > (size_t)size) { ok = false; break; }
      WeaponGroup *group = w->group + g;
      group->id = word(data + pos); group->count = word(data + pos + 2);
      group->animation_size = word(data + pos + 4); pos += 6;
      for (unsigned c=0;c<16;++c) group->colors[c]=(uint16_t)word(data+pos+c*2);
      pos+=32;
      if (group->animation_size < 5 || group->animation_size > 8192 || pos + group->animation_size > (size_t)size) { ok = false; break; }
      group->animation = data + pos; pos += group->animation_size;
      if (group->count > POSES || !group->count) { ok = false; break; }
      for (unsigned j = 0; j < group->count && ok; ++j) {
        if (pos + 8 > (size_t)size) { ok = false; break; }
        MmxWeaponPose *pose = group->pose + j;
        pose->left = (int16_t)word(data + pos); pose->top = (int16_t)word(data + pos + 2);
        pose->width = (uint16_t)word(data + pos + 4); pose->height = (uint16_t)word(data + pos + 6); pos += 8;
        size_t length = (size_t)pose->width * pose->height;
        if (pose->width > 256 || pose->height > 256 || pos + length > (size_t)size ||
            pose->left < -256 || pose->left > 256 || pose->top < -256 || pose->top > 256) { ok = false; break; }
        pose->pixels = data + pos;
        for (size_t n = 0; n < length; ++n) if (data[pos + n] > 15) { ok = false; break; }
        pos += length;
      }
    }
  }
  ok = ok && pos == (size_t)size;
  if (ok) {
    if (!page) { MmxWeaponsDisable(); asset = data; }
    else { free(page_assets[page-1]);page_assets[page-1]=data; }
    memcpy(art+first,candidate+first,count*sizeof(*art));initialize();
  }
  else free(data);
  free(candidate); return ok;
}
bool MmxWeaponsLoad(const char *path) { return load(path,0); }
bool MmxWeaponsLoadPage(const char *path,unsigned page) { return page>=1 && page<=2 && load(path,page); }
const MmxWeaponPose *MmxWeaponsPose(unsigned page, unsigned weapon, unsigned group, unsigned pose) {
  if (!MmxWeaponsPageEnabled(page) || !valid_weapon(page, weapon)) return NULL;
  const WeaponArt *w = art + weapon_index(page, weapon);
  /* UINT_MAX asks for the weapon's primary group (including its native icon). */
  for (unsigned i = 0; i < w->groups; ++i) if ((group == UINT32_MAX || w->group[i].id == group) && pose < w->group[i].count)
    return w->group[i].pose + pose;
  return NULL;
}
const uint16_t *MmxWeaponsPalette(unsigned page, unsigned weapon, bool body) {
  if (!MmxWeaponsPageEnabled(page) || !valid_weapon(page, weapon)) return NULL;
  const WeaponArt *w = art + weapon_index(page, weapon); return body ? w->body : w->colors;
}
const MmxWeaponPose *MmxWeaponsIcon(unsigned page, unsigned weapon) {
  return MmxWeaponsPageEnabled(page) && valid_weapon(page, weapon) ? &art[weapon_index(page, weapon)].icon : NULL;
}
const uint16_t *MmxWeaponsGroupPalette(unsigned page,unsigned weapon,unsigned group) {
  if (!MmxWeaponsPageEnabled(page) || !valid_weapon(page,weapon)) return NULL;
  const WeaponArt *w=art+weapon_index(page,weapon);
  for (unsigned i=0;i<w->groups;++i) if(w->group[i].id==group) return w->group[i].colors;
  return NULL;
}
const MmxWeaponPose *MmxWeaponsHudIcon(unsigned page, unsigned weapon) {
  return MmxWeaponsPageEnabled(page) && valid_weapon(page, weapon) ? &art[weapon_index(page, weapon)].hud_icon : NULL;
}
const uint8_t *MmxWeaponsAnimation(unsigned page, unsigned weapon, unsigned group, unsigned *size) {
  if (!MmxWeaponsPageEnabled(page) || !size || !valid_weapon(page, weapon)) return NULL;
  const WeaponArt *w = art + weapon_index(page, weapon);
  for (unsigned i = 0; i < w->groups; ++i) if (w->group[i].id == group) {
    *size = w->group[i].animation_size; return w->group[i].animation;
  }
  return NULL;
}
const uint16_t *MmxWeaponsIconPalette(unsigned page, unsigned weapon) {
  return MmxWeaponsPageEnabled(page) && valid_weapon(page, weapon) ? art[weapon_index(page, weapon)].icon_colors : NULL;
}
const char *MmxWeaponsLabel(unsigned page, unsigned weapon) {
  static const char *labels[16] = {"C.HUNTER", "B.SPLASH", "S.SHOT", "S.WHEEL", "S.SLICER", "S.CHAIN", "M.MINE", "S.BURNER",
      "ACID.B", "P.BOMB", "T.THUNDR", "S.BLADE", "R.SPLASH", "G.WELL", "F.SHIELD", "T.FANG"};
  return valid_weapon(page, weapon) ? labels[weapon_index(page, weapon)] : "";
}
unsigned MmxWeaponsEnergyRead(unsigned address, unsigned original) {
  if (!MmxWeaponsActive()) return original;
  if (address == 0xbdb) return 2; /* HUD/pickup routines only: virtual inventory index. */
  unsigned i = weapon_index(state.page,state.weapon), value = 0xc0 | state.energy[i];
  return address == 0x1f85 ? (value << 8) | state.fraction[i] : value;
}
static unsigned energy_amount(unsigned i) { return state.energy[i]*256 + state.fraction[i]; }
static void set_energy(unsigned i, unsigned value) {
  if (value > 28*256) value = 28*256;
  state.energy[i] = (uint8_t)(value >> 8); state.fraction[i] = (uint8_t)value;
}
unsigned MmxWeaponsEnergyAmount(unsigned page, unsigned weapon) {
  return MmxWeaponsPageEnabled(page) && valid_weapon(page,weapon) ? energy_amount(weapon_index(page,weapon)) : 0;
}
bool MmxWeaponsSpend(unsigned page, unsigned weapon, unsigned cost) {
  if (!MmxWeaponsPageEnabled(page) || !valid_weapon(page,weapon)) return false;
  unsigned i = weapon_index(page,weapon), value = energy_amount(i);
  if (cost > value) return false;
  set_energy(i,value-cost); return true;
}
bool MmxWeaponsEnergyStore(unsigned value, bool pickup) {
  if (!MmxWeaponsActive()) return false;
  if (pickup) {
    set_energy(weapon_index(state.page,state.weapon),value & 0x3fff);
  }
  return true; /* Guest inventory stays untouched, including its dirty flags. */
}
void MmxWeaponsEnergyOverflow(uint8_t r[0x20000], unsigned index) {
  /* Native auto-refill has already scanned X1's owned weapons. Only the
   * unconsumed remainder after that complete scan can fill new inventory. */
  if (!MmxWeaponsEnabled() || index < 18) return;
  unsigned amount = word(r), initial = amount;
  for (unsigned i=0;i<16 && amount;++i) {
    if (!MmxWeaponsPageEnabled(i/8+1)) continue;
    unsigned value = energy_amount(i), add = 28*256-value; if (add > amount) add = amount;
    set_energy(i,value+add); amount -= add;
  }
  if (amount != initial) {
    unsigned i = r[0xba3] & 30;
    r[0xb72+i] = 0x0d; r[0xb73+i] = 0; r[0xba3] = (uint8_t)((i+2)&30);
  }
}
void MmxWeaponsRefill(void) {
  MmxArmorRefill();
  if (MmxWeaponsEnabled()) { memset(state.energy,28,sizeof(state.energy)); memset(state.fraction,0,sizeof(state.fraction)); }
}
bool MmxWeaponsMenuVisible(const uint8_t r[0x20000]) {
  return MmxWeaponsEnabled() && r && r[0xd1] == 2 && r[0xd2] == 4 &&
      (((r[0x1f10] == 6 || r[0x1f10] == 8) && (r[0xc3] & 128)) ||
       (r[0x1989] == 1 && word(r + 0x198d) == 128 && word(r + 0x1990) == 160 &&
        (r[0x199e] == 0 || r[0x199e] == 0x18)));
}
void MmxWeaponsMenuTick(uint8_t r[0x20000], unsigned dp) {
  if (!MmxWeaponsEnabled() || !r || dp > 0x1ff00) return;
  unsigned button = r[0xbe2] & 0x30;
  if (button != 0x10 && button != 0x20) return;
  do { state.menu_page = (uint8_t)((state.menu_page + (button == 0x10 ? 1 : 2)) % 3); }
  while (state.menu_page && !MmxWeaponsPageEnabled(state.menu_page));
  unsigned cursor = r[dp + 10];
  if (!state.menu_page && cursor >= 1 && cursor <= 8 && !(r[0x1f86 + cursor * 2] & 64)) r[dp + 10] = 0;
  /* Use the native menu movement sound and its existing SPC command ring. */
  unsigned index = r[0xba3] & 0x1e;
  r[0xb72 + index] = 0x2c; r[0xb73 + index] = 0; r[0xba3] = (uint8_t)((index + 2) & 0x1e);
  r[0xbe2] &= (uint8_t)~0x30;
}
unsigned MmxWeaponsMenuRead(uint8_t r[0x20000], unsigned pc, unsigned dp, unsigned index, unsigned original) {
  if (!MmxWeaponsEnabled() || !r || dp > 0x1ff00) return original;
  switch (pc & 0xffff) {
    case 0xc484:
      state.menu_page = state.page;
      return state.page ? state.weapon * 2 : original;
    case 0xc767:
      return state.menu_page ? (state.page == state.menu_page ? state.weapon * 2 : 0) : original;
    case 0xce28: {
      unsigned cursor = r[dp + 10], old_page = state.page;
      if (cursor < 9) {
        MmxWeaponsCancelShots(r);
        if (old_page || state.menu_page) r[0x1f12] = 0; /* Rebuild the native energy HUD. */
        state.page = state.menu_page; /* Buster remains part of the chosen set. */
        state.weapon = (uint8_t)cursor; state.charge = state.cooldown = 0;
        if (old_page || state.menu_page) return 254; /* Run native weapon cleanup even between two extended weapons. */
      }
      return original;
    }
    case 0xce33:
      return state.menu_page && original < 9 ? 0 : original; /* Native actor retains safe buster resources. */
    default:
      return state.menu_page && index < 16 ? 0xc0 | state.energy[(state.menu_page - 1) * 8 + index / 2] : original;
  }
}
