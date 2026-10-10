/* ROM-backed checks for the MMX adapters and the real shared desktop host.
 * The caller supplies an empty working directory; only slot 12 is used. */
#define MMX_DESKTOP_ENTRY MmxDesktopMain
#include "desktop/host_main.c"
#include MMX_GAME_MAIN
#include "common/launcher_binds.h"
#include "mmx_weapons.h"
#include "snes/interp_bridge.h"

static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
  printf("ok: %s\n", what);
}
static void frame(unsigned input) {
  MmxBeforeFrame();
  if(getenv("MMX_COOP_VIEWS_TEST") || getenv("MMX_COOP_VIEWS_BOOT_TEST") ||
      getenv("MMX_COOP_ONLINE_FIXTURE")) MmxCoopViewsSetOnline(true);
  RtlRunFrame(input | (1u << 30));
  CaptureSimulationFrame(1);
}
static void replay(int count) {
  for (int i = 0; i < count; ++i) frame(i < count / 2 ? SNES_PAD_RIGHT : 0);
}
static void same(const void *a, size_t an, const void *b, size_t bn, const char *what) {
  if (an != bn || memcmp(a, b, an)) {
    size_t i = 0;
    while (i < an && i < bn && ((const uint8 *)a)[i] == ((const uint8 *)b)[i]) ++i;
    fprintf(stderr, "first difference at %zu / %zu / %zu\n", i, an, bn);

  }
  check(an && an == bn && !memcmp(a, b, an), what);
}
static void zero_replay(int n) { for (int i=0;i<n;++i) frame(0); }
static void rewind_six_frames(void) {
  /* The framework harness requests interval 1; the game's default is 6. */
  for (int i=0;i<6 && snes_rewind_selected_seconds() < 0.0999f;++i)
    snes_rewind_step(-1);
}
static unsigned zero_projectiles(unsigned kind) {
  unsigned count = 0;
  for (unsigned d=0x1228;d<0x1428;d+=64) count += g_ram[d] && g_ram[d+10] == kind;
  return count;
}
static void zero_capture(const char *base, const char *suffix) {
  if (!base) return;
  char path[4096]; snprintf(path, sizeof(path), "%s%s", base, suffix);
  check(MmxRendererSaveCapture(path), "Zero renderer capture saved");
}
#include "mmx_coop_test.inc"
#include "mmx_netplay_test.inc"
#include "mmx_zero_modern_test.inc"
#include "mmx_graphics_pacing_test.inc"
#include "mmx_hitbox_overlay_test.inc"
#include "mmx_saber_lock_test.inc"
#include "mmx_coop_replay_test.inc"
static void zero_motion_checks(const char *fixture) {
  const char *path = getenv("MMX_ZERO_MOTION_REFERENCE");
  if (!path) path = MMX_ZERO_MOTION_REFERENCE_DEFAULT;
  FILE *f = fopen(path,"r"); check(f != NULL,"X3 movement reference opens");
  char line[256], previous[32] = ""; unsigned x0 = 0, y0 = 0, checked = 0;
  check(fgets(line,sizeof(line),f) != NULL,"X3 movement reference header");
  while (fgets(line,sizeof(line),f)) {
    char name[32], keys[32]; int tick,x,y,vx,vy,action,sub,pose,group,ground,visibility;
    check(sscanf(line,"%31[^,],%d,%31[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
          name,&tick,keys,&x,&y,&vx,&vy,&action,&sub,&pose,&group,&ground,&visibility) == 13,
          "X3 movement reference row");
    if (strcmp(name,previous)) {
      check(RtlLoadSnapshot(fixture),"restore movement fixture");
      zero_replay(10); g_ram[0xbac] = g_ram[0xbaf] = 0;
      x0 = g_ram[0xbad] << 8 | g_ram[0xbae] << 16;
      y0 = g_ram[0xbb0] << 8 | g_ram[0xbb1] << 16;
      snprintf(previous,sizeof(previous),"%s",name);
    }
    unsigned input = strstr(keys,"Right") ? SNES_PAD_RIGHT : 0;
    if (strchr(keys,'A')) input |= SNES_PAD_A;
    if (strchr(keys,'B')) input |= SNES_PAD_B;
    frame(input);
    int dx = (int)(g_ram[0xbac] | g_ram[0xbad] << 8 | g_ram[0xbae] << 16) - (int)x0;
    int dy = (int)(g_ram[0xbaf] | g_ram[0xbb0] << 8 | g_ram[0xbb1] << 16) - (int)y0;
    MmxZeroState z = MmxZeroGetState();
    if (dx != x || dy != y || (int16_t)(g_ram[0xbc2] | g_ram[0xbc3] << 8) != vx ||
        (int16_t)(g_ram[0xbc4] | g_ram[0xbc5] << 8) != vy ||
        (tick > 1 && (!z.anim_valid || z.anim_pose != pose))) {
      fprintf(stderr,"X3 reference mismatch %s/%d: xy %d,%d vs %d,%d pose %u vs %d valid %u\n",
              name,tick,dx,dy,x,y,z.anim_pose,pose,z.anim_valid); exit(1);
    }
    ++checked;
  }
  fclose(f); printf("ok: %u original-X3 movement/animation reference frames\n",checked);
}
static void zero_combat_checks(const char *fixture) {
  FILE *f=fopen(MMX_ZERO_COMBAT_REFERENCE_DEFAULT,"r"); check(f!=NULL,"X3 combat reference opens");
  char line[160],previous[16]=""; unsigned count=0; int y0=0;
  check(fgets(line,sizeof(line),f)!=NULL,"X3 combat reference header");
  while(fgets(line,sizeof(line),f)) {
    char mode[16],phase[24],keys[16]; int tick,pose,dy,vy,emitted;
    check(sscanf(line,"%15[^,],%23[^,],%d,%15[^,],%d,%d,%d,%d",mode,phase,&tick,keys,&pose,&dy,&vy,&emitted)==8,"X3 combat reference row");
    if(strcmp(mode,previous)) {
      check(RtlLoadSnapshot(fixture),"restore combat fixture");
      g_ram[0xbaf]=0xed; /* Original fixture's fractional Y, including landing snap. */
      for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
      y0=g_ram[0xbaf] | g_ram[0xbb0]<<8 | g_ram[0xbb1]<<16;
      snprintf(previous,sizeof(previous),"%s",mode);
    }
    unsigned input=strchr(keys,'Y')?SNES_PAD_Y:0;
    if(strchr(keys,'B')) input|=SNES_PAD_B;
    unsigned before=zero_projectiles(3);
    frame(input);
    MmxZeroState z=MmxZeroGetState();
    int actual_pose=(int)((MmxZeroPose(g_ram,&z)-MmxZeroMenuPose())/(128*128));
    int actual_y=(int)(g_ram[0xbaf] | g_ram[0xbb0]<<8 | g_ram[0xbb1]<<16)-y0;
    int actual_vy=(int16_t)(g_ram[0xbc4]|g_ram[0xbc5]<<8);
    if(actual_pose!=pose || actual_y!=dy || actual_vy!=vy ||
       (emitted && zero_projectiles(3)<=before)) {
      fprintf(stderr,"combat mismatch %s/%s/%d pose %d/%d dy %d/%d vy %d/%d emitted %u/%d burst %u/%u\n",
        mode,phase,tick,actual_pose,pose,actual_y,dy,actual_vy,vy,zero_projectiles(3)>before,emitted,z.burst,z.burst_end);
      fprintf(stderr,"native action=%u sub=%u pose=%u seqbase=%u mirror=%u/%u/%u\n",g_ram[0xbaa],g_ram[0xbab],g_ram[0xbbf],g_ram[0xc17],z.anim_offset,z.anim_pose,z.anim_timer);
      exit(1);
    }
    ++count;
  }
  fclose(f); printf("ok: %u original-X3 combat animation/physics reference frames\n",count);
}
static void zero_menu_pixels(void) {
  static uint32_t pixels[256 * 224]; unsigned body = 0, icon = 0;
  static uint32_t baseline[48 * 56]; static bool have_baseline;
  check(MmxRendererDraw(pixels,(MmxRenderView){256,0,4.0/3.0},false),"Zero menu renders");
  const uint32_t *stock = MmxRendererStockFrame(); check(stock != NULL,"menu stock reference exists");
  for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) if (pixels[y * 256 + x] != stock[y * 256 + x]) {
    if (x >= 104 && x < 152 && y >= 128 && y < 184) ++body;
    else if (x >= 192 && x < 216 && y >= 139 && y < 163) ++icon;
    else { fprintf(stderr,"Unexpected Zero menu change at %d,%d\n",x,y); exit(1); }
  }
  check(body > 100 && icon == 0,"menu replaces body and preserves X's original life head exactly");
  bool same_body = true;
  for (int y = 128; y < 184; ++y) for (int x = 104; x < 152; ++x) {
    unsigned at = (y - 128) * 48 + x - 104;
    if (have_baseline) same_body &= pixels[y * 256 + x] == baseline[at];
    else baseline[at] = pixels[y * 256 + x];
  }
  check(same_body,"armor ownership leaves the same Zero menu body");
  have_baseline = true;
}
static void zero_weapon_checks(const char *fixture, const char *capture) {
  for (unsigned weapon = 0; weapon <= 8; ++weapon) for (unsigned charged = 0; charged < 2; ++charged) {
    check(RtlLoadSnapshot(fixture),"restore weapon fixture");
    if (weapon) {
      g_ram[0x1f85 + weapon * 2] = 0; g_ram[0x1f86 + weapon * 2] = 0xdc;
      for (int i = 0; i < 6; ++i) frame(SNES_PAD_R);
      zero_replay(1); check(g_ram[0xbdb] == weapon * 2,"native switch selects requested X1 weapon");
    }
    if (charged) {
      g_ram[0x1f99] = 2;
      for (int i = 0; i < 181; ++i) frame(SNES_PAD_Y);
      if (!weapon) zero_capture(capture,".charge.cap");
    }
    unsigned seen = 0;
    for (int i = 0; i < (charged && weapon == 3 ? 95 : 35); ++i) {
      frame(!charged && i == 0 ? SNES_PAD_Y : 0);
      for (unsigned d = 0x1228; d < 0x1428; d += 64) if (g_ram[d] && g_ram[d + 10] < 32)
        seen |= 1u << g_ram[d + 10];
      if (i == 0 && !charged && !weapon) {
        unsigned d = 0x1228;
        check((int)(g_ram[d+5] | g_ram[d+6]<<8) - (int)(g_ram[0xbad] | g_ram[0xbae]<<8) == 27 &&
              (int)(g_ram[d+8] | g_ram[d+9]<<8) - (int)(g_ram[0xbb0] | g_ram[0xbb1]<<8) == -7,
              "standing shot uses original Zero pose 31 muzzle, translated to X1 feet");
      }
      if (i == 2) { char suffix[64]; snprintf(suffix,sizeof(suffix),".weapon%u-%u.cap",weapon,charged); zero_capture(capture,suffix); }
      if (i == 26 && charged && (weapon == 2 || weapon == 3)) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),".effect%u.cap",weapon); zero_capture(capture,suffix);
      }
      if (i == 32 && charged && weapon == 2) zero_capture(capture,".chameleon-next.cap");
      if (i == 84 && charged && weapon == 3) zero_capture(capture,".shield.cap");
      if (i == 0 && !charged && weapon == 5)
        check((int)(g_ram[0x122d] | g_ram[0x122e]<<8) - (int)(g_ram[0xbad] | g_ram[0xbae]<<8) == 22,
              "Tornado starts six pixels farther along Zero's facing direction");
      if (i == 0 && !charged && weapon == 7)
        check((int)(g_ram[0x1230] | g_ram[0x1231]<<8) - (int)(g_ram[0xbb0] | g_ram[0xbb1]<<8) == -8,
              "Boomerang launches above X's original origin");
      if (i == 20 && charged && weapon == 3)
        check((int)(g_ram[0x1230] | g_ram[0x1231]<<8) - (int)(g_ram[0xbb0] | g_ram[0xbb1]<<8) == -6,
              "Rolling Shield collision and art follow Zero's raised center");
    }
    unsigned expected_kind = weapon ? weapon + (charged ? 15 : 6) : charged ? 3 : 0;
    check(seen & (1u << expected_kind),"X1 weapon produces its expected normal/charged projectile class");
    if (weapon) check((g_ram[0x1f85 + weapon*2] | (g_ram[0x1f86 + weapon*2] & 63)<<8) < 0x1c00,
                      "X1 weapon spends native energy");
  }
  check(RtlLoadSnapshot(fixture),"restore for left-facing muzzle");
  frame(SNES_PAD_LEFT); frame(0); frame(SNES_PAD_Y);
  check((int)(g_ram[0x122d] | g_ram[0x122e]<<8) - (int)(g_ram[0xbad] | g_ram[0xbae]<<8) == -27 &&
        (int)(g_ram[0x1230] | g_ram[0x1231]<<8) - (int)(g_ram[0xbb0] | g_ram[0xbb1]<<8) == -7,
        "native facing mirrors Zero muzzle without changing its height");
}
static void zero_menu_transition_checks(const char *fixture, const char *capture, unsigned armor) {
  check(RtlLoadSnapshot(fixture),"restore for menu transition probe");
  g_ram[0x1f99] = (uint8_t)armor;
  bool repairs = g_mmx_render_asset_repairs; g_mmx_render_asset_repairs = false;
  static uint32_t pixels[256 * 224]; unsigned previous = ~0u;
  for (unsigned direction = 0; direction < 2; ++direction) for (unsigned i = 0; i < 120; ++i) {
    frame(i == 0 ? SNES_PAD_START : 0);
    check(MmxRendererDraw(pixels,(MmxRenderView){256,0,4.0/3.0},false),"menu transition renders");
    MmxRenderStats stats = MmxRendererGetStats();
    unsigned changed = 0, lit = 0;
    const uint32_t *stock = MmxRendererStockFrame();
    /* Keep background repair out of the comparison. Only the character can
     * change this rectangle; the HUD badge and life head are outside it. */
    for (int y = 80; y < 184; ++y) for (int x = 104; x < 152; ++x) {
      unsigned p = y * 256 + x;
      changed += pixels[p] != stock[p]; lit += stock[p] != 0;
    }
    if (lit && changed < 20) {
      fprintf(stderr,"menu missing Zero %u/%u: changed %u lit %u\n",direction,i,changed,lit);
      char suffix[80]; snprintf(suffix,sizeof(suffix),".missing%u-%u.cap",direction,i);zero_capture(capture,suffix);
      check(false,"visible menu-transition body remains Zero");
    }
    unsigned signature = g_ram[0xd3] | g_ram[0xd4] << 8 | g_ram[0x1f10] << 16 | g_ram[0xc3] << 24;
    if (signature != previous) {
      fprintf(stderr,"menu transition %u/%u state %08x custom %u fallback %u\n",direction,i,signature,stats.custom_lines,stats.fallback_lines);
      char suffix[80]; snprintf(suffix,sizeof(suffix),".transition%u-%u.cap",direction,i);zero_capture(capture,suffix);
      previous = signature;
    }
  }
  g_mmx_render_asset_repairs = repairs;
}
static void zero_swap_checks(const char *fixture, uint8 *start, uint8 *expected, uint8 *actual, size_t cap) {
  check(RtlLoadSnapshot(fixture),"restore for grounded Select swap");
  zero_replay(10);
  frame(SNES_PAD_Y); frame(0);
  check(zero_projectiles(0)!=0,"live native projectile participates in frozen-world check");
  uint8_t objects[0x1d08-0xe18], camera[6], position[6];
  memcpy(camera,g_ram+0x1e4d,6); memcpy(position,g_ram+0xbac,6);
  unsigned hp=g_ram[0xbcf], weapon=g_ram[0xbdb], upgrades=g_ram[0x1f99];
  const char *capture=getenv("MMX_ZERO_TEST_CAPTURE");
  for(unsigned direction=0;direction<2;++direction) {
    frame(SNES_PAD_SELECT);
    if(!MmxZeroSwapping()) fprintf(stderr,"swap gates: %02x %02x %02x %02x %02x %02x; %02x %02x %02x %02x; vx %02x%02x\n",
        g_ram[0xd1],g_ram[0xd2],g_ram[0xba9],g_ram[0xbaa],g_ram[0xbab],g_ram[0xbb6],
        g_ram[0x1f0c],g_ram[0x1f10],g_ram[0x1f23],g_ram[0x1f48],g_ram[0xbc3],g_ram[0xbc2]);
    check(MmxZeroSwapping(),"grounded Select starts exchange");
    memcpy(objects,g_ram+0xe18,sizeof(objects));
    unsigned phases=0, ticks=0;
    while(MmxZeroSwapping() && ticks++<160) {
      MmxZeroState s=MmxZeroGetState();
      if(!(phases&(1u<<s.swap_phase))) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),".swap%u-phase%u.cap",direction,s.swap_phase);
        zero_capture(capture,suffix); phases|=1u<<s.swap_phase;
      }
      if(ticks==12) {
        size_t n=RtlSaveSnapshotToMemory(start,cap);
        zero_replay(5); size_t en=RtlSaveSnapshotToMemory(expected,cap);
        check(RtlLoadSnapshotFromMemory(start,n),"mid-teleport state loads");
        zero_replay(5); size_t an=RtlSaveSnapshotToMemory(actual,cap);
        same(expected,en,actual,an,"mid-teleport replay preserves phase and frozen world");
      }
      check(!memcmp(objects,g_ram+0xe18,sizeof(objects)) && !memcmp(camera,g_ram+0x1e4d,6),
            "enemies items scripts and camera remain frozen");
      frame(direction ? SNES_PAD_SELECT : 0);
    }
    check(!MmxZeroSwapping() && phases==0x7e,"all six teleport phases finish");
    check(MmxZeroActive()==(direction!=0),"exchange changes playable character");
    check(!memcmp(position,g_ram+0xbac,6) && g_ram[0xbcf]==hp &&
        g_ram[0xbdb]==weapon && g_ram[0x1f99]==upgrades,"swap preserves feet health weapon and equipment");
    check(MmxZeroUpgradeBits(0x81971c,0)==(direction?8:0),"X and Zero retain separate dash/charge capabilities");
    check(g_snes->cart->rom[0x32555]==(direction?18:14),"terrain/damage bounds follow active character");
    if(direction) {
      frame(SNES_PAD_SELECT); frame(SNES_PAD_SELECT);
      check(!MmxZeroSwapping() && MmxZeroActive(),"holding Select cannot retrigger an exchange");
    }
    zero_replay(3);
    char suffix[48]; snprintf(suffix,sizeof(suffix),".swap%u-done.cap",direction); zero_capture(capture,suffix);
  }
  frame(SNES_PAD_B); frame(SNES_PAD_SELECT);
  check(!MmxZeroSwapping(),"Select cannot swap in midair");
  check(RtlLoadSnapshot(fixture),"restore after Select checks");
}
static void zero_health_swap(void) {
  frame(SNES_PAD_SELECT);
  check(MmxZeroSwapping(),"HP exchange begins");
  for(unsigned i=0;i<160 && MmxZeroSwapping();++i) frame(0);
  check(!MmxZeroSwapping(),"HP exchange completes");
  zero_replay(3);
}
static void zero_health_pickup(unsigned small) {
  /* Retail health actor (item kind 2), collected through native collision. */
  memset(g_ram+0x1628,0,48); g_ram[0x1628]=1;
  g_ram[0x1632]=2; g_ram[0x1633]=(uint8_t)(128|small);
  memcpy(g_ram+0x162d,g_ram+0xbad,2);
  unsigned y=(g_ram[0xbb0] | g_ram[0xbb1]<<8)-16;
  g_ram[0x1630]=(uint8_t)y; g_ram[0x1631]=(uint8_t)(y>>8);
  zero_replay(55);
  check(!g_ram[0x1628],"native health pickup finishes collection");
}
static void zero_ready_checks(const char *fixture) {
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore READY fixture");
    zero_replay(10);
    if(character) zero_health_swap();
    g_ram[0xbcf]=128; g_ram[0xbaa]=0x0c; g_ram[0xbab]=0;
    bool seen=false;
    for(unsigned i=0;i<1000;++i) {
      frame(0);
      if(g_ram[0xd3]==2 && g_ram[0x1ce9] && g_ram[0x1cfe]==0x19 && !g_ram[0x1cf4]) {
        zero_replay(28); /* Group $19 pose 12: fully formed lettering. */
        zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-ready.cap":".zero-ready.cap");
        uint32_t pixels[256*224];
        check(MmxRendererDraw(pixels,(MmxRenderView){256,0,4.0/3.0},true),"READY frame renders");
        const uint32_t *stock=MmxRendererStockFrame();
        unsigned changed=0,red_pixels=0;
        for(unsigned y=96;y<128;++y) for(unsigned x=80;x<176;++x) {
          unsigned index=y*256+x,color=pixels[index];
          if(color!=stock[index]) {
            ++changed;
            if(((color>>16)&255)>(color&255)+40 && ((color>>16)&255)>((color>>8)&255)+40) ++red_pixels;
          }
        }
        check(character ? !changed : changed>20 && changed==red_pixels,
            "Zero READY changes only blue lettering to red; X READY matches original");
        seen=true; break;
      }
    }
    check(seen,"native respawn reaches the original READY banner");
  }
  puts("MMX READY CHECKS PASSED");
}
static void zero_health_checks(const char *fixture, uint8 *start, uint8 *expected, uint8 *actual, size_t cap) {
  check(RtlLoadSnapshot(fixture),"restore for separate HP");
  zero_replay(10);
  g_ram[0xbcf]=7|128; frame(0);
  check(MmxZeroGetState().hp[0]==7 && MmxZeroGetState().hp[1]==16,"damage affects only active Zero");
  zero_health_swap();
  check(!MmxZeroActive() && (g_ram[0xbcf]&127)==16,"X arrives with his independent HP");
  g_ram[0xbcf]=9|128; frame(0);
  zero_health_pickup(1);
  check((g_ram[0xbcf]&127)==11 && MmxZeroGetState().hp[0]==7,"small health pickup heals X only");
  zero_health_pickup(0);
  check((g_ram[0xbcf]&127)==16 && MmxZeroGetState().hp[0]==7,"large health pickup clamps X without healing Zero");
  zero_health_pickup(1);
  check(MmxZeroGetState().hp[0]==7,"full active pool never redirects pickup healing to reserve");
  zero_health_swap();
  check(MmxZeroActive() && (g_ram[0xbcf]&127)==7,"Zero returns with his previous HP");
  g_ram[0x1f9a]=18; g_ram[0xbcf]=9|128; frame(0);
  check(MmxZeroGetState().hp_max==18 && MmxZeroGetState().hp[1]==16,"heart-tank maximum is shared without refilling reserve HP");
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  zero_health_swap(); size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"separate HP save loads");
  zero_health_swap(); size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"separate HP and exchange replay deterministically");
  unsigned lives=g_ram[0x1f80];
  /* State produced by native lethal damage at $84:9D60. Let the real death
   * animation, life decrement and checkpoint initializer perform the rest. */
  g_ram[0xbcf]=128; g_ram[0xbaa]=0x0c; g_ram[0xbab]=0;
  bool respawned=false;
  for(unsigned i=0;i<1000;++i) {
    frame(0); MmxZeroState s=MmxZeroGetState();
    if(g_ram[0x1f80]+1==lives && s.hp_valid && s.hp[0]==18 && s.hp[1]==18 && g_ram[0xba9]==2) {
      respawned=true; break;
    }
  }
  check(respawned,"native death loses one life and respawn refills both HP pools");
  check(RtlLoadSnapshot(fixture),"restore after separate HP checks");
}
static void weapon_menu_checks(const char *assets, const char *fixture, uint8 *start,
                               uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets), "original X2/X3 weapon cache loads");
  check(RtlLoadSnapshot(fixture), "legacy gameplay fixture initializes new weapons");
  for (unsigned i=0;i<16;++i) check(MmxWeaponsGetState().energy[i]==28,"new weapons unlocked with full energy");
  uint8 inventory[16]; memcpy(inventory,g_ram+0x1f88,16);
  frame(SNES_PAD_START); zero_replay(75);
  check(MmxWeaponsMenuVisible(g_ram),"native pause opens for weapon pages");
  for (unsigned page=1;page<=2;++page) {
    frame(SNES_PAD_R); zero_replay(3);
    check(MmxWeaponsGetState().menu_page==page,"R advances extended weapon page");
    for (unsigned i=1;i<=4;++i) {
      frame(SNES_PAD_DOWN); zero_replay(3);
      check(g_ram[0x1ed2]==i,"left-column extended weapon selectable while X1 is locked");
    }
    frame(SNES_PAD_UP); zero_replay(2); frame(SNES_PAD_RIGHT); zero_replay(2);
    check(g_ram[0x1ed2]==8,"native horizontal cursor selects extended right column");
    for (unsigned i=7;i>=5;--i) {
      frame(SNES_PAD_UP); zero_replay(3);
      check(g_ram[0x1ed2]==i,"right-column extended weapon selectable while X1 is locked");
    }
    char suffix[40]; snprintf(suffix,sizeof(suffix),".x%u-menu.cap",page+1);
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),suffix);
    frame(SNES_PAD_START); zero_replay(75);
    MmxWeaponsState s=MmxWeaponsGetState();
    check(s.page==page && s.weapon==5 && g_ram[0xbdb]==0,"pause confirms extended selection with safe native buster resources");
    check(!memcmp(inventory,g_ram+0x1f88,16),"extended selection preserves native X1 progression and energy");
    if (page==1) {
      frame(SNES_PAD_START); zero_replay(75);
      check(MmxWeaponsGetState().menu_page==1 && g_ram[0x1ed2]==5,"pause reopens at extended selection");
      frame(SNES_PAD_LEFT); zero_replay(3); check(!g_ram[0x1ed2],"native cursor returns to buster");
    }
  }
  MmxWeaponsState s=MmxWeaponsGetState(); s.energy[0]=7; s.energy[15]=13;
  s.fraction[0]=128;s.fraction[15]=64;MmxWeaponsSetState(s);
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  frame(SNES_PAD_START); zero_replay(75); frame(SNES_PAD_R); zero_replay(3);
  check(!MmxWeaponsGetState().menu_page,"R wraps to original X1 page");
  check(!g_ram[0x1ed2],"return to locked X1 page selects available buster");
  size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"weapon selection and partial energy restore");
  s=MmxWeaponsGetState(); check(s.page==2 && s.weapon==5 && s.energy[0]==7 && s.energy[15]==13 &&
      s.fraction[0]==128 && s.fraction[15]==64,"independent fractional weapon state survives save/load");
  frame(SNES_PAD_START); zero_replay(75); frame(SNES_PAD_R); zero_replay(3);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"weapon-menu deterministic replay");
  frame(SNES_PAD_START); zero_replay(75);
  check(!MmxWeaponsActive() && !g_ram[0xbdb],"X1 buster selection exits extended weapon mode");
  check(MmxWeaponsGetState().fraction[0]==128 && MmxWeaponsGetState().fraction[15]==64,"menu selection preserves reserve energy fractions");
  check(!memcmp(inventory,g_ram+0x1f88,16),"page cycle and save/load leave original inventory intact");
  zero_health_swap(); check(MmxZeroGetState().active_x,"weapon-page test switches to X");
  frame(SNES_PAD_START); zero_replay(75); frame(SNES_PAD_L); zero_replay(3);
  check(MmxWeaponsGetState().menu_page==2,"L wraps backward from X1 to X3 for X");
  frame(SNES_PAD_DOWN); zero_replay(3);
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".x-menu.cap");
  frame(SNES_PAD_START); zero_replay(75);
  check(MmxZeroGetState().active_x && MmxWeaponsGetState().page==2 &&
        MmxWeaponsGetState().weapon==1 && !g_ram[0xbdb],"X selects an extended weapon through native pause");
  puts("MMX X2/X3 WEAPON MENU CHECKS PASSED");
}
static void weapon_cycle_buster_check(void) {
  frame(SNES_PAD_Y);frame(0);
  bool fired=false;
  for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d] && g_ram[d+10]==0) fired=true;
  check(fired,"buster fires after shoulder cycling back from an imported weapon");
  zero_replay(90);
  check(!g_ram[0xbdd],"cycled buster shot retires without blocking subsequent weapon switches");
}
static void weapon_cycle_checks(const char *assets,const char *fixture,uint8 *start,size_t cap) {
  check(MmxWeaponsLoad(assets),"load weapons for shoulder cycling");
  for (unsigned character=0;character<2;++character) for (unsigned page=1;page<=2;++page) {
    check(RtlLoadSnapshot(fixture),"restore shoulder cycling fixture");
    zero_replay(10);
    if (character) zero_health_swap();
    uint8 inventory[16]; memcpy(inventory,g_ram+0x1f88,16);
    frame(SNES_PAD_START); zero_replay(75);
    for(unsigned i=0;i<page;++i) { frame(SNES_PAD_R); zero_replay(3); }
    frame(SNES_PAD_START); zero_replay(75);
    check(MmxWeaponsGetState().page==page && !MmxWeaponsGetState().weapon,
        "pause buster selection retains its X2/X3 weapon set");
    weapon_cycle_buster_check();
    for(unsigned i=1;i<=9;++i) {
      frame(SNES_PAD_R); zero_replay(3);
      check(MmxWeaponsGetState().page==page && MmxWeaponsGetState().weapon==i%9 && !g_ram[0xbdb],
          "R cycles within chosen set including buster and never selects X1");
    }
    weapon_cycle_buster_check();
    for(unsigned i=1;i<=9;++i) {
      frame(SNES_PAD_L); zero_replay(3);
      check(MmxWeaponsGetState().page==page && MmxWeaponsGetState().weapon==(9-i)%9 && !g_ram[0xbdb],
          "L cycles backward within chosen set");
    }
    weapon_cycle_buster_check();
    for(unsigned i=0;i<12;++i) frame(SNES_PAD_R);
    check(MmxWeaponsGetState().weapon==1,"holding R advances only once");
    frame(0); frame(SNES_PAD_L|SNES_PAD_R); zero_replay(3);
    check(MmxWeaponsGetState().page==page && !MmxWeaponsGetState().weapon,"both shoulders return to this set's buster");
    weapon_cycle_buster_check();
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    frame(SNES_PAD_R); zero_replay(3);
    check(RtlLoadSnapshotFromMemory(start,n) && MmxWeaponsGetState().page==page && !MmxWeaponsGetState().weapon,
        "save restores selected set while buster is equipped");
    check(!memcmp(inventory,g_ram+0x1f88,16),"extended shoulder cycling preserves X1 progression and energy");
    frame(SNES_PAD_START); zero_replay(75);
    check(MmxWeaponsGetState().menu_page==page && !g_ram[0x1ed2],"pause reopens on the selected set's buster");
    for(unsigned i=page;i<3;++i) { frame(SNES_PAD_R); zero_replay(3); }
    frame(SNES_PAD_START); zero_replay(75);
    g_ram[0x1f8a]=g_ram[0x1f92]=0xdc; /* Own only X1 weapons 2 and 6. */
    frame(SNES_PAD_R); zero_replay(3); check(g_ram[0xbdb]==4 && !MmxWeaponsGetState().page,"X1 R skips locked weapons");
    frame(SNES_PAD_R); zero_replay(3); check(g_ram[0xbdb]==12,"X1 cycle stays on original owned inventory");
    frame(SNES_PAD_R); zero_replay(3); check(!g_ram[0xbdb] && !MmxWeaponsGetState().page,"X1 cycle wraps to native buster");
    weapon_cycle_buster_check();
  }
  puts("MMX WEAPON SHOULDER CYCLE CHECKS PASSED");
}
static void weapon_energy_pickup(unsigned small) {
  memset(g_ram+0x1628,0,48); g_ram[0x1628]=1;
  g_ram[0x1632]=1; g_ram[0x1633]=(uint8_t)(128|small);
  memcpy(g_ram+0x162d,g_ram+0xbad,2);
  unsigned y=(g_ram[0xbb0] | g_ram[0xbb1]<<8)-16;
  g_ram[0x1630]=(uint8_t)y; g_ram[0x1631]=(uint8_t)(y>>8);
}
static void weapon_energy_checks(const char *assets, const char *fixture, uint8 *start,
                                uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets) && RtlLoadSnapshot(fixture),"load energy pickup fixture");
  uint8 inventory[16]; memcpy(inventory,g_ram+0x1f88,16);
  MmxWeaponsState w=MmxWeaponsGetState(); w.page=2;w.weapon=4;w.energy[11]=10;MmxWeaponsSetState(w);
  g_ram[0x1f12]=0; zero_replay(6);
  check(g_ram[0x71c]==24 && g_ram[0x71d]==80,"native weapon energy HUD becomes visible for extended selection");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".energy-hud.cap");
  weapon_energy_pickup(1); zero_replay(55);
  check(!g_ram[0x1628] && MmxWeaponsGetState().energy[11]==12,"small native pickup refills two selected extended energy");
  weapon_energy_pickup(0); zero_replay(8);
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  zero_replay(45);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(!g_ram[0x1628] && MmxWeaponsGetState().energy[11]==20,"large native pickup refills eight selected extended energy");
  check(RtlLoadSnapshotFromMemory(start,n),"restore animated energy refill");
  zero_replay(45);size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"native energy refill resumes deterministically");
  check(!memcmp(inventory,g_ram+0x1f88,16),"HUD and extended pickups preserve X1 inventory");
  w=MmxWeaponsGetState();w.energy[11]=27;w.energy[0]=3;MmxWeaponsSetState(w);
  weapon_energy_pickup(0);zero_replay(55);
  check(MmxWeaponsGetState().energy[11]==28 && MmxWeaponsGetState().energy[0]==10,"overflow fills other extended weapons after native inventory scan");
  w=MmxWeaponsGetState();w.page=w.weapon=0;MmxWeaponsSetState(w);g_ram[0x1f12]=0;
  weapon_energy_pickup(1);zero_replay(55);
  check(MmxWeaponsGetState().energy[0]==12,"buster-mode pickup can auto-refill extended inventory");
  check(!memcmp(inventory,g_ram+0x1f88,16),"extended auto-refill does not unlock X1 weapons");
  g_ram[0x1f88]=0x45; /* Native owned weapon with five energy for comparison. */
  unsigned lives=g_ram[0x1f80];g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;
  bool respawned=false;
  for(unsigned i=0;i<1000;++i) {
    frame(0);
    if(g_ram[0x1f80]+1==lives && (g_ram[0xbcf]&127)==16 && g_ram[0xba9]==2) {respawned=true;break;}
  }
  check(respawned,"native respawn completes during energy check");
  check((g_ram[0x1f88]&63)==5 && MmxWeaponsGetState().energy[0]==12,"checkpoint death preserves energy like native X1 weapons");
  check(RtlLoadSnapshot(fixture),"restore fractional pickup fixture");
  w=MmxWeaponsGetState();w.page=2;w.weapon=5;w.energy[12]=20;w.fraction[12]=128;MmxWeaponsSetState(w);
  weapon_energy_pickup(1);zero_replay(55);
  check(MmxWeaponsEnergyAmount(2,5)==22*256+128,"native small refill preserves half-unit energy");
  w=MmxWeaponsGetState();w.energy[12]=27;w.energy[0]=10;w.fraction[0]=64;MmxWeaponsSetState(w);
  weapon_energy_pickup(1);zero_replay(8);n=RtlSaveSnapshotToMemory(start,cap);
  zero_replay(50);en=RtlSaveSnapshotToMemory(expected,cap);
  /* X1 $81:E0AF..C1 adds/clamps one unit, then forwards only the remaining
   * whole pickup ticks. Preserve that behavior, including the last half-unit
   * discarded by native clamping, and retain the reserve's existing fraction. */
  check(MmxWeaponsEnergyAmount(2,5)==28*256 && MmxWeaponsEnergyAmount(1,1)==11*256+64,
        "fractional pickup caps like native X1 and preserves reserve fractions");
  check(RtlLoadSnapshotFromMemory(start,n),"restore fractional native refill");
  zero_replay(50);an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"fractional refill and overflow replay exactly");
  n=RtlSaveSnapshotToMemory(start,cap);size_t chunk=0;
  for(size_t i=n-8;i>8;--i) {uint32_t magic;memcpy(&magic,start+i,4);if(magic==0x4d4d5854u) {chunk=i;break;}}
  check(chunk!=0,"fractional inventory game chunk located");
  size_t end_inventory=n-sizeof(MmxWeaponCombatState);
  memmove(start+end_inventory-16,start+end_inventory,MMX_WEAPON_COMBAT_LEGACY_SIZE);
  uint32_t legacy=10;memcpy(start+chunk+4,&legacy,4);
  check(RtlLoadSnapshotFromMemory(start,n-16-(sizeof(MmxWeaponCombatState)-MMX_WEAPON_COMBAT_LEGACY_SIZE)),
        "legacy v10 whole-unit weapon save loads");
  check(MmxWeaponsEnergyAmount(1,1)==11*256 && MmxWeaponsEnergyAmount(2,5)==28*256,
        "legacy weapon save retains whole units and initializes zero fractions");
  puts("MMX EXTENDED ENERGY CHECKS PASSED");
}
static unsigned extended_shots(bool charged) {
  MmxWeaponCombatState s=MmxWeaponsGetCombatState(); unsigned count=0;
  for (unsigned i=0;i<8;++i) count += s.shots[i].active && s.shots[i].charged==charged;
  return count;
}
static unsigned acid_variants(unsigned variant, bool charged) {
  MmxWeaponCombatState c=MmxWeaponsGetCombatState(); unsigned n=0;
  for(unsigned i=0;i<8;++i) n+=c.shots[i].active && c.shots[i].weapon==1 &&
    c.shots[i].variant==variant && c.shots[i].charged==charged;
  return n;
}
static void weapon_source_pack_checks(const char *fixture,uint8 *start,uint8 *expected,uint8 *actual,size_t cap) {
  const char *x2=getenv("MMX_SOURCE_X2_CACHE"),*x3=getenv("MMX_SOURCE_X3_CACHE");
  check(x2 && x3,"native source cache paths supplied");
  MmxZeroDisable();MmxWeaponsDisable();
  for(unsigned page=1;page<=2;++page) {
    check(MmxWeaponsLoadPage(page==1?x2:x3,page),"independent source weapon pack loads");
    check(RtlLoadSnapshot(fixture),"restore source pack fixture");
    check(!MmxZeroEnabled() && !MmxZeroActive(),"source weapon pack works with Zero disabled");
    int width,height;MmxPrepareFrame(1280,720,&width,&height);
    check(width==256 && g_mmx_custom_renderer,"weapon pack alone enables native-width compositor");
    check(MmxWeaponsPageEnabled(page) && !MmxWeaponsPageEnabled(3-page),"only selected source pack is available");
    frame(SNES_PAD_START);zero_replay(75);frame(SNES_PAD_R);zero_replay(3);
    check(MmxWeaponsGetState().menu_page==page,"pause skips source pack that is not enabled");
    frame(SNES_PAD_R);zero_replay(3);check(!MmxWeaponsGetState().menu_page,"pause wraps to X1 with one source pack");
    frame(SNES_PAD_L);zero_replay(3);check(MmxWeaponsGetState().menu_page==page,"backward pause selects enabled source pack");
    frame(SNES_PAD_DOWN);zero_replay(3);
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),page==1?".x2-only-menu.cap":".x3-only-menu.cap");
    frame(SNES_PAD_START);zero_replay(75);
    MmxWeaponsState w=MmxWeaponsGetState();w.page=page;w.weapon=page==1?5:1;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(35);check(extended_shots(false)>0,"X fires ported weapon without Zero mod");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(10);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"source-pack-only save loads");
    zero_replay(10);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"source-pack-only projectiles replay exactly");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),page==1?".x2-only-shot.cap":".x3-only-shot.cap");
    MmxWeaponsCancelShots(g_ram);MmxWeaponsDisable();
  }
  check(MmxWeaponsLoadPage(x2,1) && MmxWeaponsLoadPage(x3,2),"both independently extracted packs load together");
  check(MmxWeaponsPageEnabled(1) && MmxWeaponsPageEnabled(2) && MmxWeaponsPose(1,5,135,11),"second pack preserves first pack art");
  puts("MMX SOURCE PACK CHECKS PASSED");
}
static unsigned frost_slot(bool charged) {
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].weapon==7 && c.shots[i].charged==charged) return i;
  return 8;
}
static unsigned triad_count(bool charged,unsigned first,unsigned last) {
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned n=0;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].page==2 && c.shots[i].weapon==3 &&
      c.shots[i].charged==charged && c.shots[i].variant>=first && c.shots[i].variant<=last) ++n;
  return n;
}
static void weapon_triad_checks(const char *assets,const char *fixture,uint8 *start,
                               uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Triad Thunder source assets load");
  check(MmxWeaponsPose(2,3,51,34)->width>0,"original base-X punch art is extracted");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Triad fixture");if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=3;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);
    check(triad_count(false,0,2)==3 && MmxWeaponsEnergyAmount(2,3)==27*256,
        "three Triad orbs launch for one energy");
    zero_replay(45);
    check(triad_count(false,0,2)==3 && triad_count(false,3,5)==3,"three original connecting arcs appear after orb formation");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-triad.cap":".zero-triad.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(40);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore Triad triangle");zero_replay(40);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"Triad links and beam emission replay exactly");
    check(triad_count(false,6,8)==3,"formation releases three original directional bolts");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-triad-rays.cap":".zero-triad-rays.cap");
    zero_replay(160);check(!extended_shots(false) && !g_ram[0xbdd],"Triad debris and bolts retire without slot leaks");
    frame(SNES_PAD_Y);frame(0);zero_replay(12);
    for(unsigned press=0;press<4;++press) {frame(SNES_PAD_Y);frame(0);}
    zero_replay(68);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned inverted=0;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].variant<3 && (c.shots[i].facing&128)) ++inverted;
    check(inverted==3 && MmxWeaponsEnergyAmount(2,3)==25*256,"four fire edges invert the existing triangle for one more energy");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-triad-inverted.cap":".zero-triad-inverted.cap");
    MmxWeaponsCancelShots(g_ram);zero_replay(2);
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);
    check(!extended_shots(true),"charged Triad requires X1 arm upgrade");
    check(RtlLoadSnapshot(fixture),"restore charged Triad fixture");if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=2;w.weapon=3;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(2);
    check(triad_count(true,0,2)==3 && MmxWeaponsEnergyAmount(2,3)==24*256,
        "charged release creates punch and two delayed waves for three energy");
    int x=g_ram[0xbad]|g_ram[0xbae]<<8;
    for(unsigned i=0;i<35;++i) frame(SNES_PAD_RIGHT|SNES_PAD_B|SNES_PAD_Y);
    c=MmxWeaponsGetCombatState();unsigned body=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged && !c.shots[i].variant) body=i;
    check(body<8 && c.shots[body].muzzle_pose==2 && c.shots[body].pose==34 &&
        (g_ram[0xbad]|g_ram[0xbae]<<8)==x && g_ram[0x1e83]==3 && !(g_ram[0xc2f]&64),
        "source punch pose locks movement, begins native quake, and stops charge audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-triad-punch.cap":".zero-triad-punch.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(58);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged Triad quake");zero_replay(58);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"native quake, body poses and wave release replay exactly");
    c=MmxWeaponsGetCombatState();unsigned moving=0;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged && c.shots[i].variant && c.shots[i].muzzle_pose) ++moving;
    check(moving>0 && triad_count(true,0,0)==1,"waves launch after the sixty-tick quake while body recovery continues");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-triad-waves.cap":".zero-triad-waves.cap");
    zero_replay(180);check(!extended_shots(true) && !g_ram[0xbdd],"charged attack restores input and releases all slots");
    frame(SNES_PAD_RIGHT);frame(SNES_PAD_RIGHT);
    check((g_ram[0xbad]|g_ram[0xbae]<<8)>x,"player can move again after original recovery");
    frame(0);for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_Y|SNES_PAD_B);frame(0);frame(0);
    c=MmxWeaponsGetCombatState();body=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged && !c.shots[i].variant) body=i;
    check(body<8 && !c.shots[body].muzzle_pose,"air release waits for solid ground before punching");
    zero_replay(60);c=MmxWeaponsGetCombatState();
    check(c.shots[body].active && c.shots[body].muzzle_pose>=1,"air release lands and starts the committed punch");
    g_ram[0xbaa]=14;frame(0);
    check(!triad_count(true,0,2),"hurt cancels the punch and unreleased ground waves");
  }
  check(RtlLoadSnapshot(fixture),"restore Triad enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for Triad damage");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=3;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && !c.shots[i].variant) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    c.shots[i].muzzle_pose=1;c.shots[i].radius=30;c.shots[i].vx=c.shots[i].vy=0;slot=i;break;
  }
  check(slot<8,"Triad orb is available for native collision");g_ram[victim+0x27]=32;MmxWeaponsSetCombatState(c);
  zero_replay(1);
  check((g_ram[victim+0x27]&127)==27,"native orb collision preserves five-buster damage ratio");
  puts("MMX TRIAD THUNDER CHECKS PASSED");
}
static void weapon_fang_checks(const char *assets,const char *fixture,uint8 *start,
                              uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Tornado Fang original assets load");
  check(MmxWeaponsPose(2,8,19,65)!=NULL,"charged Fang includes all four original CHR phases");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Fang fixture");if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=8;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {slot=i;break;}
    check(slot<8 && c.shots[slot].vx==512 && MmxWeaponsEnergyAmount(2,8)==27*256,
        "normal drill starts at two pixels per tick and costs one energy");
    zero_replay(20);c=MmxWeaponsGetCombatState();
    check(c.shots[slot].active && c.shots[slot].muzzle_pose==1 && !c.shots[slot].vx,
        "normal drill pauses after eight-tick launch route");
    frame(SNES_PAD_Y);frame(0);zero_replay(15);c=MmxWeaponsGetCombatState();
    unsigned top=8,bottom=8;for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
      if(c.shots[i].variant==1) top=i;if(c.shots[i].variant==2) bottom=i;
    }
    check(top<8 && bottom<8 && c.shots[bottom].y-c.shots[top].y==36*256 &&
        MmxWeaponsEnergyAmount(2,8)==26*256,"second press launches two separated drills for one energy");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(40);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore staged drill salvo");zero_replay(40);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"drill staging and acceleration replay exactly");
    c=MmxWeaponsGetCombatState();check(c.shots[slot].muzzle_pose==2 && c.shots[slot].vx>0,
        "drill accelerates after its original sixty-tick delay");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-fang.cap":".zero-fang.cap");
    zero_replay(180);check(!extended_shots(false) && !g_ram[0xbdd],"normal drills leave the screen and release slots");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);
    check(!extended_shots(true),"Fang charge requires X1 arms");
    check(RtlLoadSnapshot(fixture),"restore charged Fang fixture");if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=2;w.weapon=8;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    c=MmxWeaponsGetCombatState();slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) slot=i;
    check(slot<8 && c.shots[slot].pose>=42 && c.shots[slot].pose<66 &&
        MmxWeaponsEnergyAmount(2,8)==24*256,"full held charge automatically deploys the rotating drill for three energy");
    check(g_ram[0xbf8] && !(g_ram[0xc2f]&64),"held drill retains firing pose without lingering charge audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-fang-held.cap":".zero-fang-held.cap");
    n=RtlSaveSnapshotToMemory(start,cap);for(unsigned i=0;i<20;++i) frame(SNES_PAD_Y|SNES_PAD_RIGHT);
    en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore held Fang");for(unsigned i=0;i<20;++i) frame(SNES_PAD_Y|SNES_PAD_RIGHT);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"held drill follows moving player and replays exactly");
    for(unsigned i=0;i<44;++i) frame(SNES_PAD_Y);
    check(MmxWeaponsEnergyAmount(2,8)==21*256,"sustained drill costs three energy each sixty-four ticks");
    frame(0);zero_replay(2);check(!extended_shots(true) && !(g_ram[0xc2f]&64),"release removes held drill and its charge sound");
  }
  check(RtlLoadSnapshot(fixture),"restore Fang enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for drill contact");
  unsigned fx=(g_ram[0xbad]|g_ram[0xbae]<<8)+64,fy=(g_ram[0xbb0]|g_ram[0xbb1]<<8)-32;
  g_ram[victim+5]=(uint8_t)fx;g_ram[victim+6]=(uint8_t)(fx>>8);
  g_ram[victim+8]=(uint8_t)fy;g_ram[victim+9]=(uint8_t)(fy>>8);
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=8;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    c.shots[i].muzzle_pose=1;c.shots[i].vx=c.shots[i].vy=0;slot=i;break;
  }
  check(slot<8,"drill is available for native contact");g_ram[victim+0x27]=32;MmxWeaponsSetCombatState(c);
  zero_replay(3);c=MmxWeaponsGetCombatState();
  check((g_ram[victim+0x27]&127)==31 && c.shots[slot].muzzle_pose==3 && c.shots[slot].tether_pose==6,
      "real enemy contact applies scaled damage and original impact pause");
  bool debris=false;for(unsigned i=0;i<16;++i) debris|=c.effects[i].active && c.effects[i].group==20;
  check(debris,"native drill contact emits original sixteen-tick impact debris");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".fang-debris.cap");
  size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(8);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore drill impact debris");zero_replay(8);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"drill contact debris replays exactly");
  unsigned d=0x1228+slot*64;
  for(unsigned contact=0;contact<6;++contact) {
    c=MmxWeaponsGetCombatState();c.shots[slot].muzzle_pose=2;c.shots[slot].radius=0;
    MmxWeaponsSetCombatState(c);g_ram[d+1]=6;MmxWeaponsProjectileTick(g_ram,d,1);
  }
  check(!MmxWeaponsGetCombatState().shots[slot].active,"seventh damage contact consumes normal drill durability");
  puts("MMX TORNADO FANG CHECKS PASSED");
}
static void weapon_speed_checks(const char *assets,const char *fixture,uint8 *start,
                               uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Speed Burner original assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Speed Burner fixture");if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=8;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {slot=i;break;}
    check(slot<8 && c.shots[slot].vx==1280 && MmxWeaponsEnergyAmount(1,8)==27*256,
        "normal flame uses original five-pixel speed and one-energy cost");
    zero_replay(15);c=MmxWeaponsGetCombatState();unsigned fire=0;
    for(unsigned i=0;i<8;++i) fire+=c.shots[i].active && c.shots[i].variant==1;
    check(fire>0 && c.shots[slot].muzzle_pose==1,"fireball grows and creates ground flames");
    unsigned sparks=0;for(unsigned i=0;i<16;++i) sparks+=c.effects[i].active;
    check(sparks>0,"original flame sparkles use separate visual slots");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-speed.cap":".zero-speed.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(20);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore growing flame trail");zero_replay(20);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"fireball and ground flames replay exactly");
    zero_replay(180);check(!extended_shots(false) && !g_ram[0xbdd],"normal fire and ground flames expire cleanly");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(5);
    check(!extended_shots(true),"charged Speed Burner requires X1 arms");
    check(RtlLoadSnapshot(fixture),"restore charged dash fixture");if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=1;w.weapon=8;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(5);
    c=MmxWeaponsGetCombatState();slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) {slot=i;break;}
    check(slot<8 && c.shots[slot].group==38 && !c.shots[slot].variant &&
        (int16_t)(g_ram[0xbc2]|g_ram[0xbc3]<<8)==1141 && MmxWeaponsEnergyAmount(1,8)==24*256,
        "charged ground dash uses original speed, effect, and three-energy cost");
    check(g_ram[0xbaa]==0x14 && g_ram[0xbd8]==128 && !(g_ram[0xc2f]&64),"charged dash owns native dash pose and damage guard without charge sound");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-speed-dash.cap":".zero-speed-dash.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(12);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged dash");zero_replay(12);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"charged body dash replays exactly");
    zero_replay(50);check(!extended_shots(true) && !g_ram[0xbd8] && g_ram[0xbaa]!=0x14,"ground dash ends and clears its damage guard");
    check(RtlLoadSnapshot(fixture),"restore airborne dash fixture");if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=1;w.weapon=8;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    for(unsigned i=0;i<10;++i) frame(SNES_PAD_Y|SNES_PAD_B);
    frame(0);zero_replay(3);
    c=MmxWeaponsGetCombatState();slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) {slot=i;break;}
    check(slot<8 && c.shots[slot].variant==1,"air release starts source short dash");
    unsigned y=g_ram[0xbb0]|g_ram[0xbb1]<<8;zero_replay(8);
    check((g_ram[0xbb0]|g_ram[0xbb1]<<8)==y,"air dash holds height while moving horizontally");
    zero_replay(25);check(!extended_shots(true) && !g_ram[0xbd8],"air dash expires and gravity resumes");
  }
  check(RtlLoadSnapshot(fixture),"restore private water-fire fixture");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=8;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
  unsigned px=g_ram[0xbad]|g_ram[0xbae]<<8,py=g_ram[0xbb0]|g_ram[0xbb1]<<8;
  size_t offsets[256];uint8_t properties[256];unsigned changed=0;
  for(int y=(int)py-160;y<=(int)py+16;y+=16) for(int x=(int)px-64;x<=(int)px+300;x+=16) {
    if(x<0 || y<0) continue;
    unsigned screen=g_ram[0xe800+(y>>8)*32+(x>>8)];
    unsigned cell=0x2000+((screen*512+((y&240)<<1)+((x&240)>>3))&65535);
    unsigned address=(g_ram[0xb92]|g_ram[0xb93]<<8|g_ram[0xb94]<<16)+(g_ram[cell]|g_ram[cell+1]<<8);
    size_t offset=((address>>16)&127)*32768+(address&32767);
    if((g_snes->cart->rom[offset]&63)!=0) continue;
    check(changed<256,"private water properties fit fixture storage");
    offsets[changed]=offset;properties[changed++]=g_snes->cart->rom[offset];g_snes->cart->rom[offset]=13;
  }
  frame(SNES_PAD_Y);zero_replay(5);MmxWeaponCombatState c=MmxWeaponsGetCombatState();
  unsigned parent=8,children=0;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) { if(c.shots[i].variant==2) parent=i;children+=c.shots[i].variant==3; }
  check(parent<8 && children==2 && c.shots[parent].pose==6,"water replaces normal fire with original three-bubble formation");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".speed-water.cap");
  size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(12);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore water-bubble orbit");zero_replay(12);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"source water orbits replay exactly");
  for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(5);
  c=MmxWeaponsGetCombatState();unsigned dash=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) dash=i;
  check(dash<8 && c.shots[dash].tether_pose && !g_ram[0xbd8] &&
      !(g_ram[0x1228+dash*64+0x20]|g_ram[0x1228+dash*64+0x21]),"water dash retains movement but loses flames, attack box and damage guard");
  bool bubbles=false;for(unsigned i=0;i<16;++i) bubbles|=c.effects[i].active && c.effects[i].variant==5 &&
      c.effects[i].group==23 && c.effects[i].vy<0;
  check(bubbles,"underwater charged dash emits original accelerating bubble particles");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".speed-dash-bubbles.cap");
  n=RtlSaveSnapshotToMemory(start,cap);zero_replay(10);en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore underwater dash bubbles");zero_replay(10);
  an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"underwater dash bubbles replay exactly");
  for(unsigned i=0;i<changed;++i) g_snes->cart->rom[offsets[i]]=properties[i];
  MmxWeaponsCancelShots(g_ram);
  puts("MMX SPEED BURNER CHECKS PASSED");
}
#include "mmx_weapon_silk_test.inc"
#include "mmx_weapon_chain_test.inc"
static void weapon_magnet_checks(const char *assets,const char *fixture,uint8 *start,
                                uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Magnet Mine and original explosion assets load");
  check(MmxWeaponsPose(1,7,8,8) && memcmp(MmxWeaponsGroupPalette(1,7,8),MmxWeaponsPalette(1,7,false),32),
      "mine blast has its original separate effect palette");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Magnet Mine fixture");if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=7;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {slot=i;break;}
    check(slot<8 && c.shots[slot].vx==512 && MmxWeaponsEnergyAmount(1,7)==27*256,
        "normal mine uses source speed and one-energy cost");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-magnet-normal.cap":".zero-magnet-normal.cap");
    for(unsigned i=0;i<8;++i) frame(SNES_PAD_UP);
    c=MmxWeaponsGetCombatState();check(c.shots[slot].vy==-1024,"up steers mine to source vertical speed cap");
    frame(0);check(MmxWeaponsGetCombatState().shots[slot].vy==-1024,"mine retains vertical momentum after steering release");
    frame(SNES_PAD_Y);frame(0);check(extended_shots(false)==1,"one flying mine at a time matches source limit");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(12);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore steering mine");zero_replay(12);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"steered mine replay is exact");
    check(RtlLoadSnapshot(fixture),"restore planted-mine fixture");if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=1;w.weapon=7;MmxWeaponsSetState(w);frame(SNES_PAD_Y);frame(0);
    c=MmxWeaponsGetCombatState();for(unsigned i=0;i<8;++i) if(c.shots[i].active) {slot=i;break;}
    unsigned phases=0,linked=8;bool blast=false,chain=false;
    for(unsigned i=0;i<165;++i) {
      frame(i<20 ? SNES_PAD_DOWN : 0);c=MmxWeaponsGetCombatState();
      if(linked<8 && !c.shots[slot].active && c.shots[linked].active && c.shots[linked].muzzle_pose==3) chain=true;
      if(c.shots[slot].active) {
        phases|=1u<<c.shots[slot].muzzle_pose;
        if(c.shots[slot].muzzle_pose==3 && !blast) {
          blast=true;zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-magnet-blast.cap":".zero-magnet-blast.cap");
          check(c.shots[slot].group==8,"mine detonates with original shared explosion art");
          frame(SNES_PAD_Y);frame(0);c=MmxWeaponsGetCombatState();
          for(unsigned k=0;k<8;++k) if(k!=slot && c.shots[k].active) {linked=k;break;}
          check(linked<8,"planted mine releases the one-mine firing limit");
          c.shots[linked].x=c.shots[slot].x+16*256;c.shots[linked].y=c.shots[slot].y;
          c.shots[linked].muzzle_pose=2;c.shots[linked].radius=60;c.shots[linked].vx=c.shots[linked].vy=0;
          MmxWeaponsSetCombatState(c);
        }
      }
    }
    check((phases&14)==14 && blast && !g_ram[0xbdd],"mine arms on native terrain, waits, explodes and cleans up");
    check(chain,"blast end detonates the adjacent mine before its timer expires");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(10);
    check(!extended_shots(true),"charged Magnet Mine requires X1 arms");
    MmxWeaponsCancelShots(g_ram);zero_replay(5);g_ram[0x1f99]|=2;
    unsigned energy=MmxWeaponsEnergyAmount(1,7);
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);frame(0);zero_replay(12);
    c=MmxWeaponsGetCombatState();slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) {slot=i;break;}
    check(slot<8 && c.shots[slot].group==19 && c.shots[slot].vx==128 &&
        MmxWeaponsEnergyAmount(1,7)==energy-4*256,"charged mine uses original group, slow speed and three-energy cost");
    unsigned d=0x1228+slot*64,bullet=0x1428;
    unsigned x=(unsigned)(c.shots[slot].x>>8),y=(unsigned)(c.shots[slot].y>>8);
    memset(g_ram+bullet,0,64);g_ram[bullet]=1;g_ram[bullet+0x28]=1;
    g_ram[bullet+5]=(uint8_t)(x+50);g_ram[bullet+6]=(uint8_t)((x+50)>>8);
    g_ram[bullet+8]=(uint8_t)(y+40);g_ram[bullet+9]=(uint8_t)((y+40)>>8);
    MmxWeaponsProjectileTick(g_ram,d,1);
    check((unsigned)(g_ram[bullet+5]|g_ram[bullet+6]<<8)==x+45 &&
          (unsigned)(g_ram[bullet+8]|g_ram[bullet+9]<<8)==y+37,"charged mine pulls enemy shots by source 5/3-pixel steps");
    g_ram[bullet+0x28]=0;MmxWeaponsProjectileTick(g_ram,d,1);
    check((unsigned)(g_ram[bullet+5]|g_ram[bullet+6]<<8)==x+45,"immune enemy shots are not made magnetic");
    for(unsigned absorbed=0;absorbed<32;++absorbed) {
      memset(g_ram+bullet,0,64);g_ram[bullet]=1;g_ram[bullet+0x28]=1;
      memcpy(g_ram+bullet+5,g_ram+d+5,2);memcpy(g_ram+bullet+8,g_ram+d+8,2);
      memcpy(g_ram+bullet+0x20,g_ram+d+0x20,2);
      MmxWeaponsProjectileTick(g_ram,d,1);if(g_ram[bullet]) MmxWeaponsProjectileTick(g_ram,d,1);
      check(!g_ram[bullet],"charged mine absorbs contacting enemy shot");
      if(absorbed==15) check(MmxWeaponsGetCombatState().shots[slot].variant==1,"sixteen absorbed shots grow the middle mine");
    }
    c=MmxWeaponsGetCombatState();check(c.shots[slot].variant==2 && c.shots[slot].radius==32,
        "thirty-two absorbed shots grow original largest mine and hitbox");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-magnet-charged.cap":".zero-magnet-charged.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(20);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore grown charged mine");zero_replay(20);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"charged mine size, animation and motion replay exactly");
    for(unsigned i=0;i<80;++i) frame(SNES_PAD_UP);
    check(!extended_shots(true) && !g_ram[0xbdd] && !(g_ram[0xc2f]&64),"charged mine leaves the screen without audio or slot leaks");
  }
  check(RtlLoadSnapshot(fixture),"restore mine enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for mine collision");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=7;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    c.shots[i].vx=c.shots[i].vy=0;slot=i;break;
  }
  check(slot<8,"mine is available for native contact");g_ram[victim+0x27]=32;MmxWeaponsSetCombatState(c);
  zero_replay(3);check((g_ram[victim+0x27]&127)<=30,"native mine impact deals source-scaled damage");
  check(MmxWeaponsGetCombatState().shots[slot].muzzle_pose==3,"native enemy contact starts mine explosion");
  puts("MMX MAGNET MINE CHECKS PASSED");
}
static void weapon_bubble_checks(const char *assets,const char *fixture,uint8 *start,
                                uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Bubble Splash original assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Bubble Splash fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=2;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);
    check(extended_shots(false)==1 && MmxWeaponsEnergyAmount(1,2)==28*256-32,
        "tap creates one original bubble for one-eighth energy");
    for(unsigned i=0;i<18;++i) frame(SNES_PAD_Y);
    check(extended_shots(false)==7 && MmxWeaponsEnergyAmount(1,2)==28*256-7*32,
        "held fire fills source seven-bubble limit with fractional costs");
    frame(0);zero_replay(15);MmxWeaponCombatState c=MmxWeaponsGetCombatState();
    unsigned rising=0;
    for(unsigned i=0;i<8;++i) rising+=c.shots[i].active && c.shots[i].vy<0;
    check(rising>0,"normal bubbles grow and accelerate upward");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-bubble.cap":".zero-bubble.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(40);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore rising bubbles");zero_replay(40);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"bubble motion and popping replay exactly");
    zero_replay(220);check(!g_ram[0xbdd] && !extended_shots(false),"released bubbles pop and free every slot");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(20);check(!extended_shots(true),"Bubble Splash charged form requires X1 arms");
    MmxWeaponsCancelShots(g_ram);zero_replay(10);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<120;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(5);check(!extended_shots(true),"intermediate buster tier remains a normal special-weapon release");
    MmxWeaponsCancelShots(g_ram);zero_replay(10);
    for(unsigned i=0;i<250;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(80);c=MmxWeaponsGetCombatState();
    unsigned controller=0,children=0,moving=0;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) {
      controller+=!c.shots[i].variant;children+=c.shots[i].variant!=0;
      moving+=c.shots[i].variant!=0 && c.shots[i].muzzle_pose==1;
    }
    check(controller==1 && children==7 && moving,"charged bubbles form the original renewing seven-bubble cloud");
    check(!(g_ram[0xc2f]&64),"charged cloud stops charge audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-bubble-charged.cap":".zero-bubble-charged.cap");
    n=RtlSaveSnapshotToMemory(start,cap);unsigned energy=MmxWeaponsEnergyAmount(1,2);
    zero_replay(110);en=RtlSaveSnapshotToMemory(expected,cap);
    check(MmxWeaponsEnergyAmount(1,2)<energy,"renewing charged bubbles consume fractional energy");
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged cloud");zero_replay(110);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"charged routes, renewal and drain replay exactly");
    /* Resolve a real X1 water property in the private runtime, then check
     * the source's three-pixel lift while the native player is airborne. */
    unsigned px=g_ram[0xbad]|g_ram[0xbae]<<8,py=g_ram[0xbb0]|g_ram[0xbb1]<<8;
    unsigned screen=g_ram[0xe800+(py>>8)*32+(px>>8)];
    unsigned cell=0x2000+((screen*512+((py&240)<<1)+((px&240)>>3))&65535);
    unsigned address=(g_ram[0xb92]|g_ram[0xb93]<<8|g_ram[0xb94]<<16)+(g_ram[cell]|g_ram[cell+1]<<8);
    size_t offset=((address>>16)&127)*32768+(address&32767);
    uint8_t property=g_snes->cart->rom[offset];g_snes->cart->rom[offset]=13;g_ram[0xbaa]=6;
    c=MmxWeaponsGetCombatState();
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged && !c.shots[i].variant)
      MmxWeaponsProjectileTick(g_ram,0x1228+i*64,1);
    check((unsigned)(g_ram[0xbb0]|g_ram[0xbb1]<<8)==py-3,"charged bubbles supply the source underwater lift");
    g_snes->cart->rom[offset]=property;
    check(RtlLoadSnapshotFromMemory(start,n),"restore after private water check");
    frame(SNES_PAD_R);zero_replay(3);
    check(MmxWeaponsGetState().weapon==3 && !g_ram[0xbdd],"shoulder cycle cancels the persistent bubble cloud cleanly");
    check(RtlLoadSnapshotFromMemory(start,n),"restore for empty-energy cleanup");
    w=MmxWeaponsGetState();w.energy[1]=0;w.fraction[1]=31;MmxWeaponsSetState(w);
    zero_replay(220);check(!g_ram[0xbdd] && !extended_shots(true),"insufficient energy ends cloud and all bubbles");
  }
  check(RtlLoadSnapshot(fixture),"restore bubble enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for bubble contact");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=2;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    c.shots[i].vx=c.shots[i].vy=0;slot=i;break;
  }
  check(slot<8,"bubble available for native collision");g_ram[victim+0x27]=32;MmxWeaponsSetCombatState(c);
  zero_replay(3);check((g_ram[victim+0x27]&127)==31,"native bubble hit applies rounded source two-thirds damage");
  check(MmxWeaponsGetCombatState().shots[slot].muzzle_pose==3,"enemy contact starts original bubble pop");
  puts("MMX BUBBLE SPLASH CHECKS PASSED");
}
static void weapon_frost_checks(const char *assets,const char *fixture,uint8 *start,
                               uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Frost Shield source assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Frost Shield fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=7;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(80);unsigned slot=frost_slot(false);
    check(slot<8 && MmxWeaponsEnergyAmount(2,7)==27*256,"normal Frost Shield forms and costs one energy");
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();
    check(c.shots[slot].muzzle_pose==1 && c.shots[slot].vx>16,"source formation becomes an accelerating ice missile");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-frost.cap":".zero-frost.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(25);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore ice missile");zero_replay(25);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"ice flight and original animation replay exactly");
    check(RtlLoadSnapshotFromMemory(start,n),"restore for ice impact");
    g_ram[0x1228+slot*64+1]=6; /* Native lethal-contact result, X3 $84:CF60. */
    unsigned phases=0;
    for(unsigned i=0;i<150;++i) {
      frame(0);c=MmxWeaponsGetCombatState();
      if(c.shots[slot].active) phases|=1u<<c.shots[slot].muzzle_pose;
    }
    check((phases&60)==60,"ice core falls onto native terrain, lands, grows and plants a spike");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-frost-spike.cap":".zero-frost-spike.cap");
    zero_replay(300);check(!extended_shots(false) && !g_ram[0xbdd],"planted ice expires and frees its slot");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(12);check(!extended_shots(true),"charged Frost Shield requires X1 arms");
    MmxWeaponsCancelShots(g_ram);zero_replay(5);g_ram[0x1f99]|=2;
    unsigned energy=MmxWeaponsEnergyAmount(2,7);
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(50);slot=frost_slot(true);
    check(slot<8 && MmxWeaponsEnergyAmount(2,7)==energy-4*256,"charged shield costs three after its initial normal shot");
    c=MmxWeaponsGetCombatState();
    check(c.shots[slot].muzzle_pose==2 && c.shots[slot].pose==53 && g_ram[0xbf8]<128,
        "charged ice uses original full shield and sustained arm pose");
    check(!(g_ram[0xc2f]&64),"charged shield clears charge sound");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-frost-shield.cap":".zero-frost-shield.cap");
    n=RtlSaveSnapshotToMemory(start,cap);
    unsigned ice=0x1228+slot*64,bullet=0x1428;
    memset(g_ram+bullet,0,64);g_ram[bullet]=1;g_ram[bullet+1]=2;g_ram[bullet+0x28]=1;
    memcpy(g_ram+bullet+5,g_ram+ice+5,2);memcpy(g_ram+bullet+8,g_ram+ice+8,2);
    memcpy(g_ram+bullet+0x20,g_ram+ice+0x20,2);
    MmxWeaponsProjectileTick(g_ram,ice,1);
    check(!g_ram[bullet],"charged shield intercepts a destructible enemy projectile");
    g_ram[bullet]=1;g_ram[bullet+0x28]=0;
    MmxWeaponsProjectileTick(g_ram,ice,1);
    check(g_ram[bullet],"charged shield leaves immune enemy projectiles intact");
    g_ram[ice+1]=6;MmxWeaponsProjectileTick(g_ram,ice,1);
    check(MmxWeaponsGetCombatState().shots[slot].muzzle_pose==2,"charged shield survives its native lethal-contact result");
    g_ram[ice+1]=8;MmxWeaponsProjectileTick(g_ram,ice,1);
    check(MmxWeaponsGetCombatState().shots[slot].muzzle_pose==4,"charged shield shatters after a surviving enemy contact");
    check(RtlLoadSnapshotFromMemory(start,n),"restore shield after contact checks");
    zero_replay(375);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore arm shield");zero_replay(375);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"shield expiry and released ice chunk replay exactly");
    c=MmxWeaponsGetCombatState();check(c.shots[slot].active && c.shots[slot].muzzle_pose==3,
        "shield releases the original moving ice chunk after its 360-tick hold");
    zero_replay(250);check(!extended_shots(true) && !g_ram[0xbdd] && !g_ram[0xc25],"charged ice cleans up all native counters");
  }
  /* Controlled water tile in the private runtime. Exercise the real player
   * terrain hook and jumping, without depending on an owner's stage save. */
  check(RtlLoadSnapshot(fixture),"restore private water-platform fixture");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=7;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
  unsigned px=g_ram[0xbad]|g_ram[0xbae]<<8,py=g_ram[0xbb0]|g_ram[0xbb1]<<8;
  size_t offsets[256];uint8_t properties[256];unsigned changed=0;
  for(int y=(int)py-160;y<=(int)py+16;y+=16) for(int x=(int)px-64;x<=(int)px+96;x+=16) {
    if(x<0 || y<0) continue;
    unsigned screen=g_ram[0xe800+(y>>8)*32+(x>>8)];
    unsigned cell=0x2000+((screen*512+((y&240)<<1)+((x&240)>>3))&65535);
    unsigned address=(g_ram[0xb92]|g_ram[0xb93]<<8|g_ram[0xb94]<<16)+(g_ram[cell]|g_ram[cell+1]<<8);
    size_t offset=((address>>16)&127)*32768+(address&32767);
    if((g_snes->cart->rom[offset]&63)!=0) continue;
    check(changed<256,"private water properties fit fixture storage");
    offsets[changed]=offset;properties[changed++]=g_snes->cart->rom[offset];g_snes->cart->rom[offset]=13;
  }
  for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
  frame(0);zero_replay(24);unsigned slot=frost_slot(true);
  check(slot<8,"underwater charged ice is present");MmxWeaponCombatState c=MmxWeaponsGetCombatState();
  check(c.shots[slot].muzzle_pose==5 && c.shots[slot].variant==1,"water produces the rising original ice platform");
  int platform_y=c.shots[slot].y;zero_replay(8);c=MmxWeaponsGetCombatState();
  check(c.shots[slot].y<platform_y,"underwater ice rises toward the surface");
  px=(unsigned)(c.shots[slot].x>>8);py=(unsigned)((c.shots[slot].y>>8)-43);
  g_ram[0xbad]=(uint8_t)px;g_ram[0xbae]=(uint8_t)(px>>8);
  g_ram[0xbb0]=g_ram[0xbcc]=(uint8_t)py;g_ram[0xbb1]=g_ram[0xbcd]=(uint8_t)(py>>8);
  g_ram[0xbc4]=g_ram[0xbc5]=0;g_ram[0xbaa]=6;g_ram[0xbab]=0;
  zero_replay(12);c=MmxWeaponsGetCombatState();
  check(c.shots[slot].active && c.shots[slot].tether_pose,"native terrain hook lands player on moving ice");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".frost-platform.cap");
  size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(8);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore moving ice rider");zero_replay(8);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"moving platform and rider replay exactly");
  frame(SNES_PAD_B);c=MmxWeaponsGetCombatState();
  check(!c.shots[slot].tether_pose && (int16_t)(g_ram[0xbc4]|g_ram[0xbc5]<<8)>0,"player jumps freely off the ice platform");
  for(unsigned i=0;i<changed;++i) g_snes->cart->rom[offsets[i]]=properties[i];
  puts("MMX FROST SHIELD CHECKS PASSED");
}
static void weapon_wheel_checks(const char *assets,const char *fixture,uint8 *start,
                                uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Spin Wheel original assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Spin Wheel fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=4;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(5);
    check(extended_shots(false)==1 && MmxWeaponsEnergyAmount(1,4)==27*256,
        "normal Spin Wheel forms for one energy unit");
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    unsigned phases=0;
    for(unsigned i=0;i<90;++i) {
      frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();
      for(unsigned j=0;j<8;++j) if(c.shots[j].active) phases|=1u<<c.shots[j].muzzle_pose;
      if(i==66) zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-wheel.cap":".zero-wheel.cap");
    }
    check((phases&15)==15,"normal wheel forms, falls, waits on real terrain and rolls");
    size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore Spin Wheel formation");
    zero_replay(90);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"Spin Wheel ground delay and rolling replay exactly");
    zero_replay(400);
    check(!extended_shots(false) && !g_ram[0xbdd],"normal wheel releases native slot");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(20);
    check(!extended_shots(true),"charged Spin Wheel remains gated by X1 arms");
    MmxWeaponsCancelShots(g_ram);zero_replay(5);g_ram[0x1f99]|=2;
    unsigned energy=MmxWeaponsEnergyAmount(1,4);
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(5);
    check(extended_shots(true)==1,"charged wheel begins with original formation");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(14);
    check(extended_shots(true)==8 && MmxWeaponsEnergyAmount(1,4)==energy-4*256,
        "charged wheel creates all eight directions for three energy after the initial normal shot");
    check(!(g_ram[0xc2f]&64),"charged Spin Wheel stops charging audio");
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned directions=0;
    for(unsigned j=0;j<8;++j) if(c.shots[j].active) directions|=1u<<c.shots[j].variant;
    check(directions==255,"charged wheel retains every original radial direction");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-wheel-charged.cap":".zero-wheel-charged.cap");
    en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged wheel before split");
    zero_replay(14);an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"charged wheel split and trajectories replay exactly");
    zero_replay(160);
    check(!extended_shots(true) && !g_ram[0xbdd] && !g_ram[0xc25],"charged wheel releases slots and native charge count");
    w=MmxWeaponsGetState();w.energy[3]=0;w.fraction[3]=255;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(35);
    check(!extended_shots(false) && MmxWeaponsEnergyAmount(1,4)==255,"Spin Wheel refuses insufficient energy without underflow");
  }
  check(RtlLoadSnapshot(fixture),"restore Spin Wheel enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for Spin Wheel contact");
  size_t encounter=RtlSaveSnapshotToMemory(start,cap);
  for(unsigned falling=0;falling<2;++falling) {
    check(RtlLoadSnapshotFromMemory(start,encounter),"restore wheel contact setup");
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=4;MmxWeaponsSetState(w);
    frame(0);frame(SNES_PAD_Y);zero_replay(falling ? 40 : 5);
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned shot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
      c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
      c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
      shot=i;break;
    }
    check(shot<8,"Spin Wheel available for native enemy contact");
    if(falling) {c.shots[shot].muzzle_pose=1;c.shots[shot].vy=768;}
    else check(!c.shots[shot].muzzle_pose,"contact regression starts while Spin Wheel is still forming");
    g_ram[victim+0x27]=32; /* Keep the target alive through the stronger wheel. */
    MmxWeaponsSetCombatState(c);unsigned hp=g_ram[victim+0x27]&127;zero_replay(3);
    c=MmxWeaponsGetCombatState();
    check((g_ram[victim+0x27]&127)+8==hp && c.shots[shot].active && c.shots[shot].tether_pose,
        "Spin Wheel deals normalized source damage and pauses on contact without disappearing");
    check(!c.shots[shot].vy,"wheel clears falling velocity on native enemy contact");
    int32_t contact_x=c.shots[shot].x,contact_y=c.shots[shot].y;
    unsigned hits=1,last_hp=g_ram[victim+0x27]&127;
    bool stayed=true;
    for(unsigned i=0;i<40 && g_ram[victim] && g_ram[victim+1]!=4;++i) {
      /* Keep this mobile highway enemy in range. Its original AI, collision,
       * hit reaction, damage and death still execute normally. */
      g_ram[victim+5]=(uint8)(contact_x>>8);g_ram[victim+6]=(uint8)(contact_x>>16);
      g_ram[victim+8]=(uint8)(contact_y>>8);g_ram[victim+9]=(uint8)(contact_y>>16);
      frame(0);c=MmxWeaponsGetCombatState();
      unsigned now=g_ram[victim+0x27]&127;
      if(now<last_hp) {++hits;last_hp=now;}
      stayed&=c.shots[shot].active && c.shots[shot].x==contact_x && c.shots[shot].y==contact_y;
    }
    check(stayed,"wheel hangs at the contact position through repeated native enemy hits");
    check(hits==4 && (!g_ram[victim] || g_ram[victim+1]==4),
        "wheel repeatedly damages the same native enemy through its native death state");
    zero_replay(45);c=MmxWeaponsGetCombatState();
    check(c.shots[shot].active && c.shots[shot].y!=contact_y,
        "wheel drops and resumes travel after its target dies");
  }
  puts("MMX SPIN WHEEL CHECKS PASSED");
}
static void weapon_sonic_checks(const char *assets, const char *fixture, uint8 *start,
                                uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets),"Sonic Slicer normal and charged assets load");
  check(MmxWeaponsPose(1,5,135,11)!=NULL,"charged Sonic Slicer uses original separate sprite group");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Sonic Slicer fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=5;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(8);
    check(extended_shots(false)==1 && MmxWeaponsEnergyAmount(1,5)==27*256+128,
          "normal Sonic Slicer forms at muzzle for half an energy unit");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(25);
    check(extended_shots(false)==2,"normal Sonic Slicer launches two blades after forming");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-sonic.cap":".zero-sonic.cap");
    size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore Sonic Slicer before split");
    zero_replay(25);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"Sonic Slicer split and arcs replay exactly");
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned shot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {shot=i;break;}
    check(shot<8,"Sonic blade available for native terrain collision");
    c.shots[shot].x=(g_ram[0xbad]|g_ram[0xbae]<<8)*256;
    c.shots[shot].y=(g_ram[0xbb0]|g_ram[0xbb1]<<8)*256;
    c.shots[shot].vx=0;c.shots[shot].vy=768;MmxWeaponsSetCombatState(c);
    zero_replay(12);c=MmxWeaponsGetCombatState();
    check(c.shots[shot].active && c.shots[shot].radius==1 && c.shots[shot].vy<0,
          "normal Sonic Slicer reflects upward from the native highway floor");
    c.shots[shot].y=(g_ram[0xbb0]|g_ram[0xbb1]<<8)*256;
    c.shots[shot].vy=768;c.shots[shot].radius=2;MmxWeaponsSetCombatState(c);
    zero_replay(12);
    check(!MmxWeaponsGetCombatState().shots[shot].active,"third vertical ricochet retires normal Sonic Slicer");
    zero_replay(180);
    check(!extended_shots(false) && !g_ram[0xbdd],"Sonic normal blades release native slots");
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(15);
    check(!extended_shots(true),"Sonic charged attack remains locked without X1 arms");
    zero_replay(180);g_ram[0x1f99]|=2;
    unsigned energy=MmxWeaponsEnergyAmount(1,5);
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(19);
    check(extended_shots(true)==5 && MmxWeaponsEnergyAmount(1,5)==energy-0x280,
          "charged Sonic Slicer launches five blades for two energy after the initial half-unit shot");
    check(!(g_ram[0xc2f]&64),"charged Sonic release stops charging audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-sonic-charged.cap":".zero-sonic-charged.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(29);
    c=MmxWeaponsGetCombatState();unsigned falling=0;
    for(unsigned i=0;i<8;++i) falling+=c.shots[i].active && c.shots[i].charged &&
      c.shots[i].muzzle_pose==2 && !c.shots[i].vx && c.shots[i].vy>0;
    check(falling==5,"all five charged blades stop spreading at their apex and fall");
    en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged Sonic ascent");
    zero_replay(29);an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"five charged Sonic trajectories replay exactly across apex");
    zero_replay(120);
    check(!extended_shots(true) && !g_ram[0xbdd] && !g_ram[0xc25],"charged Sonic blades retire without leaking native counts");
    w=MmxWeaponsGetState();w.energy[4]=0;w.fraction[4]=127;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(30);
    check(!extended_shots(false) && MmxWeaponsEnergyAmount(1,5)==127,
          "insufficient half-unit energy prevents Sonic fire without underflow");
  }
  check(RtlLoadSnapshot(fixture),"restore Sonic native-enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for Sonic collision");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=1;w.weapon=5;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);zero_replay(30);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned shot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256-c.shots[i].vx;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256-c.shots[i].vy;
    shot=i;break;
  }
  check(shot<8,"Sonic blade available for native enemy contact");
  MmxWeaponsSetCombatState(c);unsigned hp=g_ram[victim+0x27]&127;zero_replay(3);
  check((g_ram[victim+0x27]&127)+1==hp,"Sonic Slicer deals ordinary native enemy damage");
  zero_replay(30);check(!MmxWeaponsGetCombatState().shots[shot].active,"Sonic impact retires without repeated hits");
  puts("MMX SONIC SLICER CHECKS PASSED");
}
static unsigned ray_births(void) {
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned count=0;
  for(unsigned i=0;i<8;++i) count+=c.shots[i].active && c.shots[i].weapon==5 &&
    c.shots[i].variant==2 && c.shots[i].born==c.tick;
  return count;
}
static void weapon_ray_checks(const char *assets, const char *fixture, uint8 *start,
                              uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets),"Ray Splasher original assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Ray Splasher fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=5;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);unsigned rays=0;
    for(unsigned i=0;i<65;++i) {
      frame(0);rays+=ray_births();
      if(i==19 || i==45) {
        check(g_ram[0xbf8]<128 && (g_ram[0xbbf]&127)!=0,
            "Ray firing pose remains active through the complete source burst");
        if(!character) check(MmxZeroGetState().anim_pose==50,
            "Zero retains his original extended-arm firing pose during the late burst");
        if(i==45) zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-ray.cap":".zero-ray.cap");
      }
    }
    check(rays==7 && MmxWeaponsEnergyAmount(2,5)==27*256,"normal Ray Splasher emits seven spread rays for one energy");
    check(g_ram[0xbf8]==255 && !(g_ram[0xbbf]&127),"Ray returns to idle after the source burst ends");
    zero_replay(40);check(!extended_shots(false) && !g_ram[0xbdd] && !g_ram[0xc25],"ray burst releases slots without leaking native charged-beam count");
    g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(10);
    check(extended_shots(true)==1 && MmxWeaponsEnergyAmount(2,5)==23*256+128,
          "charged Ray Splasher deploys its turret for exactly two and a half energy");
    check(!(g_ram[0xc2f]&64),"charged Ray Splasher stops charge audio");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(95);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-ray-turret.cap":".zero-ray-turret.cap");
    check(RtlLoadSnapshotFromMemory(start,n),"restore Ray turret deployment");
    zero_replay(95);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"turret rise and radial firing replay exactly");
    check(RtlLoadSnapshotFromMemory(start,n),"restore for turret lifetime check");
    rays=0;bool left=false,right=false,up=false,down=false;
    for(unsigned i=0;i<300;++i) {
      frame(0);rays+=ray_births();MmxWeaponCombatState c=MmxWeaponsGetCombatState();
      for(unsigned j=0;j<8;++j) if(c.shots[j].active && c.shots[j].variant==2) {
        left|=c.shots[j].vx<0;right|=c.shots[j].vx>0;up|=c.shots[j].vy<0;down|=c.shots[j].vy>0;
      }
    }
    check(rays==22 && left && right && up && down,"original Ray turret cycles radial directions for twenty-two shots");
    check(!extended_shots(true) && !g_ram[0xbdd] && !g_ram[0xc25],"turret and ray trails retire cleanly");
    check(MmxWeaponsEnergyAmount(2,5)==23*256+128,"turret child rays do not consume extra energy");
  }
  check(RtlLoadSnapshot(fixture),"restore Ray native-enemy encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for Ray Splasher collision");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=5;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);zero_replay(9);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned projectile=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].variant==2) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256-c.shots[i].vx;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256-c.shots[i].vy;
    projectile=i;break;
  }
  check(projectile<8,"Ray projectile available for native collision");
  g_ram[victim+0x27]=32;
  MmxWeaponsSetCombatState(c);unsigned hp=g_ram[victim+0x27]&127;zero_replay(3);
  check((g_ram[victim+0x27]&127)+2==hp,"Ray Splasher deals normalized source damage to a native enemy");
  zero_replay(3);check(!MmxWeaponsGetCombatState().shots[projectile].active,"Ray hit drains original trail and releases its slot");
  puts("MMX RAY SPLASHER CHECKS PASSED");
}
static void weapon_acid_checks(const char *assets, const char *fixture, uint8 *start,
                               uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets),"Acid Burst original assets load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Acid Burst fixture");
    if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=1;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);zero_replay(8);
    check(extended_shots(false)==1 && MmxWeaponsGetState().energy[8]==27,
          "normal Acid Burst fires and spends one energy");
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();int shot_y=0;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) shot_y=c.shots[i].y>>8;
    check(shot_y < (g_ram[0xbb0]|g_ram[0xbb1]<<8),"acid lobs upward from character muzzle");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-acid.cap":".zero-acid.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(65);
    size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore acid before terrain contact");
    zero_replay(65);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"acid terrain contact and droplets replay exactly");
    check(acid_variants(128,false)==1 && acid_variants(2,false)==4,
          "acid splashes against native highway floor and throws four droplets");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-acid-splash.cap":".zero-acid-splash.cap");
    zero_replay(170);
    check(!extended_shots(false) && !g_ram[0xbdd],"acid and droplets release all native slots");
    g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(25);
    check(extended_shots(true)==2 && MmxWeaponsGetState().energy[8]==24,
          "charged Acid Burst releases two growing blobs and costs two energy");
    check(!(g_ram[0xc2f]&64),"charged acid release stops charging audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-acid-charged.cap":".zero-acid-charged.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(35);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged acid before bounce");
    zero_replay(35);an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"charged acid bounce and splashes replay exactly");
    check(acid_variants(128,true)>=1 && extended_shots(true)>=3,
          "charged acid bounces from native floor and leaves damaging splashes");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-acid-bounce.cap":".zero-acid-bounce.cap");
    zero_replay(330);
    check(!extended_shots(true) && !g_ram[0xbdd],"charged acid bounce budget retires the attack");
  }
  check(RtlLoadSnapshot(fixture),"restore acid native-enemy encounter");
  unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64)
      if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"native enemy available for Acid Burst collision");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=1;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned projectile=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    c.shots[i].vx=c.shots[i].vy=0;projectile=i;break;
  }
  check(projectile<8,"acid actor available for native collision");
  g_ram[victim+0x27]=32;
  MmxWeaponsSetCombatState(c);unsigned hp=g_ram[victim+0x27]&127;zero_replay(3);
  check((g_ram[victim+0x27]&127)+3==hp,"Acid Burst deals three buster hits of damage to a real enemy");
  zero_replay(35);
  check(!MmxWeaponsGetCombatState().shots[projectile].active,"acid enemy impact retires without creating floor droplets");
  puts("MMX ACID BURST CHECKS PASSED");
}
static void weapon_combat_checks(const char *assets, const char *fixture, uint8 *start,
                                 uint8 *expected, uint8 *actual, size_t cap) {
  check(MmxWeaponsLoad(assets),"original weapon animations load");
  for (unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore blade fixture");
    if (character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState(); w.page=2; w.weapon=4; MmxWeaponsSetState(w);
    frame(SNES_PAD_Y); zero_replay(6);
    check(extended_shots(false)==2 && MmxWeaponsGetState().energy[11]==27,"normal blade creates twin projectiles and spends one energy");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-blade.cap":".zero-blade.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    zero_replay(12); size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore active twin blades");
    zero_replay(12); size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"active blade save and replay");
    MmxWeaponsCancelShots(g_ram); zero_replay(30);
    for (unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0); zero_replay(6);
    check(!extended_shots(true),"extended charging requires actual X1 arm upgrade");
    MmxWeaponsCancelShots(g_ram); zero_replay(30); g_ram[0x1f99]|=2;
    for (unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0); zero_replay(25);
    check(extended_shots(true)==1,"arms-upgraded release creates charged blade");
    check(g_ram[0xbf8]!=0,"attached Blade retains the native firing overlay");
    check(MmxWeaponsGetState().energy[11]==22 && !(g_ram[0xc2f]&64),"charged blade costs three energy and stops charge audio");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-blade-charged.cap":".zero-blade-charged.cap");
    MmxWeaponCombatState blade=MmxWeaponsGetCombatState();unsigned charged_slot=8;
    for(unsigned i=0;i<8;++i) if(blade.shots[i].active && blade.shots[i].charged) {charged_slot=i;break;}
    check(charged_slot<8,"charged Blade controller is present");
    n=RtlSaveSnapshotToMemory(start,cap);
    zero_replay(140-blade.shots[charged_slot].age);
    blade=MmxWeaponsGetCombatState();
    check(blade.shots[charged_slot].radius==80,"source charged Blade remains extended through local tick 140");
    zero_replay(10);blade=MmxWeaponsGetCombatState();
    check(blade.shots[charged_slot].radius==48,"source timeout retracts to 48 pixels by local tick 150");
    zero_replay(16);check(!extended_shots(true),"source timed retraction releases the charged Blade");
    check(RtlLoadSnapshotFromMemory(start,n),"restore held Blade for control checks");
    frame(SNES_PAD_Y);zero_replay(4);blade=MmxWeaponsGetCombatState();
    check(blade.shots[charged_slot].variant==5 && extended_shots(true)==1,
          "fire does not rotate the original charged Blade or create another shot");
    frame(SNES_PAD_UP); zero_replay(10);
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-blade-turn.cap":".zero-blade-turn.cap");
    n=RtlSaveSnapshotToMemory(start,cap); zero_replay(30); en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore rotating charged blade");
    zero_replay(30); an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"rotating blade save and replay");
    zero_replay(80); check(!extended_shots(true) && !g_ram[0xc25],"charged blade retracts without leaking native charged-beam count");
  }
  check(RtlLoadSnapshot(fixture),"restore native enemy encounter");
  unsigned victim=0;
  for (unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for (unsigned d=0xe68;d<0x1228;d+=64)
      if (g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) { victim=d; break; }
  }
  check(victim!=0,"native highway enemy supplies collision target");
  uint8_t blade_enemy[64];memcpy(blade_enemy,g_ram+victim,64);
  MmxWeaponsState w=MmxWeaponsGetState(); w.page=2;w.weapon=4;MmxWeaponsSetState(w);
  frame(0); frame(SNES_PAD_Y); frame(0);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState(); unsigned projectile=0;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
    c.shots[i].x=(g_ram[victim+5]|g_ram[victim+6]<<8)*256;
    c.shots[i].y=(g_ram[victim+8]|g_ram[victim+9]<<8)*256;
    projectile=0x1228+i*64; break;
  }
  check(projectile!=0,"blade actor available for native collision");
  g_ram[victim+0x27]=32;
  MmxWeaponsSetCombatState(c); unsigned hp=g_ram[victim+0x27]&127;
  zero_replay(3);
  check((g_ram[victim+0x27]&127)+3==hp,"blade uses three buster hits of damage against a real enemy");
  zero_replay(30);
  check(!MmxWeaponsGetCombatState().shots[(projectile-0x1228)/64].active,"native enemy hit plays original impact animation and retires blade");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore charged Blade contact fixture");
    if(character) zero_health_swap();
    w=MmxWeaponsGetState();w.page=2;w.weapon=4;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
    for(unsigned i=0;i<205;++i) frame(SNES_PAD_Y);
    frame(0);zero_replay(30);c=MmxWeaponsGetCombatState();unsigned slot=8;
    for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].charged) {slot=i;break;}
    check(slot<8,"charged Blade available for surviving-enemy contact");
    memcpy(g_ram+victim,blade_enemy,64);g_ram[victim+0x27]=64;g_ram[victim+0x30]=0;
    unsigned x=(unsigned)(c.shots[slot].x>>8),y=(unsigned)(c.shots[slot].y>>8);
    g_ram[victim+5]=(uint8_t)x;g_ram[victim+6]=(uint8_t)(x>>8);
    g_ram[victim+8]=(uint8_t)y;g_ram[victim+9]=(uint8_t)(y>>8);
    zero_replay(3);c=MmxWeaponsGetCombatState();
    check((g_ram[victim+0x27]&127)==54,"charged Blade deals ten buster hits once to a surviving enemy");
    check(c.shots[slot].active && c.shots[slot].variant==4 && !c.shots[slot].muzzle_pose && !c.shots[slot].tether_pose,
          "source surviving hit throws charged Blade away and removes the tether");
    check(c.shots[slot].vy==-2048 && c.shots[slot].vx==-(c.shots[slot].facing?1:-1)*2048-224,
          "charged Blade recoil matches the original contact-state velocities");
    check(!(g_ram[0x1228+slot*64+0x20]|g_ram[0x1228+slot*64+0x21]),"recoiling Blade no longer has a damage box");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-blade-recoil.cap":".zero-blade-recoil.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    zero_replay(20);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check((g_ram[victim+0x27]&127)==54,"charged Blade does not apply the old periodic repeat hit");
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged Blade recoil");
    zero_replay(20);size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"charged Blade contact recoil replays exactly");
  }
  puts("MMX EXTENDED WEAPON COMBAT CHECKS PASSED");
}
static void weapon_damage_checks(const char *assets,const char *fixture,uint8 *start,size_t cap) {
  check(MmxWeaponsLoad(assets) && RtlLoadSnapshot(fixture),"restore source damage fixture");
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=5;MmxWeaponsSetState(w);
  frame(SNES_PAD_Y);zero_replay(9);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=8;
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].variant==2) {slot=i;break;}
  check(slot<8,"original Ray projectile available for damage scaling");
  unsigned d=0x1228+slot*64,enemy=0xe68;
  g_ram[enemy]=1;g_ram[enemy+1]=2;g_ram[enemy+10]=0x1f;
  g_ram[enemy+0x27]=32;g_ram[enemy+0x28]=3;
  check(MmxWeaponsDamage(g_ram,enemy,d,0)==0 && MmxWeaponsDamage(g_ram,enemy,d,128)==128,
        "native immunity and reflection remain authoritative");
  check(MmxWeaponsDamage(g_ram,enemy,d,1)==2,"first 5/3 ray contact rounds cumulative damage");
  check(MmxWeaponsDamage(g_ram,enemy,d,1)==0,"same ray cannot hit the same enemy twice");
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  unsigned total=2;
  for(unsigned i=0;i<2;++i) {
    c=MmxWeaponsGetCombatState();c.shots[slot].hit_slots=0;MmxWeaponsSetCombatState(c);
    total+=MmxWeaponsDamage(g_ram,enemy,d,1);
  }
  check(total==5,"three rays equal exactly five X1 buster hits, without per-ray rounding inflation");
  check(RtlLoadSnapshotFromMemory(start,n),"damage fraction survives save/load");
  c=MmxWeaponsGetCombatState();c.shots[slot].hit_slots=0;MmxWeaponsSetCombatState(c);
  check(MmxWeaponsDamage(g_ram,enemy,d,1)==1,"restored fractional carry gives the same next contact");
  c=MmxWeaponsGetCombatState();c.shots[slot].hit_slots=0;MmxWeaponsSetCombatState(c);
  g_ram[enemy+0x28]=6;
  check(MmxWeaponsDamage(g_ram,enemy,d,1)==1,"boss receives neutral source damage without imported weakness");
  /* A v11 snapshot ends at the old combat prefix. */
  size_t chunk=0;
  for(size_t i=n-8;i>8;--i) {uint32_t magic;memcpy(&magic,start+i,4);if(magic==0x4d4d5854u) {chunk=i;break;}}
  check(chunk!=0,"damage snapshot chunk found");
  uint32_t particles_legacy=12;memcpy(start+chunk+4,&particles_legacy,4);
  check(RtlLoadSnapshotFromMemory(start,n-(sizeof(c)-MMX_WEAPON_COMBAT_DAMAGE_SIZE)),
        "v12 damage snapshot migrates without visual particles");
  c=MmxWeaponsGetCombatState();unsigned particles=0;for(unsigned i=0;i<16;++i) particles+=c.effects[i].active;
  check(!particles,"legacy particle slots initialize empty");
  uint32_t legacy=11;memcpy(start+chunk+4,&legacy,4);
  check(RtlLoadSnapshotFromMemory(start,n-(sizeof(c)-MMX_WEAPON_COMBAT_LEGACY_SIZE)),
        "v11 projectile snapshot migrates without damage carry");
  c=MmxWeaponsGetCombatState();check(!c.enemies[0].active,"legacy damage carry initializes empty");
  puts("MMX SOURCE DAMAGE CHECKS PASSED");
}
static void zero_half_charge_checks(const char *fixture) {
  for (unsigned arms=0;arms<2;++arms) {
    check(RtlLoadSnapshot(fixture),"restore for native half-charge check");
    g_ram[0x1f99]=(uint8_t)(arms?2:0);
    for(unsigned i=0;i<30;++i) frame(SNES_PAD_Y);
    frame(0);
    check(zero_projectiles(1)==1 && !zero_projectiles(2) && !zero_projectiles(3),
          "first Zero charge emits native green class 1 with or without X1 arms");
    check(!MmxZeroGetState().combo && !MmxZeroGetState().burst && !(g_ram[0xc2f]&64),
          "half-charge has no stored beam sequence or looping charge sound");
    zero_replay(2);
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),arms?".half-charge-arms.cap":".half-charge.cap");
  }
  check(RtlLoadSnapshot(fixture),"restore after native half-charge check");
}
static unsigned zero_charge_actors(void) {
  unsigned n=0;
  for(unsigned d=0xc98;d<0xe18;d+=32) n+=g_ram[d] && MmxZeroNativeChargeObject(d,g_ram[d+10]);
  return n;
}
static void zero_charge_visual_checks(const char *fixture,uint8 *start,uint8 *expected,uint8 *actual,size_t cap) {
  const char *capture=getenv("MMX_ZERO_TEST_CAPTURE");
  for(unsigned arms=0;arms<=2;arms+=2) {
    check(RtlLoadSnapshot(fixture),"restore charge-color fixture");
    g_ram[0x1f99]=(uint8_t)arms;
    const uint16_t *palettes[3]={0};
    for(unsigned tick=1;tick<=228;++tick) {
      frame(SNES_PAD_Y); MmxZeroState z=MmxZeroGetState();
      check(zero_charge_actors()<=1,"one native charge effect while holding fire");
      if(z.charge>=25 && !(z.charge_phase&2)) {
        unsigned tier=z.charge>=201?2:z.charge>=141?1:0;
        palettes[tier]=MmxZeroBodyColors(&z);
        check(palettes[tier]!=MmxZeroColors()+16,"body flashes before obtaining X1 arms");
        if(tick==29 || tick==145 || tick==205) {
          char suffix[64];snprintf(suffix,sizeof(suffix),".charge-%u-%u.cap",arms,tier);
          zero_capture(capture,suffix);
        }
      }
      if(tick>=21) check(MmxZeroChargePose(&z)!=NULL,"original X3 charge particle poses available");
    }
    check(palettes[0] && palettes[1] && palettes[2] && memcmp(palettes[0],palettes[1],32) &&
        memcmp(palettes[1],palettes[2],32),"blue, purple and green charge palettes are distinct");
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    for(unsigned i=0;i<16;++i) frame(SNES_PAD_Y);
    size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore full-charge animation");
    for(unsigned i=0;i<16;++i) frame(SNES_PAD_Y);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);
    same(expected,en,actual,an,"full-charge particles and palette replay exactly");
    frame(0);zero_replay(17);frame(SNES_PAD_Y);zero_replay(90);
    MmxZeroState z=MmxZeroGetState();
    check(z.combo==2 && z.saber_ready && !z.burst && !MmxZeroChargePose(&z) && !zero_charge_actors(),
        "stored saber retains readiness after beams without orbiting charge effects");
    unsigned green=0,base=0;
    for(unsigned i=0;i<8;++i) {
      frame(0);z=MmxZeroGetState();const uint16_t *p=MmxZeroBodyColors(&z);
      green+=p==palettes[2];base+=p==MmxZeroColors()+16;
      if(p==palettes[2]) zero_capture(capture,".stored-saber.cap");
    }
    check(green==4 && base==4,"stored saber alternates original green and normal palette every two frames");
    frame(SNES_PAD_Y);z=MmxZeroGetState();
    check(z.slash && !z.saber_ready && MmxZeroBodyColors(&z)==MmxZeroColors()+16,"saber use consumes the green readiness flash");
  }
  check(RtlLoadSnapshot(fixture),"restore interrupted charge fixture");
  for(unsigned i=0;i<160;++i) frame(SNES_PAD_Y);
  /* Put the live charge actor in a later legal allocation, then take hurt. */
  check(g_ram[0xc98] && g_ram[0xca2]==1,"native charge actor allocated");
  memcpy(g_ram+0xd58,g_ram+0xc98,32);memset(g_ram+0xc98,0,32);
  g_ram[0xbaa]=0x0e;g_ram[0xbab]=0;frame(SNES_PAD_Y);
  check(!MmxZeroGetState().charge && !zero_charge_actors(),"hurt cancels charge including an effect outside the first pool slot");
  for(unsigned i=0;i<230;++i) {frame(SNES_PAD_Y);check(zero_charge_actors()<=1,"held fire after hurt never doubles the effect");}
  zero_health_swap();
  check(MmxZeroGetState().active_x && !zero_charge_actors(),"swap cancels Zero charge effects before X arrives");
  for(unsigned i=0;i<150;++i) frame(SNES_PAD_Y);
  zero_health_swap();
  check(!MmxZeroGetState().active_x && !zero_charge_actors(),"swap cancels X charge effects before Zero arrives");
  for(unsigned i=0;i<210;++i) {frame(SNES_PAD_Y);check(zero_charge_actors()<=1,"held fire after swap never doubles the effect");}
  frame(0);check(MmxZeroGetState().saber_ready,"recharged Zero releases the full combo normally");
  puts("MMX ZERO CHARGE COLOR AND CANCELLATION CHECKS PASSED");
}
#include "mmx_weapon_crystal_test.inc"
#include "mmx_weapon_gravity_test.inc"
#include "mmx_weapon_parasitic_test.inc"
#include "mmx_weapon_stage_test.inc"
#include "mmx_weapon_weakness_test.inc"
static void zero_state_checks(const char *assets, const char *fixture, uint8 *start,
                              uint8 *expected, uint8 *actual, size_t cap) {
  check(fixture != NULL && MmxZeroLoad(assets), "Zero local assets load");
  MmxZeroRegisterHooks();
  check(RtlLoadSnapshot(fixture), "Zero gameplay fixture loads");
  MmxZeroCancel(g_ram);
  check(g_ram[0xba9] == 2 && g_ram[0xbaa] == 0 && g_ram[0xbdb] == 0 &&
        !g_ram[0x1f99] && (g_ram[0xbd3] & 4), "fixture is standing, unupgraded buster");
  g_config.widescreen = false;
  int w,h; MmxPrepareFrame(1280,720,&w,&h);
  check(w == 256 && g_mmx_custom_renderer, "Zero activates native-width compositor");
  const char *weapons=getenv("MMX_WEAPONS_TEST_ASSETS");
  if (getenv("MMX_SOURCE_PACK_TEST")) { weapon_source_pack_checks(fixture,start,expected,actual,cap);return; }
  if (weapons) {
    if (getenv("MMX_WEAPON_STAGE_TEST")) weapon_stage_checks(weapons);
    else if (getenv("MMX_WEAPON_DAMAGE_TEST")) weapon_damage_checks(weapons,fixture,start,cap);
    else if (getenv("MMX_WEAPON_CYCLE_TEST")) weapon_cycle_checks(weapons,fixture,start,cap);
    else if (getenv("MMX_WEAPON_FROST_TEST")) weapon_frost_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_BUBBLE_TEST")) weapon_bubble_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_MAGNET_TEST")) weapon_magnet_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_TRIAD_TEST")) weapon_triad_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_FANG_TEST")) weapon_fang_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_CHAIN_TEST")) weapon_chain_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_SILK_TEST")) weapon_silk_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_CRYSTAL_TEST")) weapon_crystal_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_GRAVITY_TEST")) weapon_gravity_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_PARASITIC_TEST")) weapon_parasitic_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_SPEED_TEST")) weapon_speed_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_WHEEL_TEST")) weapon_wheel_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_SONIC_TEST")) weapon_sonic_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_RAY_TEST")) weapon_ray_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_ACID_TEST")) weapon_acid_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_ENERGY_TEST")) weapon_energy_checks(weapons,fixture,start,expected,actual,cap);
    else if (getenv("MMX_WEAPON_COMBAT_TEST")) weapon_combat_checks(weapons,fixture,start,expected,actual,cap);
    else weapon_menu_checks(weapons,fixture,start,expected,actual,cap);
    return;
  }
  if(getenv("MMX_ZERO_CHARGE_ONLY")) { zero_charge_visual_checks(fixture,start,expected,actual,cap); return; }
  if(getenv("MMX_ZERO_READY_ONLY")) { zero_ready_checks(fixture); return; }
  zero_swap_checks(fixture,start,expected,actual,cap);
  if(getenv("MMX_ZERO_SWAP_ONLY")) { puts("MMX SELECT SWAP CHECKS PASSED"); return; }
  zero_health_checks(fixture,start,expected,actual,cap);
  if(getenv("MMX_ZERO_HEALTH_ONLY")) { puts("MMX CHARACTER HP CHECKS PASSED"); return; }
  zero_motion_checks(fixture);
  zero_combat_checks(fixture);
  zero_half_charge_checks(fixture);
  check(RtlLoadSnapshot(fixture), "restore for burst state replay");
  for(unsigned i=0;i<201;++i) frame(SNES_PAD_Y);
  zero_replay(4);
  size_t burst_n=RtlSaveSnapshotToMemory(start,cap);
  zero_replay(8); size_t burst_en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,burst_n),"load during first windup");
  zero_replay(8); size_t burst_an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,burst_en,actual,burst_an,"windup replay emits exactly the same shot");
  size_t burst_rn=RtlRollbackSaveToMemory(start,cap);
  zero_replay(14); size_t burst_ren=RtlRollbackSaveToMemory(expected,cap);
  check(RtlRollbackLoadFromMemory(start,burst_rn),"load live beam and stored follow-ups");
  zero_replay(14); burst_an=RtlRollbackSaveToMemory(actual,cap);
  same(expected,burst_ren,actual,burst_an,"live beam and stored combo rollback replay");
  /* v4/v5 had only the 12/18-byte prefix. Both must still load, with the
   * former full-charge combo interpreted as saber-capable. */
  burst_n=RtlSaveSnapshotToMemory(start,cap);
  size_t chunk=0;
  for(size_t i=burst_n-8;i>8;--i) {
    uint32_t magic; memcpy(&magic,start+i,4);
    if(magic==0x4d4d5854u) { chunk=i; break; }
  }
  check(chunk!=0,"Zero game chunk located");
  size_t zero_offset=burst_n-sizeof(MmxZeroState)-sizeof(MmxWeaponsState)-
      sizeof(MmxWeaponCombatState)-sizeof(MmxCoopState);
  for(uint32_t v=4;v<=5;++v) {
    memcpy(start+chunk+4,&v,4);
    size_t prefix=v==4?MMX_ZERO_LEGACY_STATE_SIZE:MMX_ZERO_ANIMATION_STATE_SIZE;
    check(RtlLoadSnapshotFromMemory(start,zero_offset+prefix),"legacy Zero state prefix loads");
    check(MmxZeroGetState().combo==1 && MmxZeroGetState().saber_ready && !MmxZeroGetState().burst,"legacy stored combo migrates");
  }
  uint32_t v6=6; memcpy(start+chunk+4,&v6,4);
  check(RtlLoadSnapshotFromMemory(start,zero_offset+MMX_ZERO_COMBAT_STATE_SIZE),
        "pre-swap v6 save retains combat prefix");
  check(MmxZeroActive() && !MmxZeroSwapping(),"pre-swap save defaults to Zero without an exchange");
  uint32_t v7=7; memcpy(start+chunk+4,&v7,4);
  check(RtlLoadSnapshotFromMemory(start,zero_offset+MMX_ZERO_SWAP_STATE_SIZE),
        "pre-HP v7 save retains character/swap prefix");
  check(!MmxZeroGetState().hp_valid,"pre-HP save seeds separate pools from native HP on first update");
  check(RtlLoadSnapshot(fixture), "restore for combo probe");
  for (int i=0;i<201;++i) frame(SNES_PAD_Y);
  frame(0);
  check(MmxZeroGetState().combo == 1 && MmxZeroGetState().saber_ready &&
        !zero_projectiles(3), "full charge begins original windup and stores two follow-ups");
  zero_replay(17); frame(SNES_PAD_Y); zero_replay(9);
  check(zero_projectiles(3) == 2 && MmxZeroGetState().combo == 2, "two native charged shots coexist after second windup");
  for (unsigned i=0;i<160 && (MmxZeroGetState().burst || MmxZeroGetState().shot_mask || g_ram[0xc25]);++i) frame(0);
  check(!MmxZeroGetState().burst && !MmxZeroGetState().shot_mask && !g_ram[0xc25], "beams and their disappearance effects retire");
  frame(SNES_PAD_Y); zero_replay(12);
  MmxZeroState state = MmxZeroGetState();
  check(state.slash > 7 && state.projectile && !state.combo, "saber reaches active frames");
  check(g_ram[0x1f99] == 0, "combo does not grant X1 upgrades");
  const char *capture = getenv("MMX_ZERO_TEST_CAPTURE");
  zero_capture(capture, "");
  size_t n = RtlSaveSnapshotToMemory(start,cap);
  zero_replay(10); size_t en = RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n), "mid-saber memory load");
  size_t an = RtlSaveSnapshotToMemory(actual,cap);
  same(start,n,actual,an,"mid-saber complete immediate restore");
  zero_replay(10); an = RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"mid-saber deterministic replay");
  check(RtlLoadSnapshotFromMemory(start,n), "restore before rollback probe");
  size_t rn = RtlRollbackSaveToMemory(start,cap);
  zero_replay(8); en = RtlRollbackSaveToMemory(expected,cap);
  check(RtlRollbackLoadFromMemory(start,rn), "mid-saber rollback load");
  zero_replay(8); an = RtlRollbackSaveToMemory(actual,cap);
  same(expected,en,actual,an,"mid-saber rollback replay");
  check(RtlRollbackLoadFromMemory(start,rn), "restore before rewind probe");
  snes_rewind_configure();
  for (int i=0;i<12;++i) { frame(0); snes_rewind_note_frame(); }
  en = RtlSaveSnapshotToMemory(expected,cap);
  for (int i=0;i<6;++i) { frame(0); snes_rewind_note_frame(); }
  check(snes_rewind_open(), "mid-saber rewind opens");
  rewind_six_frames(); snes_rewind_commit();
  an = RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"rewind restores complete mid-saber state");
  snes_rewind_shutdown();
  zero_replay(60);
  check(!MmxZeroGetState().slash && !MmxZeroGetState().projectile && !g_ram[0xbdd], "saber and projectile counts retire");
  check(RtlLoadSnapshot(fixture), "restore for dash probe");
  unsigned x = g_ram[0xbad] | g_ram[0xbae] << 8;
  for (int i=0;i<8;++i) frame(SNES_PAD_A | SNES_PAD_RIGHT);
  unsigned dx = (g_ram[0xbad] | g_ram[0xbae] << 8) - x;
  check(dx >= 24 && !g_ram[0x1f99], "unupgraded dash moves at dash speed");
  zero_capture(capture, ".dash.cap");
  check(RtlLoadSnapshot(fixture), "restore for aerial saber probe");
  unsigned floor = g_ram[0xbb0] | g_ram[0xbb1] << 8;
  MmxZeroSetState((MmxZeroState){.combo=2,.saber_ready=1});
  for (int i=0;i<6;++i) frame(SNES_PAD_B);
  frame(SNES_PAD_B | SNES_PAD_Y); zero_replay(12);
  check(MmxZeroGetState().air && MmxZeroGetState().slash > 7 &&
        (g_ram[0xbb0] | g_ram[0xbb1] << 8) < floor, "aerial saber retains jump and gravity");
  zero_capture(capture, ".air.cap");
  zero_replay(100);
  check((g_ram[0xbb0] | g_ram[0xbb1] << 8) == floor && !MmxZeroGetState().slash,
        "original-size Zero lands on the same floor");
  check(RtlLoadSnapshot(fixture), "restore for special weapon probe");
  /* Fixture inventory grant only: production code never grants weapons. */
  g_ram[0x1f87] = 0; g_ram[0x1f88] = 0xdc;
  for (int i=0;i<6;++i) frame(SNES_PAD_R);
  zero_replay(1);
  check(g_ram[0xbdb] == 2, "native weapon switch selects Homing Torpedo");
  frame(SNES_PAD_Y); zero_replay(8);
  check(zero_projectiles(7) && g_ram[0x1f88] != 0xdc, "normal Torpedo spawns and spends energy");
  for (int i=0;i<181;++i) frame(SNES_PAD_Y);
  frame(0); zero_replay(8);
  check(!zero_projectiles(0x10) && !g_ram[0x1f99], "charged special remains unavailable without arms");
  zero_replay(60); g_ram[0x1f99] = 2;
  for (int i=0;i<181;++i) frame(SNES_PAD_Y);
  frame(0); zero_replay(8);
  check(zero_projectiles(0x10) == 5, "arm upgrade permits five charged Torpedo projectiles");
  zero_capture(capture, ".torpedo.cap");
  check(RtlLoadSnapshot(fixture), "restore for life pickup probe");
  /* Spawn the retail 1-up actor in a copied test fixture. Collection and the
   * life counter still execute the original game's collision/update path. */
  memset(g_ram + 0x1628,0,48); g_ram[0x1628] = 1; g_ram[0x1632] = 4; g_ram[0x1633] = 128;
  unsigned life_x = (g_ram[0xbad] | g_ram[0xbae] << 8) + 48;
  unsigned life_y = (g_ram[0xbb0] | g_ram[0xbb1] << 8) - 24;
  g_ram[0x162d] = (uint8_t)life_x; g_ram[0x162e] = (uint8_t)(life_x >> 8);
  g_ram[0x1630] = (uint8_t)life_y; g_ram[0x1631] = (uint8_t)(life_y >> 8);
  zero_replay(20); check(g_ram[0x163e] == 0x11,"retail life pickup submits its head animation");
  zero_capture(capture, ".life.cap");
  unsigned lives = g_ram[0x1f80];
  memcpy(g_ram + 0xbad,g_ram + 0x162d,2); memcpy(g_ram + 0xbb0,g_ram + 0x1630,2);
  zero_replay(8); check(g_ram[0x1f80] == lives + 1,"Zero collects a 1-up through native collision");
  check(RtlLoadSnapshot(fixture), "restore for pause menu probe");
  frame(SNES_PAD_START); zero_replay(120);
  zero_capture(capture, ".menu.cap"); zero_menu_pixels();
  check(RtlLoadSnapshot(fixture), "restore for upgraded menu probe");
  g_ram[0x1f99] = 15; frame(SNES_PAD_START); zero_replay(120);
  zero_capture(capture, ".menu-armor.cap"); zero_menu_pixels();
  zero_menu_transition_checks(fixture,capture,0);
  zero_menu_transition_checks(fixture,NULL,15);
  zero_weapon_checks(fixture,capture);
  MmxZeroDisable(); MmxBeforeFrame(); MmxPrepareFrame(1280,720,&w,&h);
  check(!g_mmx_custom_renderer && !MmxZeroEnabled(), "disabling returns to stock presentation");
  puts("MMX ZERO RUNTIME CHECKS PASSED");
}
int main(int argc, char **argv) {
  check(argc == 2 || argc == 3, "ROM supplied");
  SDL_SetMainReady();
  check(snesrecomp_sdl_init(SDL_INIT_EVENTS), "SDL initializes");
  g_audio_mutex = SDL_CreateMutex();
  static const SnesDesktopHostGame game = {
    .display_name = "MMX private netplay test", .build_version = "netplay-test-1",
    .num_players = 2,
    .before_run_frame = MmxBeforeFrame,
    .native_widescreen = 0, .state_menu_hotkeys = 1,
    .prepare_frame = MmxPrepareFrame, .begin_sim_frame = MmxBeginFrame,
    .end_sim_frame = MmxEndFrame, .draw_frame = MmxDrawFrame,
  };
  g_game = &game;
  ConfigUseStateMenuDefaults();
  FILE *f = fopen("config.ini", "w");
  fputs("[Graphics]\nDisplayAspect=8:7\n[KeyMap]\nLoad = F1,F2,F3,F4,F5,F6,F7,F8,F9,F10\n", f); fclose(f);
  ParseConfigFile("config.ini");
  /* Exercise the game adapter with the actual parsed settings, including a
   * setting change between frames. Pure renderer tests cannot catch a host
   * that forgets to pass Display Aspect into adaptive geometry. */
  g_config.widescreen = true;
  g_mmx_custom_aspect = MMX_ASPECT_ADAPTIVE;
  const int display_widths[] = {342, 398, 456};
  check(g_config.display_aspect == kSnesDisplayAspect_SquarePixels8x7, "Display Aspect parsed");
  for (int i = 0; i < 3; ++i) {
    int setting = (i + 1) % 3;
    /* The launcher updates the live setting after the initial config load. */
    if (i) g_config.display_aspect = SnesDisplayAspect_Clamp(setting);
    int w, h;
    MmxPrepareFrame(1920, 1080, &w, &h);
    check(w == display_widths[setting] && h == 224, "adaptive honors Display Aspect");
    SnesDisplayViewport viewport;
    MmxViewport(w, h, 1920, 1080, &viewport);
    check(viewport.width == 1920 && viewport.height == 1080, "adaptive fills matching window");
    g_mmx_custom_aspect = MMX_ASPECT_16_9;
    MmxPrepareFrame(3840, 1080, &w, &h);
    MmxViewport(w, h, 3840, 1080, &viewport);
    check(w == display_widths[setting] && viewport.width == 1920 && viewport.x == 960,
          "fixed view honors pixel shape and boxes wider window");
    g_config.widescreen = false;
    MmxPrepareFrame(1920, 1080, &w, &h);
    MmxViewport(w, h, 1920, 1080, &viewport);
    const int native_widths[] = {1440, 1234, 1080};
    check(w == 256 && viewport.width == native_widths[setting], "native view retains Display Aspect");
    g_config.widescreen = true;
    g_mmx_custom_aspect = MMX_ASPECT_ADAPTIVE;
  }
  g_config.display_aspect = kSnesDisplayAspect_Crt4x3;
  check(FindCmdForSdlKey(SDLK_F7, 0) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F8, 0) == kKeys_Rewind &&
        FindCmdForSdlKey(SDLK_F11, 0) == kKeys_Load + 6 &&
        FindCmdForSdlKey(SDLK_F12, 0) == kKeys_Load + 7, "F7/F8 and legacy config migration");
  HandleInput(SDLK_F7, 0, true); HandleInput(SDLK_F8, 0, true);
  check(g_savestate_menu_hotkey && g_rewind_hotkey, "host dispatches F7/F8");
  g_savestate_menu_hotkey = g_rewind_hotkey = 0;
  LauncherModel model = {0};
  g_launcher_config_path = "config.ini";
  launcher_binds_set_hotkey(&model, LNG_HK_SAVE_STATE_MENU, SDLK_F9, KMOD_CTRL);
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, SDLK_F9, 0);
  ConfigReloadKeyMap("config.ini");
  check(FindCmdForSdlKey(SDLK_F9, KMOD_CTRL) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F9, 0) == kKeys_Rewind, "launcher rebind and slot collision");
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, 0, 0);
  ConfigReloadKeyMap("config.ini");
  check(!FindCmdForSdlKey(SDLK_F8, 0) &&
        FindCmdForSdlKey(SDLK_F9, 0) != kKeys_Rewind, "launcher clear persists");
  g_launcher_config_path = NULL;
  f = fopen(argv[1], "rb"); check(f != NULL, "ROM opens");
  fseek(f, 0, SEEK_END); long rom_size = ftell(f); rewind(f);
  uint8 *rom = malloc(rom_size);
  check(fread(rom, 1, rom_size, f) == (size_t)rom_size, "ROM reads"); fclose(f);
  if ((rom_size & 0x7fff) == 512) {
    rom_size -= 512;
    memmove(rom, rom + 512, rom_size);
  }
  g_config.new_renderer = true; g_config.widescreen = true;
  g_mmx_custom_renderer = true;
  MmxRendererSetRom(rom, rom_size);
  g_last_drawable_width = 1280; g_last_drawable_height = 720;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer;
  RtlRegisterGame(&kMmxGameInfo);
  check(SnesInit(rom, rom_size) != NULL, "game initializes");
  g_spc_player = SmwSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
  if (getenv("MMX_WEAKNESS_TEST")) {
    weapon_weakness_checks();
    return 0;
  }
  MkDir("saves");
  size_t cap = 2u * 1024u * 1024u;
  uint8 *start = malloc(cap), *expected = malloc(cap), *actual = malloc(cap);
  const char *zero_assets = getenv("MMX_ZERO_TEST_ASSETS");
  const char *zero_title = getenv("MMX_ZERO_TITLE_FIXTURE");
  if(getenv("MMX_COOP_VIEWS_BOOT_TEST")) {
    coop_view_boot_checks(zero_assets,start,expected,actual,cap);return 0;
  }
  if(getenv("MMX_COOP_REPLAY")) { coop_replay_probe(zero_assets,getenv("MMX_COOP_REPLAY")); return 0; }
  if(getenv("MMX_COOP_ARMOR_PILOT_FIXTURE")) { coop_armor_pilot_checks(zero_assets,getenv("MMX_COOP_ARMOR_PILOT_FIXTURE")); return 0; }
  if(getenv("MMX_COOP_ARMOR_FIXTURE")) { coop_armor_checks(zero_assets,getenv("MMX_COOP_ARMOR_FIXTURE")); return 0; }
  if(getenv("MMX_COOP_CANISTER_FIXTURE")) { coop_canister_checks(zero_assets,getenv("MMX_COOP_CANISTER_FIXTURE")); return 0; }
  if(getenv("MMX_COOP_SLAB_DROP_TEST")) { coop_slab_drop_checks(zero_assets,getenv("MMX_COOP_SLAB_DROP_TEST")); return 0; }
  if(getenv("MMX_COOP_COLLAPSE_TEST")) { coop_collapse_checks(zero_assets,getenv("MMX_COOP_COLLAPSE_TEST")); return 0; }
  if(getenv("MMX_SABER_LOCK_TEST")) {
    saber_lock_checks(zero_assets,start,cap);return 0;
  }
  if(getenv("MMX_HITBOX_OVERLAY_TEST")) {
    hitbox_overlay_checks(zero_assets,start,expected,actual,cap);return 0;
  }
  if (getenv("MMX_GRAPHICS_PACING_TEST")) {
    graphics_pacing_checks(zero_assets, start, expected, actual, cap);
    return 0;
  }
  if (getenv("MMX_COOP_LAUNCHER_ROOT")) {
    coop_launcher_checks(getenv("MMX_COOP_LAUNCHER_ROOT"),argv[1],getenv("MMX_COOP_X3_ROM"));
    return 0;
  }
  if (getenv("MMX_NETPLAY_POLICY_ROOT")) {
    netplay_policy_checks(getenv("MMX_NETPLAY_POLICY_ROOT"), argv[1], getenv("MMX_COOP_X3_ROM"));
    return 0;
  }
#if defined(SNES_HAS_LOBBY_CLIENT)
  if (getenv("MMX_NETPLAY_PAIR_ROOT")) {
    netplay_pair_checks(getenv("MMX_NETPLAY_PAIR_ROOT"), argv[1], getenv("MMX_COOP_X3_ROM"),
        getenv("MMX_ZERO_TEST_FIXTURE"), getenv("MMX_NETPLAY_PAIR_GUEST") != NULL);
    return 0;
  }
#endif
  if (getenv("MMX_ZERO_MODERN_TEST")) {
    modern_checks(zero_assets,getenv("MMX_ZERO_TEST_FIXTURE"),start,expected,actual,cap);
    return 0;
  }
  if (getenv("MMX_COOP_TEST")) {
    coop_checks(zero_assets,getenv("MMX_ZERO_TEST_FIXTURE"),start,expected,actual,cap);
    return 0;
  }
  if (zero_assets && zero_title) {
    check(MmxZeroLoad(zero_assets),"title Zero assets load");
    MmxZeroRegisterHooks();
    check(RtlLoadSnapshot(zero_title),"private title fixture loads");
    zero_replay(45);
    const char *capture=getenv("MMX_ZERO_TEST_CAPTURE");
    zero_capture(capture,".title-idle.cap");
    check(g_ram[0xba9]==2 && g_ram[0xbbf]==0,"title cursor is standing player");
    frame(SNES_PAD_START);
    bool title_shot=false;
    for(unsigned i=0;i<40;++i) {
      frame(0);
      title_shot |= g_ram[0x1229]==2 && g_ram[0x123e]==0x0e;
      if(i==4 || i==9 || i==19) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),".title-shot%u.cap",i+2);
        zero_capture(capture,suffix);
      }
    }
    check(title_shot,"title confirmation preserves native full-buster projectile");
    puts("MMX ZERO TITLE CHECKS PASSED"); return 0;
  }
  if (zero_assets) {
    zero_state_checks(zero_assets, getenv("MMX_ZERO_TEST_FIXTURE"), start, expected, actual, cap);
    return 0;
  }
  if (argc == 3) {
    check(RtlLoadSnapshot("saves/save11.sav"), "file loads in new process");
    replay(10);
    size_t n = RtlSaveSnapshotToMemory(actual, cap);
    f = fopen("expected.bin", "rb"); check(f != NULL, "reference opens");
    size_t en = fread(expected, 1, cap, f); fclose(f);
    same(expected, en, actual, n, "new-process replay");
    puts("MMX STATE CHECKS PASSED"); return 0;
  }
  for (int phase = 0; phase < 2; ++phase) {
    for (int i = 0; i < 600; ++i) frame(i == 200 || i == 350 ? SNES_PAD_START : 0);
    size_t n = RtlSaveSnapshotToMemory(start, cap);
    replay(30); size_t en = RtlSaveSnapshotToMemory(expected, cap);
    check(RtlLoadSnapshotFromMemory(start, n), "memory load");
    size_t an = RtlSaveSnapshotToMemory(actual, cap);
    same(start, n, actual, an, "complete immediate restore");
    replay(30); an = RtlSaveSnapshotToMemory(actual, cap);
    same(expected, en, actual, an, "30-frame deterministic replay");
  }
  snes_savestate_menu_poll_open(SNES_PAD_SELECT | SNES_PAD_R);
  check(snes_savestate_menu_is_open(), "save browser opens");
  uint64 clock = g_cpu.master_cycles;
  snes_savestate_menu_handle_key(SDLK_EQUALS, 0);
  SDL_Event key = {0};
  key.type = SDL_KEYDOWN;
#if SNESRECOMP_SDL3
  key.key.key = SDLK_s;
#else
  key.key.keysym.sym = SDLK_s;
#endif
  SDL_PushEvent(&key);
  bool running = true;
  PumpOverlayEvents(&running, snes_savestate_menu_handle_key);
  check(OverlayNavInputs() & SNES_PAD_X, "browser receives keyboard save action");
  snes_savestate_menu_poll_nav(OverlayNavInputs(), 1);
  HandleInput(SDLK_s, 0, false);
  check(clock == g_cpu.master_cycles, "saving does not advance guest");
  snes_savestate_menu_poll_nav(0, 2);
  snes_savestate_menu_poll_nav(SNES_PAD_B, 3);
  check(!snes_savestate_menu_is_open(), "B closes save browser");
  replay(10); size_t en = RtlSaveSnapshotToMemory(expected, cap);
  f = fopen("expected.bin", "wb"); fwrite(expected, 1, en, f); fclose(f);
  snes_rewind_configure();
  for (int i = 0; i < 12; ++i) { frame(0); snes_rewind_note_frame(); }
  size_t n = RtlSaveSnapshotToMemory(start, cap);
  for (int i = 0; i < 6; ++i) { frame(SNES_PAD_RIGHT); snes_rewind_note_frame(); }
  check(snes_rewind_open(), "rewind opens");
  rewind_six_frames();
  snes_rewind_commit();
  size_t an = RtlSaveSnapshotToMemory(actual, cap);
  same(start, n, actual, an, "rewind restores selected frame");
  snes_rewind_shutdown();
  /* Both meanings of the old game chunk v2 must load without reading EOF.
   * File and memory use the same format discrimination. */
  n = RtlSaveSnapshotToMemory(start, cap);
  size_t chunk = 0;
  for (size_t i = n - 8; i > 8; --i) {
    uint32_t magic; memcpy(&magic, start + i, 4);
    if (magic == 0x4d4d5854u) { chunk = i; break; }
  }
  check(chunk != 0, "game chunk located");
  uint32_t version = 2; memcpy(start + chunk + 4, &version, 4);
  check(RtlLoadSnapshotFromMemory(start, n), "shared-host v2 with execution tail loads");
  f = fopen("main-v2.sav", "wb"); fwrite(start, 1, n, f); fclose(f);
  check(RtlLoadSnapshot("main-v2.sav"), "shared-host v2 file loads");
  check(RtlLoadSnapshotFromMemory(start, chunk + 464), "adaptive v2 without tail loads");
  f = fopen("adaptive-v2.sav", "wb"); fwrite(start, 1, chunk + 464, f); fclose(f);
  check(RtlLoadSnapshot("adaptive-v2.sav"), "adaptive v2 file loads");
  replay(10);
  check(g_mmx_custom_renderer && g_mmx_custom_view.width > 256, "adaptive remains active");
  puts("MMX STATE CHECKS PASSED");
  return 0;
}
