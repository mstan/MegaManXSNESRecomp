/* Exercise the title entry and the original controllers in a real ROM.
 * Kept separate from the portable queue tests: no ROM fixtures are shipped. */
#include "mod_audio.h"
#include "mmx_boss_rush_foreign.h"
#include "mmx_cart_extension.h"
#include "mmx_boss_rush_foreign_art.h"
#include "mmx_boss_rush_audio.h"
#include "mmx_weapon_combat.h"
#include "mmx_source_assets.h"
#include "host_paths.h"
#include "mod_runtime.h"
#include "recomp_launcher.h"
#include "saber/mmx_saber_plugin.h"
#include "saber/mmx_saber_attack.h"
#include <stdlib.h>
#include "mmx_wide_policy.h"

static void rush_keep_players_alive(void) {
  g_ram[0xbcf]=0xa0;
  if(MmxCoopEnabled()) {
    MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) c.players[i].body[0x27]=0xa0;
    MmxCoopSetState(&c);
  }
}
static unsigned rush_death_sounds;
static void rush_visual_bmp(const char *path);
static void rush_pause_frames(const char *prefix) {
  for(unsigned t=0;t<90;++t) {
    frame(0);
    if(getenv("MMX_BOSS_RUSH_PAUSE_TRACE") && (t<30 || t%10==0)) {
      char path[96];snprintf(path,sizeof(path),"rush-pause-%s-%02u.bmp",prefix,t);
      rush_visual_bmp(path);
    }
  }
}
static void rush_scrolling_checks(uint8_t *snapshot,size_t cap) {
  size_t n=RtlSaveSnapshotToMemory(snapshot,cap);check(n,"scrolling encounter snapshot saves");
  MmxBossRushState s=MmxBossRushGetState();unsigned hook=0;
  for(unsigned d=0x1628;d<0x1928;d+=48)
    if(g_ram[d] && g_ram[d+10]==0x12 && MmxBossRushOwner(d)==0) hook=d;
  unsigned hook_x=s.camera_x+MMX_RUSH_ARENA_CENTER_X;
  check(hook && eagle_word(g_ram+hook+5)==hook_x,
      "Penguin's native handle aligns with the mount at the full room's center");
  unsigned body=s.bosses[0].object;
  g_ram[body+1]=4;g_ram[body+2]=2;g_ram[body+3]=0;
  bool pulled=false;
  for(unsigned t=0;t<420;++t) {
    frame(0);rush_keep_players_alive();
    pulled|=g_ram[hook+0x1b]!=0;
    if(pulled) break;
  }
  fprintf(stderr,"rush handle x=%04x target=%04x pulled=%u body=%04x,%04x state=%u:%u\n",
      eagle_word(g_ram+hook+5),hook_x,pulled,eagle_word(g_ram+body+5),eagle_word(g_ram+body+8),
      g_ram[body+2],g_ram[body+3]);
  check(pulled,"Penguin's native jump reaches and pulls the relocated handle");
  check(abs((int)eagle_word(g_ram+body+5)-(int)hook_x)<=12,
      "Penguin's grabbing pose stays beside the relocated handle");
  check(MmxRendererSaveCapture("rush-penguin-hook.cap"),"native centered handle pull capture saves");
  check(RtlLoadSnapshotFromMemory(snapshot,n),"encounter restores after handle check");
  unsigned right=eagle_word(g_ram+0x1e58),left=eagle_word(g_ram+0x1e56);
  check(right-left==MMX_RUSH_MAMMOTH_SCROLL,"combat keeps the original Mammoth scroll range");
  for(unsigned direction=0;direction<2;++direction) {
    unsigned key=direction?SNES_PAD_RIGHT:SNES_PAD_LEFT;
    for(unsigned t=0;t<720;++t) {
      g_ram[0xbd3]=120;
      if(MmxCoopEnabled()) {
        MmxCoopState c=MmxCoopGetState();
        for(unsigned i=0;i<2;++i)c.players[i].body[0x2b]=120;
        MmxCoopSetState(&c);
      }
      frame(key|(MmxCoopEnabled()?key<<12:0));rush_keep_players_alive();
      if(t%120==0)fprintf(stderr,"rush scroll tick=%u dir=%u action=%u:%u script=%u lock=%u freeze=%u/%u/%u x=%04x\n",
          t,direction,g_ram[0xbaa],g_ram[0xbab],g_ram[0xc01],g_ram[0xc16],g_ram[0x1f26],g_ram[0xc0c],g_ram[0x1f13],eagle_word(g_ram+0xbad));
      if(eagle_word(g_ram+0x1e4d)==(direction?right:left)) break;
    }
    unsigned cam=eagle_word(g_ram+0x1e4d);
    fprintf(stderr,"rush combat scroll direction=%u x=%04x camera=%04x limits=%04x/%04x\n",
        direction,eagle_word(g_ram+0xbad),cam,eagle_word(g_ram+0x1e56),eagle_word(g_ram+0x1e58));
    check(direction?cam==right:cam==left,"players can scroll to both ends during the two-boss fight");
    s=MmxBossRushGetState();
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING && !s.defeated,
        "both bosses remain alive while the room scrolls away and back");
    check(eagle_word(g_ram+hook+5)==hook_x,"native handle stays anchored while the camera moves");
    check(MmxRendererSaveCapture(direction?"rush-combat-right.cap":"rush-combat-left.cap"),"scrolling combat capture saves");
  }
  check(RtlLoadSnapshotFromMemory(snapshot,n),"encounter restores after scrolling checks");
  frame(0);rush_keep_players_alive();
}
static void rush_conveyor_checks(uint8_t *snapshot,size_t cap) {
  size_t n=RtlSaveSnapshotToMemory(snapshot,cap);check(n,"live conveyor fixture saves");
  for(unsigned seat=0;seat<2;++seat) for(unsigned phase=0;phase<4;++phase) for(unsigned direction=0;direction<2;++direction) {
    check(RtlLoadSnapshotFromMemory(snapshot,n),"conveyor fixture restores");
    MmxBossRushState s=MmxBossRushGetState();
    if(!seat) {MmxBossRushBoss b=s.bosses[0];s.bosses[0]=s.bosses[1];s.bosses[1]=b;}
    check(s.bosses[seat].id==7,"Mammoth occupies each conveyor test seat");
    if(phase==1) {s.bosses[seat].phase=MMX_RUSH_DYING;s.bosses[seat].health=0;}
    if(phase==2) memset(&s.bosses[seat],0,sizeof(s.bosses[seat]));
    if(phase==3) s.mode=MMX_RUSH_PREPARING;
    check(MmxBossRushSetState(&s),"conveyor phase installs");
    eagle_putword(g_ram+0x2c,0xba8);g_ram[0xbac]=0;eagle_putword(g_ram+0xbad,s.camera_x+64);
    uint32_t before=(uint32_t)g_ram[0xbac]|((uint32_t)eagle_word(g_ram+0xbad)<<8);
    CpuState cpu=g_cpu;cpu.D=0;cpu.DB=cpu.PB=0x84;cpu.S=0x1ffe;cpu.P=0x20;cpu.host_return_valid=0;
    cpu_p_to_mirrors(&cpu);
    /* Enter just after the native solid-floor correction. Execute the real
     * carry, collision helper and return with the production hooks installed. */
    check(interp_bridge_run(&cpu,direction?0x84990d:0x8498ff),"native conveyor carry returns cleanly");
    uint32_t after=(uint32_t)g_ram[0xbac]|((uint32_t)eagle_word(g_ram+0xbad)<<8);
    uint32_t expected=phase?before:before+(direction?-128:128);
    fprintf(stderr,"conveyor seat=%u phase=%u direction=%u x=%06x expected=%06x\n",seat,phase,direction,after,expected);
    check(after==expected,"both conveyor directions carry only while Mammoth lives, in either boss seat");
  }
  check(RtlLoadSnapshotFromMemory(snapshot,n),"live arena restores after conveyor checks");
  frame(0);rush_keep_players_alive(); /* A load invalidates the presentation capture. */
  puts("MMX BOSS RUSH NATIVE CONVEYOR CHECKS PASSED");
}
static void rush_title_navigation_checks(uint8_t *snapshot,size_t cap) {
  size_t saved=RtlSaveSnapshotToMemory(snapshot,cap);check(saved,"title navigation baseline saves");
  for(unsigned row=0;row<4;++row) for(unsigned direction=0;direction<3;++direction) {
    check(RtlLoadSnapshotFromMemory(snapshot,saved),"title navigation baseline restores");
    for(unsigned step=0;step<row;++step) {frame(SNES_PAD_DOWN);zero_replay(12);}
    check(MmxBossRushGetState().selection==row,"native input reaches each of the four title rows");
    unsigned key=direction==2?SNES_PAD_SELECT:direction?SNES_PAD_DOWN:SNES_PAD_UP;
    unsigned expected=(row+(direction?1:3))%4;
    frame(key);zero_replay(12);
    fprintf(stderr,"title navigation row=%u direction=%s selected=%u expected=%u native=%u y=%u\n",
        row,direction==2?"select":direction?"down":"up",MmxBossRushGetState().selection,expected,g_ram[0x3c],eagle_word(g_ram+0xbb0));
    check(MmxBossRushGetState().selection==expected && eagle_word(g_ram+0xbb0)==166+16*expected,
        "both title navigation directions wrap over all four choices");
  }
  check(RtlLoadSnapshotFromMemory(snapshot,saved),"title cursor restores after navigation checks");
  puts("MMX BOSS RUSH TITLE NAVIGATION CHECKS PASSED");
}
static void rush_return_title_checks(void) {
  MmxBossRushFinish();frame(0);frame(SNES_PAD_DOWN);frame(0);frame(SNES_PAD_START);frame(0);
  const uint8_t *rom=g_snes->cart->rom;
  check(rom[0xd8d]==0xa9 && rom[0xd8f]==0x20 && rom[0xd90]==0xb0 && rom[0xd91]==0x87,
      "retail opening supplies the native title music command");
  unsigned song=rom[0xd8e],bank=rom[0x3007b+rom[0x3006b+song]];
  bool opening=false,title_music=false;
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t) {
    frame(0);
    opening|=g_ram[0xd1]==0 && g_ram[0x38]==0 && g_ram[0x39]!=0;
    title_music|=g_ram[0xfffd]==bank;
    if(t==240) check(MmxRendererSaveCapture("rush-return-opening.cap"),"return opening capture saves");
  }
  fprintf(stderr,"return title opening=%u music=%u bank=%02x expected=%02x state=%u/%u/%u menu=%u\n",
      opening,title_music,g_ram[0xfffd],bank,g_ram[0x38],g_ram[0x39],g_ram[0x3a],MmxBossRushGetState().menu);
  check(opening,"Main Menu restarts the full native opening before the logo flies in");
  check(title_music,"native opening replaces the boss song with the original title music");
  check(MmxBossRushGetState().mode==MMX_RUSH_OFF && MmxBossRushGetState().menu==1,
      "native opening returns to all four title choices");
  puts("MMX BOSS RUSH RETURN TITLE CHECKS PASSED");
}
/* Feasibility spike: a body controller built in a cart extension bank runs
 * inside X1's enemy loop for one Rush seat, calls a native X1 helper, and
 * replays deterministically. Seat 0 keeps a native Penguin beside it. */
static void rush_foreign_spike_checks(uint8_t *start,size_t cap) {
  const Cart *cart=g_snes->cart;
  uint32_t image=cart->romImageSize;uint8_t before[16];
  memcpy(before,cart->rom+0x1ffff0,sizeof(before));
  check(MmxCartExtensionEnsure() && MmxCartExtensionReady(),"cart image grows to 4 MB");
  cart=g_snes->cart;
  check(cart->romSize==0x400000 && cart->romImageSize==image && g_rom==cart->rom,
      "extension keeps the original image identity and updates g_rom");
  check(!memcmp(cart->rom+0x1ffff0,before,sizeof(before)) &&
      !memcmp(cart->rom+0x200000,cart->rom,0x200000),
      "upper half mirrors the original padded image before any write");
  /* INC $3E ; JSL $84:9086 (native RNG) ; STA $3F ; RTL */
  static const uint8_t body[]={0xe6,0x3e,0x22,0x86,0x90,0x84,0x85,0x3f,0x6b};
  check(MmxCartExtensionWrite(0xf08000,body,sizeof(body)),"spike controller writes to bank $F0");
  check(!memcmp(RomPtr(0xf08000),body,sizeof(body)),"bank $F0 reads back the spike controller");
  check(!MmxCartExtensionWrite(0xa08000,body,sizeof(body)) && !MmxCartExtensionWrite(0x86f000,body,2) &&
      !MmxCartExtensionWrite(0xf07fff,body,2),"writes outside the extension windows are refused");
  check(MmxBossRushForeignSetController(23,0xf08000),"spike controller registers for roster id 23");
  MmxBossRushState s=MmxBossRushGetState();
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
  memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);s.queued=0;
  check(MmxBossRushSetState(&s),"spike clears the opening pair");
  MmxBossRushRosterState roster={.available=MMX_RUSH_X1_MASK|(1u<<23),.queued=2};
  roster.queue[0]=0;roster.queue[1]=23;
  check(MmxBossRushRosterSetState(&roster),"spike queues Penguin and roster id 23");
  unsigned object=0;
  for(unsigned t=0;t<600 && !object;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[1].phase && s.bosses[1].id==23) object=s.bosses[1].object;
  }
  check(object && MmxBossRushOwner(object)==1,"roster id 23 spawns in seat 1");
  for(unsigned t=0;t<5;++t) {frame(0);rush_keep_players_alive();}
  uint8_t counter=g_ram[object+0x3e];uint16_t rng=eagle_word(g_ram+0xba6);
  unsigned changes=0;uint8_t last=g_ram[object+0x3f];
  for(unsigned t=0;t<60;++t) {
    frame(0);rush_keep_players_alive();
    if(g_ram[object+0x3f]!=last) {++changes;last=g_ram[object+0x3f];}
  }
  unsigned advanced=(uint8_t)(g_ram[object+0x3e]-counter);
  fprintf(stderr,"foreign spike counter advanced=%u rng-writes=%u rng %04x->%04x penguin=%u:%u\n",
      advanced,changes,rng,eagle_word(g_ram+0xba6),MmxBossRushGetState().bosses[0].phase,g_ram[MmxBossRushGetState().bosses[0].object+1]);
  check(advanced==60,"extension controller runs once per frame for its seat");
  check(changes>=30 && eagle_word(g_ram+0xba6)!=rng,"native X1 RNG call returns into the extension bank");
  s=MmxBossRushGetState();
  check(s.bosses[0].id==0 && s.bosses[0].phase && g_ram[s.bosses[0].object] && s.bosses[1].object==object,
      "native Penguin keeps running beside the foreign seat");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"foreign seat snapshot saves");
  static uint8_t first[0x20000];
  for(unsigned t=0;t<30;++t) {frame(0);rush_keep_players_alive();}
  memcpy(first,g_ram,sizeof(first));
  check(RtlLoadSnapshotFromMemory(start,n),"foreign seat snapshot restores");
  for(unsigned t=0;t<30;++t) {frame(0);rush_keep_players_alive();}
  check(!memcmp(first,g_ram,sizeof(first)),"foreign seat replay is deterministic");
  MmxBossRushForeignSetController(23,0);MmxCartExtensionClear();
  check(!memcmp(cart->rom+0x380000,cart->rom+0x180000,0x8000),"clearing restores the mirrored bank");
  puts("MMX BOSS RUSH FOREIGN SPIKE CHECKS PASSED");
}
#include "mmx_boss_rush_rhino_test.inc"
#include "mmx_boss_rush_sponge_test.inc"
static uint8_t rush_extension_value;
static void rush_extension_reset(uint8_t *ram) {(void)ram;rush_extension_value=0;}
static void rush_extension_save(uint8_t out[]) {out[0]='R';out[1]=rush_extension_value;}
static void rush_extension_load(const uint8_t in[]) {if(in[0]=='R')rush_extension_value=in[1];}
static void rush_state_format_checks(uint8_t *start,uint8_t *legacy,uint8_t *actual,size_t cap) {
  static const MmxZeroExtension extension={.state_reset=rush_extension_reset,
      .state_save=rush_extension_save,.state_load=rush_extension_load};
  MmxZeroSetExtension(&extension);rush_extension_value=7;
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"combined Zero/Rush snapshot saves");
  size_t rush_bytes=sizeof(MmxBossRushState),audio_bytes=sizeof(MmxBossRushAudioState),
      visual_bytes=sizeof(MmxBossRushVisualState),bounds_bytes=sizeof(MmxCoopViewBoundsState),
      roster_bytes=sizeof(MmxBossRushRosterState),scratch_bytes=sizeof(MmxBossRushScratchState);
  size_t v21_bytes=MMX_ZERO_EXTENSION_STATE_BYTES+rush_bytes+audio_bytes+visual_bytes;
  size_t tail=n-v21_bytes-bounds_bytes-roster_bytes-scratch_bytes;
  size_t chunk=0;
  for(size_t i=n-8;i>8;--i) {
    uint32_t magic;memcpy(&magic,start+i,4);
    if(magic==0x4d4d5854u) {chunk=i;break;}
  }
  check(chunk && tail>chunk,"bounded game state tail located");
  uint32_t version;memcpy(&version,start+chunk+4,4);
  check(version==24,"combined state uses v24 with the Boss Rush roster and scratch RAM");
  MmxBossRushState original=MmxBossRushGetState();
  /* 0: main v18 (extension only); 1-3: Rush previews v18/v19/v20;
   * 4: v21 extension + Rush; 5: v22 adds per-seat view bounds; 6: v23 adds
   * the roster. */
  for(unsigned format=0;format<7;++format) {
    memcpy(legacy,start,n);size_t length=n;
    version=format?format+17:18;
    if(!format) length=tail+MMX_ZERO_EXTENSION_STATE_BYTES; /* main v18 */
    else if(format<4) {
      size_t bytes=rush_bytes+(format>=2?audio_bytes:0)+(format>=3?visual_bytes:0);
      memmove(legacy+tail,start+tail+MMX_ZERO_EXTENSION_STATE_BYTES,bytes);
      length=tail+bytes; /* original Rush v18/v19/v20 */
    } else length=tail+v21_bytes+(format>=5?bounds_bytes:0)+(format>=6?roster_bytes:0);
    memcpy(legacy+chunk+4,&version,4);rush_extension_value=99;
    check(RtlLoadSnapshotFromMemory(legacy,length),"main/preview/current memory state imports");
    check(rush_extension_value==(format==0 || format>=4?7:0),
        "only main/current states restore the Zero extension blob");
    MmxBossRushState loaded=MmxBossRushGetState();
    check(format?!memcmp(&original,&loaded,sizeof(loaded)):loaded.mode==MMX_RUSH_OFF,
        "old main v18 has no Rush state; each preview keeps its queue");
    if(format && format<6) {
      MmxBossRushRosterState imported=MmxBossRushRosterGetState();
      check(imported.available==MMX_RUSH_X1_MASK && MmxBossRushRosterMatches(&loaded,&imported),
          "pre-roster states import an X1-only roster");
    }
    if(!format) {
      FILE *f=fopen("rush-main-v18.sav","wb");check(f!=NULL,"main v18 file opens");
      check(fwrite(legacy,1,length,f)==length && !fclose(f),"main v18 file writes");
      rush_extension_value=99;
      check(RtlLoadSnapshot("rush-main-v18.sav") && rush_extension_value==7,
          "main v18 file restores its extension without misreading Rush");
    }
  }
  check(RtlLoadSnapshotFromMemory(start,n),"current combined state restores");
  size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(start,n,actual,an,"combined extension/Rush immediate restore is byte exact");
  MmxZeroSetExtension(NULL);
  puts("MMX BOSS RUSH STATE FORMAT CHECKS PASSED");
}
static void rush_replacement_menu_checks(uint8_t *start,uint8_t *expected,size_t cap) {
  MmxBossRushState s=MmxBossRushGetState();
  static const uint8_t queue[]={0,4,2,5,1,3,6,7};
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"replacement fixture queues original and next boss pairs");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
      "replacement fixture reaches native combat");
  if(getenv("MMX_BOSS_RUSH_PAUSE_VISUAL_TEST")) {
    for(unsigned t=0;t<24;++t) {frame(SNES_PAD_LEFT|SNES_PAD_Y);rush_keep_players_alive();}
    size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"moving player pause fixture saves");
    MmxBossRushReset();frame(SNES_PAD_START);zero_replay(90);
    unsigned camera_x=eagle_word(g_ram+0x1e4d),camera_y=eagle_word(g_ram+0x1e50);
    static uint32_t native[256*224],paused[256*224];
    check(MmxRendererDraw(native,(MmxRenderView){256,0,4.0/3.0},true),"native pause reference renders");
    check(MmxRendererSaveCapture("rush-pause-native-reference.cap"),"native pause reference saves");
    check(RtlLoadSnapshotFromMemory(start,n),"restore combat before Rush pause");
    frame(SNES_PAD_START);rush_pause_frames("opening");
    fprintf(stderr,"pause camera native=%u,%u rush=%u,%u preview=%u:%u at=%u,%u visible=%u pose=%u gates=%u:%u/%u/%u/%u stage=%u\n",
        camera_x,camera_y,eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),
        g_ram[0x1988],g_ram[0x1989],eagle_word(g_ram+0x198d),eagle_word(g_ram+0x1990),
        g_ram[0x1996],g_ram[0x199f]&127,g_ram[0xd3],g_ram[0xd4],g_ram[0x1f10],g_ram[0x1f19],g_ram[0xc3],MmxWidePolicy_IsStageScene(g_ram));
    check(MmxRendererSaveCapture("rush-pause-before.cap"),"Rush pause comparison saves");
    check(eagle_word(g_ram+0x1e4d)==camera_x && eagle_word(g_ram+0x1e50)==camera_y,
        "Rush preserves the native menu camera for X's centered idle preview");
    check(MmxRendererDraw(paused,(MmxRenderView){256,0,4.0/3.0},true) &&
        !memcmp(native,paused,sizeof(native)),"Rush pause matches the native menu pixels including X's idle preview");
    size_t en=RtlSaveSnapshotToMemory(expected,cap);frame(0);
    check(RtlLoadSnapshotFromMemory(expected,en),"native pause camera snapshot restores");
    frame(SNES_PAD_START);rush_pause_frames("closing");s=MmxBossRushGetState();
    check(MmxWidePolicy_IsStageScene(g_ram) && eagle_word(g_ram+0x1e4d)>=s.camera_x &&
        eagle_word(g_ram+0x1e4d)<=s.camera_x+MMX_RUSH_MAMMOTH_SCROLL &&
        eagle_word(g_ram+0x1e58)-eagle_word(g_ram+0x1e56)==MMX_RUSH_MAMMOTH_SCROLL &&
        eagle_word(g_ram+0x1e50)==s.camera_y,"closing pause restores the scrolling Rush arena camera");
    puts("MMX BOSS RUSH NATIVE PAUSE CHECKS PASSED");return;
  }
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"two-boss replacement fixture saves");
  for(unsigned seat=0;seat<2;++seat) {
    check(RtlLoadSnapshotFromMemory(start,n),"restore both live bosses before replacement");
    s=MmxBossRushGetState();s.queue[0]=seat?5:2;s.queue[1]=seat?2:5;
    check(MmxBossRushSetState(&s),"next pair swaps Armadillo between both seats");
    for(unsigned i=0;i<2;++i) g_ram[s.bosses[i].object+0x27]=0;
    frame(0);
    check(MmxBossRushGetState().defeated==2,"simultaneous boss deaths count independently");
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[seat].id==2 && s.bosses[seat].phase==MMX_RUSH_FIGHTING) break;
    }
    unsigned object=s.bosses[seat].object;
    const MmxSpriteAsset *art=MmxRenderAssetsRushSprite(3,0x62);
    check(s.bosses[seat].id==2 && s.bosses[seat].phase==MMX_RUSH_FIGHTING && art,
        "both-dead replacement starts the original Armadillo controller");
    char name[80];snprintf(name,sizeof(name),"rush-armadillo-replacement-%u.cap",seat);
    check(MmxRendererSaveCapture(name),"replacement Armadillo native colors capture saves");
    fprintf(stderr,"replacement Armadillo seat=%u object=%x palette=%u normal=%u original=%u\n",
        seat,object,g_ram[object+0x11]&14,g_ram[object+0x35],art->attributes&14);
    check(g_ram[object+0x35]==(art->attributes&14),
        "replacement Armadillo retains its own native normal palette");
    size_t en=RtlSaveSnapshotToMemory(expected,cap);frame(0);
    check(RtlLoadSnapshotFromMemory(expected,en),"replacement native palette snapshot restores");
    check(g_ram[object+0x35]==(art->attributes&14),"Armadillo normal palette survives snapshot restore");
  }
  puts("MMX BOSS RUSH REPLACEMENT PALETTE CHECKS PASSED");
}
static unsigned rush_large_health_items(void) {
  unsigned count=0;
  for(unsigned d=0x1628;d<0x1928;d+=48)
    count+=g_ram[d] && g_ram[d+10]==2 && !(g_ram[d+11]&127) && MmxBossRushOwner(d)<0;
  return count;
}
static void rush_drop_checks(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  MmxBossRushState s=MmxBossRushGetState();
  static const uint8_t queue[]={0,7,1,3,2,4,5,6};
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"drop fixture selects Penguin and Mammoth");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,"drop fixture reaches native combat");
  eagle_putword(g_ram+0xbad,s.camera_x+32);
  if(MmxCoopEnabled()) {
    MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i)eagle_putword(c.players[i].body+5,s.camera_x+32+i*32);
    MmxCoopSetState(&c);memcpy(g_ram+0xba8,c.players[c.current].body,sizeof(c.players[c.current].body));
  }
  for(unsigned i=0;i<2;++i) {
    unsigned d=s.bosses[i].object;g_ram[d+0x27]=0;
    eagle_putword(g_ram+d+5,s.camera_x+190+i*80);eagle_putword(g_ram+d+8,s.camera_y+100);
  }
  frame(0);s=MmxBossRushGetState();
  check(s.defeated==2 && s.bosses[0].phase==MMX_RUSH_DYING && s.bosses[1].phase==MMX_RUSH_DYING,
      "simultaneous native deaths count each boss once");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"pending boss drops save");
  size_t drop_result=0;
  for(unsigned replay=0;replay<2;++replay) {
    if(replay)check(RtlLoadSnapshotFromMemory(start,n),"pending boss drops restore");
    for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
    check(rush_large_health_items()==2,"each boss creates exactly one unowned large native health item");
    if(!replay) {
      check(MmxRendererSaveCapture("rush-health-drops.cap"),"two native health drops capture saves");
      drop_result=RtlSaveSnapshotToMemory(expected,cap);check(drop_result,"boss drop result saves");
    } else {
      size_t an=RtlSaveSnapshotToMemory(actual,cap);
      same(expected,drop_result,actual,an,"simultaneous health drops and replacement controllers replay exactly");
    }
  }
  unsigned item=0;
  for(unsigned d=0x1628;d<0x1928;d+=48)
    if(g_ram[d] && g_ram[d+10]==2 && MmxBossRushOwner(d)<0) {item=d;break;}
  check(item!=0,"large health item is available to collect");
  if(MmxCoopEnabled()) {
    MmxCoopCapture(g_ram);MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) {
      unsigned hp=i?20:8;
      c.players[i].body[0x27]=(uint8_t)(128|hp);c.players[i].body[0x2b]=120;
      c.players[i].body[2]=c.players[i].body[3]=0;
      c.players[i].zero.hp[c.players[i].zero.active_x]=(uint8_t)hp;
      eagle_putword(c.players[i].body+5,i?s.camera_x+32:eagle_word(g_ram+item+5));
      eagle_putword(c.players[i].body+8,eagle_word(g_ram+item+8)+16);
    }
    MmxCoopSetState(&c);memcpy(g_ram+0xba8,c.players[c.current].body,sizeof(c.players[c.current].body));
  } else {
    g_ram[0xbcf]=0x88;g_ram[0xbd3]=120;g_ram[0xbaa]=g_ram[0xbab]=0;
    eagle_putword(g_ram+0xbad,eagle_word(g_ram+item+5));eagle_putword(g_ram+0xbb0,eagle_word(g_ram+item+8)+16);
    MmxZeroState z=MmxZeroGetState();z.hp[z.active_x]=8;MmxZeroSetState(z);
  }
  zero_replay(90);
  MmxCoopState c=MmxCoopGetState();
  unsigned hp=MmxCoopEnabled()?c.players[0].body[0x27]&127:g_ram[0xbcf]&127;
  fprintf(stderr,"boss drop collection hp=%u partner=%u item=%u\n",hp,c.players[1].body[0x27]&127,g_ram[item]);
  check(!g_ram[item] && hp==16,"native large health drop heals its collector by eight");
  if(MmxCoopEnabled())check((c.players[1].body[0x27]&127)==20,"one shared pickup heals only the native collector");
  check(RtlLoadSnapshotFromMemory(start,n),"pool pressure fixture restores pending deaths");
  s=MmxBossRushGetState();
  for(unsigned d=0x1628;d<0x1928;d+=48) {
    memset(g_ram+d,0,48);g_ram[d]=1;g_ram[d+10]=2;s.owner[(d-0xe68)/32]=0;
  }
  for(unsigned i=0;i<2;++i) {s.bosses[i].ticks=47;s.bosses[i].death_x=i?600:-100;s.bosses[i].death_y=i?300:-100;}
  check(MmxBossRushSetState(&s),"full native item pool installs");MmxBossRushFrame(g_ram,0);
  s=MmxBossRushGetState();
  check(s.bosses[0].phase==MMX_RUSH_DYING && s.bosses[1].phase==MMX_RUSH_DYING,
      "a full item pool keeps both rewards pending without consuming a new boss");
  memset(g_ram+0x1628,0,48);MmxBossRushFrame(g_ram,0);s=MmxBossRushGetState();
  check(s.bosses[0].phase==MMX_RUSH_ENTERING && s.bosses[1].phase==MMX_RUSH_DYING &&
      eagle_word(g_ram+0x162d)==s.camera_x+16 && eagle_word(g_ram+0x1630)==s.camera_y+16,
      "one freed slot releases one reward and clamps an off-screen death into the arena");
  memset(g_ram+0x1658,0,48);n=RtlSaveSnapshotToMemory(start,cap);
  MmxBossRushFrame(g_ram,0);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"last pending reward snapshot restores");
  MmxBossRushFrame(g_ram,0);size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"pool-full reward retry replays exactly without duplicate drops");
  s=MmxBossRushGetState();
  check(s.bosses[1].phase==MMX_RUSH_ENTERING && eagle_word(g_ram+0x165d)==s.camera_x+352 &&
      eagle_word(g_ram+0x1660)==s.camera_y+184,"second reward stays reachable at the arena's opposite edge");
  puts("MMX BOSS RUSH NATIVE HEALTH DROP CHECKS PASSED");
}
/* A native boss's death scene makes the player invulnerable until the stage
 * ends. Kill Penguin with a real shot, collect his drop, then require that
 * Chameleon can still hurt the player. */
static void rush_death_latch_checks(void) {
  MmxBossRushState s=MmxBossRushGetState();
  static const uint8_t queue[]={0,5,1,3,2,4,6,7};
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"death latch fixture queues Penguin and Chameleon");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].id==0 && s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].id==5,
      "death latch fixture reaches combat");
  unsigned penguin=s.bosses[0].object,item=0;g_ram[penguin+0x27]=1;
  for(unsigned t=0;t<600;++t) {
    if(s.bosses[0].id==0 && s.bosses[0].phase==MMX_RUSH_FIGHTING)
      for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
        memcpy(g_ram+d+5,g_ram+penguin+5,2);memcpy(g_ram+d+8,g_ram+penguin+8,2);
      }
    frame(t<200 && t%20==0?SNES_PAD_Y:0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(!item) for(unsigned d=0x1628;d<0x1928;d+=48)
      if(g_ram[d] && g_ram[d+10]==2 && MmxBossRushOwner(d)<0) {
        item=d;g_ram[0xbcf]=0x88;
        eagle_putword(g_ram+0xbad,eagle_word(g_ram+item+5));eagle_putword(g_ram+0xbb0,eagle_word(g_ram+item+8)+16);
        break;
      }
  }
  check(item && !g_ram[item],"Penguin's drop is collected after his native death");
  g_ram[0xbcf]=0x90;unsigned hits=0,hp=16;
  for(unsigned t=0;t<300;++t) {
    s=MmxBossRushGetState();unsigned d=s.bosses[1].object;
    eagle_putword(g_ram+d+5,eagle_word(g_ram+0xbad));eagle_putword(g_ram+d+8,eagle_word(g_ram+0xbb0)-8);
    frame(0);
    if((g_ram[0xbcf]&127)<hp) ++hits;
    hp=g_ram[0xbcf]&127;
    if(hp<4) {g_ram[0xbcf]=0x90;hp=16;}
  }
  fprintf(stderr,"death latch: contact hits=%u bd8=%u c32=%u\n",hits,g_ram[0xbd8],g_ram[0xc32]);
  check(hits>=2,"the player stays hittable after another boss's native death");
  /* A hit during a pickup's refill leaves the hurt latches set without the
   * countdown that clears them (playtest 2026-10-18): Rush starts it. */
  for(unsigned t=0;t<120 && (g_ram[0xbaa]==0x0e || g_ram[0xc13]);++t) frame(0);
  g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=0;g_ram[0xbcf]=0x90;hits=0;hp=16;
  bool cleared=false;
  for(unsigned t=0;t<300;++t) {
    s=MmxBossRushGetState();unsigned d=s.bosses[1].object;
    if(cleared) {eagle_putword(g_ram+d+5,eagle_word(g_ram+0xbad));eagle_putword(g_ram+d+8,eagle_word(g_ram+0xbb0)-8);}
    frame(0);
    cleared|=!g_ram[0xbd8] && !g_ram[0xc32];
    if((g_ram[0xbcf]&127)<hp) ++hits;
    hp=g_ram[0xbcf]&127;
    if(hp<4) {g_ram[0xbcf]=0x90;hp=16;}
  }
  fprintf(stderr,"death latch: refill latches cleared=%u hits=%u\n",cleared,hits);
  check(cleared && hits>=1,"hurt latches left by an interrupted hurt reaction clear");
  puts("MMX BOSS RUSH DEATH LATCH CHECKS PASSED");
}
static void rush_chameleon_replacement_checks(uint8_t *snapshot,size_t cap) {
  size_t n=RtlSaveSnapshotToMemory(snapshot,cap);check(n,"Chameleon replacement baseline saves");
  for(unsigned seat=0;seat<2;++seat) {
    check(RtlLoadSnapshotFromMemory(snapshot,n),"Chameleon replacement baseline restores");
    MmxBossRushState s=MmxBossRushGetState();
    s.queue[0]=seat?0:6;s.queue[1]=seat?6:0;s.queue[2]=5;s.queued=3;
    check(MmxBossRushSetState(&s),"Eagle queues ahead of replacement Chameleon");
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[seat].phase==MMX_RUSH_FIGHTING) break;
    }
    check(s.bosses[seat].id==6 && s.bosses[seat].phase==MMX_RUSH_FIGHTING,"Eagle reaches native combat");
    g_ram[s.bosses[seat].object+0x27]=0;frame(0);
    unsigned visible=0;int min_x=65535,max_x=0,min_y=65535,max_y=0;
    for(unsigned t=0;t<1800;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[seat].id!=5 || !s.bosses[seat].phase) continue;
      unsigned d=s.bosses[seat].object;
      int x=(int)eagle_word(g_ram+d+5)-s.camera_x,y=(int)eagle_word(g_ram+d+8)-s.camera_y;
      if(x<min_x)min_x=x;if(x>max_x)max_x=x;if(y<min_y)min_y=y;if(y>max_y)max_y=y;
      MmxBossRushVisualState v=MmxBossRushVisualGetState();
      const MmxSpriteAsset *art=MmxRenderAssetsRushSprite(2,0x23);
      if(s.bosses[seat].phase==MMX_RUSH_FIGHTING && v.initialized[seat] && art &&
          !memcmp(v.colors[seat],art->colors,sizeof(art->colors)) &&
          (int)eagle_word(g_ram+d+5)-(int)eagle_word(g_ram+0x1e4d)>=24 &&
          (int)eagle_word(g_ram+d+5)-(int)eagle_word(g_ram+0x1e4d)<=232) {
        if(!visible) {char path[80];snprintf(path,sizeof(path),"rush-chameleon-replacement-%u.cap",seat);check(MmxRendererSaveCapture(path),"visible replacement Chameleon capture saves");}
        ++visible;
      }
    }
    fprintf(stderr,"Chameleon replacement seat=%u visible-frames=%u bounds=%d,%d..%d,%d\n",seat,visible,min_x,min_y,max_x,max_y);
    check(min_x>=16 && max_x<=352 && min_y>=16 && max_y<=208,
        "replacement Chameleon's body stays within the full room");
    check(visible>30,"replacement Chameleon appears inside the arena in either boss seat");
  }
  puts("MMX BOSS RUSH CHAMELEON REPLACEMENT CHECKS PASSED");
}
static void rush_visual_checks(uint8_t *snapshot,size_t cap) {
  MmxBossRushState s=MmxBossRushGetState();
  static const uint8_t queue[]={2,5,0,1,3,4,6,7};
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"visual fixture queues Armadillo and Chameleon");
  unsigned roll_seen=0,fade_frames=0;bool camouflage=false,black=false,restored=false,snapshot_checked=false;
  MmxBossRushVisualState previous={0};
  for(unsigned t=0;t<1800;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    unsigned arm=s.bosses[0].object,cham=s.bosses[1].object;
    if(arm && g_ram[arm+22]==0x63 && g_ram[arm+14]) {
      unsigned pose=g_ram[arm+23]&127;
      const MmxSpriteAsset *art=MmxRenderAssetsRushObjectSprite(g_ram,arm,3,0x63);
      check(art && art->id==0x5e && art->tile_base==0x40,"native rolling form resolves its second graphics resource");
      check(pose<4,"native Armadillo roll has four authored rotation frames");
      if(!(roll_seen&(1u<<pose))) {
        char name[64];snprintf(name,sizeof(name),"rush-roll-%u.cap",pose);
        check(MmxRendererSaveCapture(name),"native roll capture saves");
      }
      roll_seen|=1u<<pose;
    }
    MmxBossRushVisualState visual=MmxBossRushVisualGetState();
    const MmxSpriteAsset *base=MmxRenderAssetsRushSprite(2,0x23);
    if(!camouflage && s.bosses[1].phase==MMX_RUSH_FIGHTING && visual.initialized[1] &&
        base && !memcmp(visual.colors[1],base->colors,sizeof(base->colors))) {
      /* Select the native disappear state, then leave all timing and poses
       * to the original controller. This avoids waiting for its RNG choice. */
      g_ram[cham+2]=2;g_ram[cham+3]=0;g_ram[cham+0x33]=0;
      camouflage=true;
    }
    if(camouflage && previous.initialized[1] && visual.initialized[1]) {
      bool changed=false,empty=true;
      for(unsigned c=0;c<16;++c) {
        empty&=visual.colors[1][c]==0;
        for(unsigned shift=0;shift<15;shift+=5) {
          int delta=((visual.colors[1][c]>>shift)&31)-((previous.colors[1][c]>>shift)&31);
          check(delta>=-1 && delta<=1,"native camouflage changes each RGB channel by at most one step");
          changed|=delta!=0;
        }
      }
      if(changed) {
        ++fade_frames;
        if(fade_frames==8 || fade_frames==24 || fade_frames==48) {
          char name[64];snprintf(name,sizeof(name),"rush-camouflage-%u.cap",fade_frames);
          check(MmxRendererSaveCapture(name),"native camouflage capture saves");
        }
      }
      if(empty && !black) check(MmxRendererSaveCapture("rush-camouflage-black.cap"),"fully camouflaged capture saves");
      black|=empty;
      if(black && !restored && !memcmp(visual.colors[1],base->colors,sizeof(base->colors))) {
        restored=true;
        check(MmxRendererSaveCapture("rush-camouflage-restored.cap"),"restored camouflage capture saves");
      }
      if(fade_frames==12 && !snapshot_checked) {
        snapshot_checked=true;
        size_t len=RtlSaveSnapshotToMemory(snapshot,cap);check(len!=0,"camouflage snapshot saves");
        for(unsigned n=0;n<8;++n) {frame(0);rush_keep_players_alive();}
        check(RtlLoadSnapshotFromMemory(snapshot,len),"camouflage snapshot loads");
        MmxBossRushVisualState loaded=MmxBossRushVisualGetState();
        check(!memcmp(&loaded,&visual,sizeof(visual)),"snapshot restores exact native camouflage colors");
      }
    }
    previous=visual;
    if(roll_seen==15 && black && restored && fade_frames>32) break;
  }
  check(roll_seen==15,"all four native rolling frames retain their graphics");
  check(camouflage && black && restored && fade_frames>32,"Chameleon fades out completely and restores his native palette gradually");
  puts("MMX BOSS RUSH NATIVE VISUAL CHECKS PASSED");
}
/* Private diagnostic captures of native Octopus/Eagle arrangements. */
static void rush_air_visual_trace(void) {
  MmxBossRushState s=MmxBossRushGetState();
  static const uint8_t queue[]={3,6,0,1,2,4,5,7};
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"visual fixture queues Octopus and Eagle");
  FILE *log=fopen("rush-air.csv","w");check(log!=NULL,"attack trace opens");
  fputs("frame,boss,object,kind,state,substate,step,visible,animation,pose,data,table,base,attributes,resource,x,y\n",log);
  unsigned keys[128]={0};bool captures[2][256][128]={0};
  for(unsigned t=0;t<3600;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    for(unsigned d=0xe68;d<0x1d08;) {
      if(d==0x1228) {d=0x1428;continue;}
      unsigned step=d<0x1628?64:d<0x1928?48:32;
      int owner=MmxBossRushOwner(d);
      if(owner>=0 && g_ram[d]) {
        unsigned n=(d-0xe68)/32,animation=g_ram[d+22],pose=g_ram[d+23]&127;
        unsigned key=animation<<24|pose<<16|g_ram[d+17]<<8|g_ram[d+14];
        if(key!=keys[n]) {
          keys[n]=key;
          const MmxSpriteAsset *a=MmxRenderAssetsRushObjectSprite(g_ram,d,kMmxBossRushBosses[s.bosses[owner].id].stage,animation);
          fprintf(log,"%u,%u,%04x,%u,%u,%u,%u,%u,%u,%u,%u,%04x,%u,%u,%u,%u,%u\n",t,s.bosses[owner].id,d,g_ram[d+10],g_ram[d+1],g_ram[d+2],g_ram[d+3],g_ram[d+14],animation,pose,g_ram[d+16],g_ram[d+49]|g_ram[d+50]<<8,g_ram[d+24],g_ram[d+17],a?a->id:255,g_ram[d+5]|g_ram[d+6]<<8,g_ram[d+8]|g_ram[d+9]<<8);
          unsigned x=g_ram[d+5]|g_ram[d+6]<<8;
          bool visible_dash=d==s.bosses[owner].object && animation==0x8a && pose>=32 &&
              x>s.camera_x+32 && x<s.camera_x+192;
          bool target=(s.bosses[owner].id==3 && animation==0x77 && t>1000) ||
              (s.bosses[owner].id==6 && ((animation==0x89 && pose>=25) || visible_dash));
          if(g_ram[d+14] && !captures[owner][animation][pose] && target) {
            captures[owner][animation][pose]=true;
            char name[80];snprintf(name,sizeof(name),"rush-air-%u-%02x-%u.cap",s.bosses[owner].id,animation,pose);
            check(MmxRendererSaveCapture(name),"native attack diagnostic capture saves");
            snprintf(name,sizeof(name),"rush-air-%u-%02x-%u.ram",s.bosses[owner].id,animation,pose);
            FILE *dump=fopen(name,"wb");check(dump && fwrite(g_ram,sizeof(g_ram),1,dump)==1 && !fclose(dump),"native attack diagnostic RAM saves");
          }
        }
      }
      d+=step;
    }
  }
  fclose(log);
  for(unsigned p=0;p<16;++p) check(captures[0][0x77][p],"native tornado exposes every authored expansion frame");
  for(unsigned p=25;p<=27;++p) check(captures[1][0x89][p],"native wing sequence exposes each extended wing pose");
  check(captures[1][0x8a][32] && captures[1][0x8a][33],"native dive exposes both poses on screen");
  puts("MMX BOSS RUSH OCTOPUS EAGLE VISUAL CHECKS PASSED");
}
static void rush_visual_bmp(const char *path) {
  uint32_t pixels[256*224];
  check(MmxRendererDraw(pixels,(MmxRenderView){256,0,4.0/3.0},true),"forced visual frame renders");
  FILE *f=fopen(path,"wb");check(f!=NULL,"visual image opens");
  uint32_t header[]={54+sizeof(pixels),0,54,40,256,224,0x200001,0,sizeof(pixels),0,0,0,0};
  check(fwrite("BM",2,1,f)==1 && fwrite(header,sizeof(header),1,f)==1,"visual image header writes");
  bool ok=true;for(int y=223;y>=0;--y) ok&=fwrite(pixels+y*256,256*4,1,f)==1;
  check(!fclose(f) && ok,"visual image rows write and close");
}
static void rush_forced_visual_checks(uint8_t *snapshot,size_t cap) {
  const char *pair=getenv("MMX_BOSS_RUSH_FORCE_VISUAL");unsigned first=7,second=1;
  if(pair) check(sscanf(pair,"%u,%u",&first,&second)==2 && first<8 && second<8 && first!=second,
      "forced visual pair selects distinct bosses");
  MmxBossRushState s=MmxBossRushGetState();s.queue[0]=(uint8_t)first;s.queue[1]=(uint8_t)second;
  unsigned q=2;for(unsigned i=0;i<8;++i) if(i!=first && i!=second) s.queue[q++]=(uint8_t)i;
  s.queued=8;check(MmxBossRushSetState(&s),"forced visual queue installs");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
      "forced visual fixture completes both native introductions");
  for(unsigned t=0;t<120;++t) {frame(0);rush_keep_players_alive();}
  size_t saved=RtlSaveSnapshotToMemory(snapshot,cap);check(saved!=0,"forced visual baseline saves");
  bool seen[8][256][128]={0};unsigned samples[8]={0};
  FILE *log=fopen("rush-forced-visual.csv","w");check(log!=NULL,"forced visual trace opens");
  fputs("seat,boss,attack,frame,object,kind,animation,pose,resource,attributes,data,table,base\n",log);
  for(unsigned seat=0;seat<2;++seat) {
    unsigned boss=seat?second:first;
    /* Native combat dispatch tables: Penguin $81:B6F3 (6), Mandrill
     * $88:9E2C (6), Chameleon $88:87BD (7), Mammoth $87:9394 (4).
     * Start each original controller at substate zero and let it run. */
    unsigned attacks=boss==7?4:boss==5?7:6;
    check(boss==0 || boss==1 || boss==5 || boss==7,"forced visual boss has a verified combat table");
    for(unsigned attack=0;attack<attacks;++attack) {
      check(RtlLoadSnapshotFromMemory(snapshot,saved),"forced native attack restores its clean baseline");
      s=MmxBossRushGetState();unsigned body=s.bosses[seat].object;
      g_ram[body+1]=4;g_ram[body+2]=(uint8_t)(attack*2);g_ram[body+3]=0;
      for(unsigned t=0;t<420;++t) {
        frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
        if(t==40 || t==100 || t==220) {
          char name[96];snprintf(name,sizeof(name),"rush-forced-%u-%u-%u.bmp",boss,attack*2,t);
          rush_visual_bmp(name);
        }
        for(unsigned d=0xe68;d<0x1d08;) {
          if(d==0x1228) {d=0x1428;continue;}
          unsigned step=d<0x1628?64:d<0x1928?48:32;
          int owner=MmxBossRushOwner(d);
          if(owner==(int)seat && g_ram[d] && g_ram[d+14]) {
            unsigned animation=g_ram[d+22],pose=g_ram[d+23]&127;
            if(!seen[boss][animation][pose]) {
              seen[boss][animation][pose]=true;
              const MmxSpriteAsset *art=MmxRenderAssetsRushObjectSprite(g_ram,d,kMmxBossRushBosses[boss].stage,animation);
              check(art!=NULL,"every forced native boss and attack pose resolves private graphics");
              fprintf(log,"%u,%u,%u,%u,%04x,%u,%u,%u,%u,%u,%u,%04x,%u\n",seat,boss,attack*2,t,d,g_ram[d+10],animation,pose,art?art->id:255,g_ram[d+17],g_ram[d+16],g_ram[d+49]|g_ram[d+50]<<8,g_ram[d+24]);
              if(art) {
                char name[96];snprintf(name,sizeof(name),"rush-art-%u-%02x-%u.bin",boss,animation,pose);
                FILE *out=fopen(name,"wb");check(out!=NULL,"native pose art opens");
                check(fwrite(art->tiles,sizeof(art->tiles),1,out)==1 &&
                    fwrite(art->colors,sizeof(art->colors),1,out)==1 &&
                    fwrite(&art->id,3,1,out)==1 && !fclose(out),"native pose graphics and palette save");
                ++samples[boss];
                snprintf(name,sizeof(name),"rush-pose-%u-%02x-%u.bmp",boss,animation,pose);
                rush_visual_bmp(name);
              }
            }
          }
          d+=step;
        }
      }
    }
    check(samples[boss]>10,"forced native attacks expose multiple body and effect poses");
    if(boss==0) check(seen[0][0x67][21] && seen[0][0x67][32] &&
        seen[0][0x68][0] && seen[0][0x68][3],"forced Penguin checks include ice beads, breath, and statue growth");
    if(boss==1) check(seen[1][0x91][25] && seen[1][0x91][26] && seen[1][0x91][9],
        "forced Mandrill checks include electric orb and charged-body poses");
    if(boss==7) check(seen[7][6][8] && seen[7][6][50] && seen[7][6][64],
        "forced Mammoth checks include trunk, oil, and fireball poses");
    if(boss==5) check(seen[5][0x23][10] && seen[5][0x23][30],
        "forced Chameleon checks include tongue and spike poses");
  }
  fclose(log);puts("MMX BOSS RUSH FORCED VISUAL CONTROLLERS COMPLETE");
}
static unsigned rush_hit_sound_owner,rush_hit_sounds[2];
static void rush_hit_sound_hook(CpuState *cpu,uint32_t pc) {
  (void)pc;
  if(MmxBossRushOwner(cpu->D)!=(int)rush_hit_sound_owner) return;
  if((cpu->A&255)==0x11) ++rush_hit_sounds[0];
  if((cpu->A&255)==0x13) ++rush_hit_sounds[1];
}
static void rush_hit_visual_checks(uint8_t *arena,uint8_t *pair,size_t cap) {
  size_t baseline=RtlSaveSnapshotToMemory(arena,cap);
  check(baseline!=0,"hit fixture saves empty arena");
  FILE *log=fopen("rush-hits.csv","w");check(log!=NULL,"hit trace opens");
  fputs("order,seat,boss,weapon,frame,hp,timer,palette,saved_palette,cut,cut_hits,animation,pose,resource,data,table,xstate,ystate,weapon_selected,shot_state,shot_kind,shot_x,shot_y\n",log);
  for(unsigned order=0;order<2;++order) {
    check(RtlLoadSnapshotFromMemory(arena,baseline),"hit fixture restores empty arena");
    MmxBossRushState s=MmxBossRushGetState();s.queue[0]=order?7:3;s.queue[1]=order?3:7;
    unsigned q=2;for(unsigned i=0;i<8;++i) if(i!=3 && i!=7) s.queue[q++]=(uint8_t)i;
    s.queued=8;check(MmxBossRushSetState(&s),"hit fixture swaps Octopus and Mammoth seats");
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
    }
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
        "hit fixture completes both introductions");
    size_t saved=RtlSaveSnapshotToMemory(pair,cap);check(saved!=0,"hit fixture saves combat");
    for(unsigned seat=0;seat<2;++seat) for(unsigned cut=0;cut<2;++cut) {
      unsigned boss=seat==order?3:7,weapon=cut?7:boss==3?3:5;
      check(RtlLoadSnapshotFromMemory(pair,saved),"each weapon restores undamaged bosses");
      if(getenv("MMX_BOSS_RUSH_HIT_AUDIO_TEST")) {
        rush_hit_sound_owner=seat;memset(rush_hit_sounds,0,sizeof(rush_hit_sounds));
        check(interp_bridge_add_pre_opcode_hook(0x8088d9,rush_hit_sound_hook),"native sound-ring observer installs after the Rush audio hook");
      }
      s=MmxBossRushGetState();unsigned body=s.bosses[seat].object;
      const MmxSpriteAsset *base=MmxRenderAssetsRushSprite(kMmxBossRushBosses[boss].stage,g_ram[body+22]);
      check(base && g_ram[body+0x33]==(base->attributes&14),
          "native damage controller saves its own resource's normal palette");
      g_ram[body+2]=g_ram[body+3]=0;g_ram[0xbdb]=(uint8_t)(weapon*2);
      g_ram[0xbad]=(uint8_t)(s.camera_x+100);g_ram[0xbae]=(uint8_t)((s.camera_x+100)>>8);
      g_ram[0xbb0]=(uint8_t)(s.camera_y+164);g_ram[0xbb1]=(uint8_t)((s.camera_y+164)>>8);
      g_ram[0xbb9]&=~64u;
      /* The scrolling entrance can leave X in hurt/stomp recovery. Start
       * this projectile fixture from an idle shooter, including Tornado. */
      g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc2d]=0;
      bool hit=false,flashing=false,normal=false,poses[128]={0};unsigned hits=0,previous=32,last_hit=0;
      for(unsigned t=0;t<(cut?1200:180);++t) {
        /* Real native projectiles make the collision. Hold the target and
         * center active shots on it, as in the finishing-hit regression. */
        g_ram[body+5]=(uint8_t)(s.camera_x+160);g_ram[body+6]=(uint8_t)((s.camera_x+160)>>8);
        g_ram[body+8]=(uint8_t)(s.camera_y+164);g_ram[body+9]=(uint8_t)((s.camera_y+164)>>8);
        unsigned other=s.bosses[seat^1].object;
        g_ram[other+5]=(uint8_t)(s.camera_x-128);g_ram[other+6]=(uint8_t)((s.camera_x-128)>>8);
        g_ram[other+2]=g_ram[other+3]=0;
        /* Contact knockback otherwise prevents X from firing repeated cuts.
         * Disable only boss contact damage; projectile collision stays native. */
        for(unsigned d=0xe68;d<0x1e28;) {
          if(d==0x1228) {d=0x1428;continue;}
          unsigned step=d<0x1628?64:d<0x1928?48:32;
          int owner=MmxBossRushOwner(d);
          if(owner>=0 && g_ram[d]) g_ram[d+0x26]=0;
          /* The off-screen partner must not grab X through a vortex child. */
          if(owner==(int)(seat^1) && d!=other) g_ram[d]=0;
          d+=step;
        }
        g_ram[0xbad]=(uint8_t)(s.camera_x+100);g_ram[0xbae]=(uint8_t)((s.camera_x+100)>>8);
        /* Keep the shooter in X1's post-hit protection (latches plus their
         * countdown), so contact and Mammoth's stomp stun never interrupt it. */
        g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=0x3c;
        if(cut && t%90==0 && !g_ram[body+(boss==3?0x3c:0x39)]) {
          g_ram[body+2]=g_ram[body+3]=0;
          if(boss==3) g_ram[body+0x3a]=0; /* Select a hittable native behavior. */
          /* Mammoth's original stomp stuns X, preventing the next shot.
           * Reset only the fixture's shooter before each forced collision. */
          g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc2d]=0;g_ram[0xbb9]&=~64u;
        }
        for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
          memcpy(g_ram+d+5,g_ram+body+5,2);memcpy(g_ram+d+8,g_ram+body+8,2);
          g_ram[d+4]=g_ram[d+7]=0;
        }
        unsigned prior_timer=g_ram[body+(boss==3?0x3c:0x39)];
        bool prior_flash=prior_timer && !(g_ram[body+17]&14);
        frame((!hit || cut) && t%90==0?SNES_PAD_Y:0);rush_keep_players_alive();
        unsigned hp=g_ram[body+0x27]&127,timer=g_ram[body+(boss==3?0x3c:0x39)];
        if(hp<previous) {
          check(!hits || t-last_hit>=60,"native damage immunity prevents early repeat hits");
          hit=true;++hits;last_hit=t;
        }
        fprintf(log,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%04x,%u,%u,%u,%u,%u,%u,%u\n",
            order,seat,boss,weapon,t,hp,timer,g_ram[body+17]&14,g_ram[body+0x33],
            g_ram[body+0x36],g_ram[body+0x0c],g_ram[body+22],g_ram[body+23]&127,
            MmxRenderAssetsRushSprite(kMmxBossRushBosses[boss].stage,g_ram[body+22])->id,
            g_ram[body+16],g_ram[body+49]|g_ram[body+50]<<8,g_ram[0xbaa],g_ram[0xbb0]|g_ram[0xbb1]<<8,
            g_ram[0xbdb],g_ram[0x1228],g_ram[0x1232],g_ram[0x122d]|g_ram[0x122e]<<8,g_ram[0x1230]|g_ram[0x1231]<<8);
        if(hit && prior_flash && !flashing) {
          flashing=true;char name[80];snprintf(name,sizeof(name),"rush-hit-%u-%u-%u.bmp",boss,seat,weapon);
          rush_visual_bmp(name);
        }
        if(hit && prior_timer && !prior_flash && !normal) {
          normal=true;char name[80];snprintf(name,sizeof(name),"rush-hit-normal-%u-%u-%u.bmp",boss,seat,weapon);
          rush_visual_bmp(name);
        }
        unsigned pose=g_ram[body+23]&127;
        if(cut && !poses[pose] && g_ram[body+14]) {
          poses[pose]=true;char name[80];snprintf(name,sizeof(name),"rush-art-%u-%02x-%u.bin",boss,g_ram[body+22],pose);
          const MmxSpriteAsset *art=MmxRenderAssetsRushObjectSprite(g_ram,body,kMmxBossRushBosses[boss].stage,g_ram[body+22]);
          FILE *out=fopen(name,"wb");check(art && out!=NULL,"damaged boss art resolves");
          check(fwrite(art->tiles,sizeof(art->tiles),1,out)==1 && fwrite(art->colors,sizeof(art->colors),1,out)==1 &&
              fwrite(&art->id,3,1,out)==1 && !fclose(out),"damaged boss art saves");
          snprintf(name,sizeof(name),"rush-pose-%u-%02x-%u.bmp",boss,g_ram[body+22],pose);rush_visual_bmp(name);
        }
        /* Keep attacks nonfatal so the native cut counter can reach zero. */
        g_ram[body+0x27]=(uint8_t)((g_ram[body+0x27]&128)|32);previous=32;
      }
      fprintf(stderr,"hit fixture boss=%u seat=%u weapon=%u hits=%u palette=%u saved=%u cut=%u remaining=%u\n",
          boss,seat,weapon,hits,g_ram[body+17]&14,g_ram[body+0x33],g_ram[body+0x36],g_ram[body+0x0c]);
      if(getenv("MMX_BOSS_RUSH_HIT_AUDIO_TEST") && boss==7 && !cut) {
        fprintf(stderr,"Storm Tornado native hit sounds seat=%u impacts=%u hurt=%u\n",seat,rush_hit_sounds[0],rush_hit_sounds[1]);
        check(rush_hit_sounds[0] && rush_hit_sounds[1],"Storm Tornado hits on Mammoth retain native impact and hurt sounds in either slot");
        MmxBossRushAudioState audio=MmxBossRushAudioGetState();
        for(unsigned c=0;c<MMX_RUSH_AUDIO_CUES;++c)
          check(audio.cues[c].command!=0x11 && audio.cues[c].command!=0x13,
              "sustained weapon hits do not create overlapping private hit voices");
      }
      check(hit && flashing && normal,"real weapon hits advance native flashing and normal damage phases");
      if(cut) check(boss==3?g_ram[body+0x36]!=0:g_ram[body+0x36]==0,
          "native Boomerang Cutter severs Octopus tentacles or Mammoth trunk");
    }
  }
  fclose(log);puts("MMX BOSS RUSH NATIVE HIT VISUAL CHECKS PASSED");
}
static void rush_death_frame(void) {
  unsigned head=g_ram[0xba3]&30;frame(0);
  for(unsigned end=g_ram[0xba3]&30;head!=end;head=(head+2)&30)
    rush_death_sounds+=g_ram[0xb72+head]==0x0a;
}
static void rush_natural_death_checks(void) {
  check(!MmxCoopEnabled(),"native Boss Rush fatal-contact fixture uses solo play");
  MmxBossRushState s=MmxBossRushGetState();
  uint8_t queue[8];
  const char *boss=getenv("MMX_BOSS_RUSH_DEATH_BOSS");
  unsigned first=boss?(unsigned)strtoul(boss,NULL,10):0;
  check(first<8,"fatal-contact fixture selects a valid native boss");
  queue[0]=(uint8_t)first;queue[1]=(uint8_t)(first==0?4:0);unsigned pos=2;
  for(unsigned i=0;i<8;++i) if(i!=queue[0] && i!=queue[1]) queue[pos++]=(uint8_t)i;
  memcpy(s.queue,queue,sizeof(queue));s.queued=8;
  check(MmxBossRushSetState(&s),"fatal-contact fixture queues the selected boss and a survivor");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
      "fatal-contact fixture completes both native entrances");
  rush_death_sounds=0;
  g_ram[0xbcf]=129;g_ram[0xc30]=g_ram[0xc32]=g_ram[0xbd8]=0;
  for(unsigned t=0;t<120 && (g_ram[0xbcf]&127);++t) {
    memcpy(g_ram+0xbad,g_ram+s.bosses[0].object+5,2);
    memcpy(g_ram+0xbb0,g_ram+s.bosses[0].object+8,2);
    rush_death_frame();
  }
  check(!(g_ram[0xbcf]&127),"native boss contact delivers the fatal hit");
  for(unsigned t=0;t<360 && MmxBossRushGetState().mode==MMX_RUSH_PLAYING;++t) {
    rush_death_frame();
    if(t==8 || t==20 || t==40 || t==80 || t==120 || t==180) {
      char name[64];snprintf(name,sizeof(name),"rush-death-%u.cap",t);
      check(MmxRendererSaveCapture(name),"native death visual capture saves");
      fprintf(stderr,"death visual t=%u player=%u/%u/%u armor=%u:%u/%u:%u/%u:%u\n",t,
          g_ram[0xbb6],g_ram[0xbbe],g_ram[0xbbf],g_ram[0xc38],g_ram[0xc46],
          g_ram[0xc58],g_ram[0xc66],g_ram[0xc78],g_ram[0xc86]);
    }
    if(t%30==0) fprintf(stderr,"native rush death t=%u mode=%u hp=%u action=%u:%u sounds=%u flags=%u/%u/%u\n",
        t,MmxBossRushGetState().mode,g_ram[0xbcf],g_ram[0xbaa],g_ram[0xbab],rush_death_sounds,
        g_ram[0x1f13],g_ram[0x1f19],g_ram[0xc32]);
  }
  check(MmxBossRushGetState().mode==MMX_RUSH_FINISHED,"native fatal contact reaches Game Over without looping");
  check(rush_death_sounds==1,"native fatal contact plays X's death sound once");
  puts("MMX BOSS RUSH NATIVE FATAL CONTACT CHECKS PASSED");
}

static void rush_native_hit_audio_checks(uint8_t *snapshot,size_t cap) {
  size_t n=RtlSaveSnapshotToMemory(snapshot,cap);check(n,"native hit-audio baseline saves");
  for(unsigned seat=0;seat<2;++seat) for(unsigned command=0x11;command<=0x13;command+=2) {
    check(RtlLoadSnapshotFromMemory(snapshot,n),"native hit-audio baseline restores");
    check(MmxBossRushAssign(seat,7,0xe68),"hit-audio fixture owns Mammoth in each slot");
    MmxBossRushState s=MmxBossRushGetState();uint8_t native[2];
    g_ram[0xe72]=0x0c;eagle_putword(g_ram+0xe6d,eagle_word(g_ram+0xbad)+40);
    for(unsigned rush=0;rush<2;++rush) {
      s.mode=rush?MMX_RUSH_PLAYING:MMX_RUSH_PREPARING;s.actor=rush?seat+1:0;
      s.actor_object=0xe68;s.actor_s=0x1ffe;
      check(MmxBossRushSetState(&s),"native/Rush sound-call context installs");
      unsigned head=g_ram[0xba3],sequence=MmxBossRushAudioGetState().sequence;
      CpuState cpu=g_cpu;cpu.D=0xe68;cpu.DB=cpu.PB=0x80;cpu.S=0x1ffe;cpu.P=0x30;
      cpu.A=(uint16_t)command;cpu.host_return_valid=0;cpu_p_to_mirrors(&cpu);
      check(interp_bridge_run(&cpu,0x8088a2),"original positional sound helper returns");
      if(!rush) memcpy(native,g_ram+0xb72+head,2);
      else {
        fprintf(stderr,"shared hit seat=%u command=%02x native=%02x/%02x Rush=%02x/%02x\n",
            seat,command,native[0],native[1],g_ram[0xb72+head],g_ram[0xb73+head]);
        check(!memcmp(native,g_ram+0xb72+head,2),"shared hit command and pan match the original native sound request");
        check(MmxBossRushAudioGetState().sequence==sequence,"shared hit requests do not allocate private voices");
      }
    }
    MmxBossRushAudioState audio=MmxBossRushAudioGetState(),legacy=audio;
    legacy.cues[0]=(MmxBossRushAudioCue){.command=(uint8_t)command,.object=0xe68,.seat=(uint8_t)seat,.kind=0x0c};
    check(MmxBossRushAudioSetState(&legacy),"legacy impact cues remain valid in saved states");
    check(MmxBossRushAudioSetState(&audio),"audio history restores after legacy cue check");
  }
  check(RtlLoadSnapshotFromMemory(snapshot,n),"native hit-audio baseline restores after checks");
  check(!MmxBossRushAudioClip(0x11,NULL) && !MmxBossRushAudioClip(0x13,NULL),
      "shared hit sounds retain their native voice priority and retrigger behavior");
}
static void rush_audio_checks(uint8_t *snapshot,size_t cap) {
  rush_native_hit_audio_checks(snapshot,cap);
  Apu *a=apu_init();apu_reset(a);unsigned entry=0;
  const uint8_t *rom=g_snes->cart->rom;
  static const uint8_t banks[]={0,1,2,0x42,0x47};
  for(unsigned bank=0;bank<sizeof(banks);++bank) {
    const uint8_t *ptr=rom+0x48000+banks[bank]*3;
    unsigned p=(9+ptr[2])*0x8000+((ptr[0]|ptr[1]<<8)&0x7fff);
    for(;;) {
      unsigned n=rom[p]|rom[p+1]<<8,d=rom[p+2]|rom[p+3]<<8;p+=4;
      if(!n) {if(!bank) entry=d;break;}
      memcpy(a->ram+d,rom+p,n);p+=n;
    }
  }
  a->romReadable=false;a->spc->pc=entry;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==1,"ROM-derived private SPC driver initializes");
  a->dsp->sampleRead=a->dsp->sampleWrite=0;
  uint32_t count,boomerang_count;const int16_t *effect=MmxBossRushAudioClip(0x51,&count);
  const int16_t *boomerang=MmxBossRushAudioClip(0x5a,&boomerang_count);
  check(effect && count>8000 && boomerang && boomerang_count>64000,
      "native boss and sustained boomerang clips are available");
  check(!MmxBossRushAudioClip(0x0c,NULL),"meter-fill sound retains its native driver");
  a->inPorts[1]=0;a->inPorts[0]=0x51;int peak=0;
  for(unsigned sample=0;sample<count;++sample) {
    for(unsigned i=0;i<32;++i) apu_cycle(a);
    int16_t pcm[2];if(dsp_getSamples(a->dsp,pcm,1)!=1) check(false,"private native SPC sample arrives");
    if(pcm[0]!=effect[sample*2] || pcm[1]!=effect[sample*2+1])
      check(false,"cached effect matches independent native SPC output");
    for(unsigned ch=0;ch<2;++ch) {int v=abs(pcm[ch]);if(v>peak) peak=v;}
  }
  check(peak>0 && a->outPorts[2]==2,"native boss effect renders audibly");
  puts("ok: cached effect matches the complete independent native SPC output");
  const uint8_t *scripts=a->ram+0x1c02;
  unsigned current=scripts[0x51*2]<<8|scripts[0x51*2+1];
  unsigned incoming=scripts[0x5a*2]<<8|scripts[0x5a*2+1];
  check(a->ram[current]>a->ram[incoming],"native boss sound outranks the boomerang effect");
  a->inPorts[0]=0x51;
  for(unsigned i=0;i<8000*32;++i) apu_cycle(a);
  a->inPorts[0]=0x5a;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==4 && a->ram[0x2a2]==a->ram[current] && (a->ram[0xc3]&4),
      "lower-priority native request acknowledges without replacing the boss sound");
  /* The substituted command acknowledges without stopping that active voice. */
  a->inPorts[0]=0xb1;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  check(a->outPorts[2]==5 && a->ram[0x2a2]==a->ram[current] && (a->ram[0xc3]&4),
      "null native effect preserves acknowledgements and the existing voice");
  apu_free(a);
  snes_mod_audio_stop_all();
  SNESModAudioClip first=snes_mod_audio_register_pcm_s16(effect,count,32040,2);
  SNESModAudioClip second=snes_mod_audio_register_pcm_s16(boomerang,boomerang_count,32040,2);
  SNESModAudioVoice one=snes_mod_audio_play_voice(first,100),two=snes_mod_audio_play_voice(second,100);
  check(one && two,"native boss and boomerang effects can play concurrently");
  int16_t mixed[1024]={0};
  RtlApuLock();snes_mod_audio_mix(mixed,512,32040,2);RtlApuUnlock();
  for(unsigned i=0;i<1024;++i) {
    int v=(int)effect[i]+boomerang[i];if(v>32767) v=32767;if(v< -32768) v=-32768;
    if(mixed[i]!=v) check(false,"independent effects mix without channel preemption");
  }
  size_t rb_cap=RtlRollbackSnapshotBound();uint8_t *rollback=malloc(rb_cap);
  check(rollback!=NULL,"local rollback fixture allocates");
  size_t n=RtlRollbackSaveToMemory(rollback,rb_cap);check(n!=0,"local rollback audio snapshot saves");
  uint32_t generation=snes_mod_audio_reset_generation();
  check(RtlRollbackLoadFromMemory(rollback,n),"local rollback audio snapshot restores");free(rollback);
  check(snes_mod_audio_reset_generation()==generation && snes_mod_audio_voice_active(one) &&
      snes_mod_audio_voice_active(two),"local rollback preserves active mod-audio voices");
  memset(mixed,0,sizeof(mixed));
  RtlApuLock();snes_mod_audio_mix(mixed,512,32040,2);RtlApuUnlock();
  for(unsigned i=0;i<1024;++i) {
    int v=(int)effect[1024+i]+boomerang[1024+i];if(v>32767) v=32767;if(v< -32768) v=-32768;
    if(mixed[i]!=v) check(false,"rollback preserves the audio playback position");
  }
  puts("ok: independent native effects and playback positions survive local rollback");
  n=RtlSaveSnapshotToMemory(snapshot,cap);check(n!=0,"ordinary audio snapshot saves");
  check(RtlLoadSnapshotFromMemory(snapshot,n),"ordinary audio snapshot restores");
  check(!snes_mod_audio_voice_active(one) && !snes_mod_audio_voice_active(two) &&
      snes_mod_audio_reset_generation()!=generation,"ordinary save-state load stops previous audio voices");
  snes_mod_audio_unregister(first);snes_mod_audio_unregister(second);
  MmxBossRushAudioState bad=MmxBossRushAudioGetState();bad.cues[0].command=0xfa;
  check(!MmxBossRushAudioSetState(&bad),"invalid audio control requests are rejected");
  puts("MMX BOSS RUSH NATIVE AUDIO CHECKS PASSED");
}

static void rush_projectile_defeat(unsigned seat,unsigned weapon) {
  MmxBossRushState s=MmxBossRushGetState();unsigned object=s.bosses[seat].object;
  unsigned count=s.defeated;g_ram[object+0x27]=1;
  /* Fire from inside the room; shots at the new right-wall trigger can hit
   * the wall and retire before the next frame's forced collision. Configure
   * the actual port-1 shooter even when a yielded co-op call projects P2. */
  if(MmxCoopEnabled()) {
    MmxCoopCapture(g_ram);MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) {
      eagle_putword(c.players[i].body+5,s.camera_x+100+i*32);
      c.players[i].body[0x11]&=~64u;
    }
    c.players[0].body[0x33]=(uint8_t)(weapon*2);
    MmxCoopSetState(&c);memcpy(g_ram+0xba8,c.players[c.current].body,sizeof(c.players[c.current].body));
  } else {
    eagle_putword(g_ram+0xbad,s.camera_x+100);g_ram[0xbb9]&=~64u;
    g_ram[0xbdb]=(uint8_t)(weapon*2);
  }
  for(unsigned t=0;t<360 && MmxBossRushGetState().defeated==count;++t) {
    /* Give a real native weapon projectile a guaranteed collision. Reducing
     * the fixture to 1 HP saves time; the finishing hit and death are guest code. */
    for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
      memcpy(g_ram+d+5,g_ram+object+5,2);memcpy(g_ram+d+8,g_ram+object+8,2);
      g_ram[d+4]=g_ram[d+7]=0;
    }
    if(MmxCoopEnabled()) {
      MmxCoopCapture(g_ram);MmxCoopState c=MmxCoopGetState();
      /* A frame can end with either seat projected. The shooter's live
       * projectiles may therefore be in its saved body, outside guest WRAM. */
      for(unsigned p=0;p<2;++p) for(unsigned d=0;d<sizeof(c.players[p].shots);d+=64) {
        uint8_t *shot=c.players[p].shots+d;if(!shot[0]) continue;
        memcpy(shot+5,g_ram+object+5,2);memcpy(shot+8,g_ram+object+8,2);
        shot[4]=shot[7]=0;
      }
      MmxCoopSetState(&c);memcpy(g_ram+0x1228,c.players[c.current].shots,sizeof(c.players[c.current].shots));
    }
    frame(t%12==0?SNES_PAD_Y:0);rush_keep_players_alive();
  }
  s=MmxBossRushGetState();
  fprintf(stderr,"rush lethal boss=%u count=%u native=%u:%u hp=%u gates=%u/%u/%u\n",
      s.bosses[seat].id,s.defeated,g_ram[object+1],g_ram[object+2],g_ram[object+0x27],
      g_ram[0xd3],g_ram[0x1f19],MmxCoopGetState().menu_owner);
  if(s.defeated==count) {
    fprintf(stderr,"lethal fixture player=%u:%u at=%u,%u weapon=%u input=%u flags=%u/%u/%u\n",
      g_ram[0xbaa],g_ram[0xbab],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,
      g_ram[0xbdb],g_ram[0xac],g_ram[0xc0c],g_ram[0xc16],g_ram[0x1f17]);
    for(unsigned d=0x1228;d<0x1428;d+=64)
      fprintf(stderr,"shot %x: %u:%u kind=%u at=%u,%u\n",d,g_ram[d],g_ram[d+1],g_ram[d+10],
        g_ram[d+5]|g_ram[d+6]<<8,g_ram[d+8]|g_ram[d+9]<<8);
  }
  check(s.defeated==count+1,"native weapon finishing hit counts exactly one defeat");
}
#include "mmx_boss_rush_coop_combat_test.inc"
/* Developer scan of a recorded session (Tier 2 diagnostics replays):
 * MMX_REPLAY_SCAN names a text file listing .replay segments. Restore each
 * segment's opening checkpoint and print the player's hurt state and Rush's
 * bosses, so a bug can be located in a long playtest. */
static void rush_replay_scan(const char *list) {
  FILE *names=fopen(list,"r");check(names!=NULL,"replay list opens");
  static uint8_t state[2*1024*1024];char name[1400],line[256],sidecar[1500];
  while(fgets(name,sizeof(name),names)) {
    name[strcspn(name,"\r\n")]=0;if(!*name) continue;
    FILE *replay=fopen(name,"r");if(!replay) continue;
    unsigned offset=0,size=0,digest=0,frame=0;bool found=false;
    while(fgets(line,sizeof(line),replay))
      if(sscanf(line,"S %u %u %x",&offset,&size,&digest)==3) {found=true;break;}
    if(found && fgets(line,sizeof(line),replay)) sscanf(line,"I %u",&frame);
    fclose(replay);
    snprintf(sidecar,sizeof(sidecar),"%s.sav",name);
    FILE *sav=found?fopen(sidecar,"rb"):NULL;
    bool loaded=sav && size<=sizeof(state) && !fseek(sav,(long)offset,SEEK_SET) &&
        fread(state,1,size,sav)==size && RtlLoadSnapshotFromMemory(state,size);
    if(sav) fclose(sav);
    if(!loaded) {fprintf(stderr,"scan %s: cannot restore\n",name);continue;}
    MmxBossRushState s=MmxBossRushGetState();
    fprintf(stderr,"scan frame=%u mode=%u st=%02x hp=%u bd8=%02x c32=%02x c30=%02x 1f0c=%u zero=%u"
        " boss0=%u/%u obj=%04x hp=%u priv=%u boss1=%u/%u obj=%04x hp=%u priv=%u world=%u/%u actor=%u defeated=%u\n",
        frame,s.mode,g_ram[0xbaa],g_ram[0xbcf]&127,g_ram[0xbd8],g_ram[0xc32],g_ram[0xc30],g_ram[0x1f0c],
        MmxZeroActive(),s.bosses[0].id,s.bosses[0].phase,s.bosses[0].object,
        s.bosses[0].object?g_ram[s.bosses[0].object+0x27]&127:0,s.bosses[0].globals[0],
        s.bosses[1].id,s.bosses[1].phase,s.bosses[1].object,
        s.bosses[1].object?g_ram[s.bosses[1].object+0x27]&127:0,s.bosses[1].globals[0],
        s.globals[31],s.globals[39],s.actor,s.defeated);
    for(unsigned d=0x1428;d<0x1628;d+=64) if(g_ram[d])
      fprintf(stderr,"  projectile %04x class=%02x state=%02x/%02x owner=%u occupied=%u\n",d,g_ram[d+10],g_ram[d+1],
          g_ram[d+2],s.owner[(d-0xe68)/32],s.occupied[(d-0xe68)/32]);
  }
  fclose(names);
}
/* MMX_REPLAY_PLAY names one .replay segment: restore its checkpoint, feed
 * its recorded inputs on the current code (no divergence check), then idle
 * MMX_REPLAY_PLAY_EXTRA frames, printing the player's hurt state whenever it
 * changes. Shows how a fix changes a captured bug. */
static void rush_replay_play(const char *name) {
  static uint8_t state[2*1024*1024];char line[256],sidecar[1500];
  FILE *replay=fopen(name,"r");check(replay!=NULL,"replay segment opens");
  unsigned offset=0,size=0,digest=0,frame_number=0,word=0,inputs=0;bool loaded=false;
  uint8_t last[6]={0};bool first=true;
  const char *extra_text=getenv("MMX_REPLAY_PLAY_EXTRA");unsigned extra=extra_text?(unsigned)atoi(extra_text):0;
  for(;;) {
    bool have=replay && fgets(line,sizeof(line),replay);
    if(have && !loaded && sscanf(line,"S %u %u %x",&offset,&size,&digest)==3) {
      snprintf(sidecar,sizeof(sidecar),"%s.sav",name);
      FILE *sav=fopen(sidecar,"rb");
      loaded=sav && size<=sizeof(state) && !fseek(sav,(long)offset,SEEK_SET) &&
          fread(state,1,size,sav)==size && RtlLoadSnapshotFromMemory(state,size);
      if(sav) fclose(sav);
      check(loaded,"replay checkpoint restores");continue;
    }
    if(have && !(loaded && sscanf(line,"I %u %x",&frame_number,&word)==2)) continue;
    if(!have) {if(!extra--) break;word=1u<<30;}
    frame(word&~(1u<<30));++inputs;
    /* MMX_REPLAY_PLAY_WATCH: hex WRAM addresses, comma separated. */
    static unsigned watch[32],watch_count;static uint8_t watched[32];static bool parsed;
    if(!parsed) {
      parsed=true;const char *w=getenv("MMX_REPLAY_PLAY_WATCH");
      while(w && *w && watch_count<32) {watch[watch_count++]=(unsigned)strtoul(w,(char **)&w,16);if(*w==',') ++w;}
    }
    for(unsigned i=0;i<watch_count;++i) if(g_ram[watch[i]]!=watched[i] || inputs==1) {
      fprintf(stderr,"play %u %04x=%02x\n",inputs,watch[i],g_ram[watch[i]]);watched[i]=g_ram[watch[i]];
    }
    uint8_t now[6]={g_ram[0xbaa],g_ram[0xbcf]&127,g_ram[0xbd8],g_ram[0xc32],g_ram[0xc13],0};
    if(first || memcmp(now,last,5)) {
      fprintf(stderr,"play %u st=%02x hp=%u bd8=%02x c32=%02x c13=%02x\n",inputs,now[0],now[1],now[2],now[3],now[4]);
      memcpy(last,now,6);first=false;
    }
  }
  if(replay) fclose(replay);
  fprintf(stderr,"play done after %u frames: bd8=%02x c32=%02x c13=%02x\n",inputs,g_ram[0xbd8],g_ram[0xc32],g_ram[0xc13]);
}
/* MMX_BOSS_RUSH_COMBO_SOUND_TEST: Zero's attack chain (Y) on a boss
 * (MMX_COMBO_BOSS roster id, default Flame Mammoth). Every landed hit,
 * including combo follow-ups inside the boss's damage flash, must queue the
 * boss-hurt sound $13 in the native sound ring ($0B72). */
static void rush_combo_sound_checks(void) {
  const char *boss_text=getenv("MMX_COMBO_BOSS");unsigned boss=boss_text?(unsigned)atoi(boss_text):4;
  if(boss>=8) {
    MmxBossRushForeignSetPreview(true);
    MmxBossRushRosterState roster={.available=MMX_RUSH_X1_MASK|(1u<<boss),.queued=2};
    roster.queue[0]=(uint8_t)boss;roster.queue[1]=0;
    check(MmxBossRushRosterSetState(&roster),"combo sound fixture queues the relocated boss");
  } else {
    MmxBossRushState s=MmxBossRushGetState();
    uint8_t queue[]={(uint8_t)boss,boss?0:1,1,3,2,5,6,7};
    memcpy(s.queue,queue,sizeof(queue));s.queued=8;
    check(MmxBossRushSetState(&s),"combo sound fixture queues the boss");
  }
  MmxBossRushState s=MmxBossRushGetState();
  unsigned seat=2;
  for(unsigned t=0;t<2400 && seat==2;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    for(unsigned i=0;i<2;++i) if(s.bosses[i].id==boss && s.bosses[i].phase==MMX_RUSH_FIGHTING) seat=i;
  }
  check(seat<2,"combo sound fixture reaches combat");
  unsigned body=s.bosses[seat].object,hits=0,silent=0,hp=g_ram[body+0x27]&127;
  for(unsigned t=0;t<240;++t) {
    s=MmxBossRushGetState();unsigned other=s.bosses[seat^1].object;
    /* The boss stands beside the player; the other stays out of reach. */
    eagle_putword(g_ram+body+5,eagle_word(g_ram+0xbad)+20);eagle_putword(g_ram+body+8,eagle_word(g_ram+0xbb0));
    if(other) eagle_putword(g_ram+other+5,eagle_word(g_ram+0xbad)-200);
    g_ram[body+0x26]=0;if(other) g_ram[other+0x26]=0;
    g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=0x3c;g_ram[0xbb9]|=64u; /* protected, facing right */
    unsigned ring=g_ram[0xba3]&0x1e;
    frame(t%10<2?SNES_PAD_Y:0);rush_keep_players_alive();
    bool sounded=false;
    for(unsigned x=ring;x!=(g_ram[0xba3]&0x1eu);x=(x+2)&0x1e) sounded|=g_ram[0xb72+x]==0x13;
    unsigned now=g_ram[body+0x27]&127;
    if(now<hp) {
      ++hits;if(!sounded) ++silent;
      fprintf(stderr,"combo hit t=%u hp=%u->%u state=%02x/%02x hurt-sound=%u\n",t,hp,now,
          g_ram[body+1],g_ram[body+2],sounded);
    }
    hp=now;
    if(now<8) {g_ram[body+0x27]=(uint8_t)((g_ram[body+0x27]&128)|32);hp=32;}
  }
  fprintf(stderr,"combo sounds: hits=%u silent=%u\n",hits,silent);
  /* Saber Zero's priority combos land inside the damage flash; other
   * characters land one hit per flash. */
  const char *who=getenv("MMX_BOSS_RUSH_RHINO_CHARACTER");
  check(hits>=(who && !strcmp(who,"saber")?4u:1u),"the attack chain lands its hits");
  check(!silent,"every landed combo hit plays the boss-hurt sound");
  puts("MMX BOSS RUSH COMBO SOUND CHECKS PASSED");
}
#include "mmx_boss_rush_reports_test.inc"
#include "mmx_gravity_rush_test.inc"
static void rush_runtime_checks(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  rush_rhino_launcher();
  if(getenv("MMX_REPLAY_SCAN")) {rush_replay_scan(getenv("MMX_REPLAY_SCAN"));return;}
  if(getenv("MMX_REPLAY_PLAY")) {rush_replay_play(getenv("MMX_REPLAY_PLAY"));return;}
  const char *coop_assets=getenv("MMX_BOSS_RUSH_COOP");
  const char *zero_assets=getenv("MMX_BOSS_RUSH_ZERO");
  if(zero_assets) {
    check(MmxZeroLoad(zero_assets),"Add Zero source assets load");MmxZeroRegisterHooks();
    MmxZeroState zero=MmxZeroGetState();zero.active_x=1;MmxZeroSetState(zero);
  }
  if(coop_assets) {
    check(MmxZeroLoad(coop_assets),"co-op source assets load");MmxZeroRegisterHooks();
    check(MmxCoopEnable(getenv("MMX_BOSS_RUSH_COOP_P1_ZERO")?MMX_COOP_ZERO:MMX_COOP_X),"co-op configuration uses the selected P1 character");
    MmxCoopRegisterHooks();
  }
  for(unsigned t=0;t<1800 && !MmxBossRushGetState().menu;++t) {
    frame(t==200 || t==350 || (t>500 && t%180==0)?SNES_PAD_START:0);
  }
  check(MmxBossRushGetState().menu==1,"Boss Rush appears on the native title menu");
  zero_replay(180);
  if(getenv("MMX_BOSS_RUSH_MENU_TEST")) rush_title_navigation_checks(start,cap);
  for(unsigned n=0;n<3;++n) {frame(SNES_PAD_DOWN);zero_replay(12);}
  check(MmxBossRushGetState().selection==3,"fourth title option is navigable");
  check(MmxRendererSaveCapture("rush-title.cap"),"fourth title entry capture saves");
  frame(SNES_PAD_Y);frame(SNES_PAD_Y);
  check(MmxRendererSaveCapture("rush-confirm.cap"),"confirmation keeps the selected fourth row");
  fprintf(stderr,"rush confirm state=%u/%u/%u/%u script=%u xy=%u,%u action=%u:%u\n",g_ram[0x38],g_ram[0x39],g_ram[0x3b],g_ram[0x3c],g_ram[0xc01],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,g_ram[0xbaa],g_ram[0xbab]);
  check(g_ram[0xbab]==2 && (g_ram[0xbb0]|g_ram[0xbb1]<<8)==214,
      "native title confirmation shot runs on the Boss Rush row");
  check(MmxBossRushGetState().mode==MMX_RUSH_LOADING,"Boss Rush starts directly without a mode submenu");
  check(MmxBossRushGetState().coop==(coop_assets!=NULL),"Boss Rush respects the configured co-op mod");
  bool title_shot=false;
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) {
    frame(SNES_PAD_Y);
    if(!MmxBossRushGetState().stage_started) {
      if(t==12) check(MmxRendererSaveCapture("rush-title-shot.cap"),"native title shot capture saves");
      for(unsigned d=0x1228;d<0x1428;d+=64) title_shot|=g_ram[d]!=0;
    }
  }
  check(title_shot,"native title confirmation produces a projectile before the fade");
  fprintf(stderr,"rush load mode=%u stage=%u checkpoint=%u/%u game=%u/%u/%u player=%u action=%u:%u xy=%u,%u camera=%u/%u\n",
      MmxBossRushGetState().mode,g_ram[0x1f7a],g_ram[0x1f81],g_ram[0x1f82],g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],
      g_ram[0xba9],g_ram[0xbaa],g_ram[0xbab],g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,
      g_ram[0x1e4d]|g_ram[0x1e4e]<<8,g_ram[0x1e50]|g_ram[0x1e51]<<8);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING,"title selection starts in the final boss-door corridor");
  check((g_ram[0xbad]|g_ram[0xbae]<<8)>=0x1d00 &&
      (g_ram[0xbad]|g_ram[0xbae]<<8)<0x1e00,"player arrives in the native preparation corridor");
  check(MmxRendererSaveCapture("rush-preparation.cap"),"preparation corridor capture saves");
  for(unsigned t=0;t<30;++t) frame(SNES_PAD_Y);
  for(unsigned d=0x1228;d<0x1428;d+=64) check(!g_ram[d],"held confirmation does not fire a weapon");
  zero_replay(180);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING &&
      !MmxBossRushGetState().bosses[0].phase && !MmxBossRushGetState().bosses[1].phase,
      "preparation waits for the central trigger before spawning bosses");
  check(g_ram[0x1f9a]==32 && g_ram[0x1f99]==255,"run starts fully upgraded");
  for(unsigned i=0;i<4;++i) check(g_ram[0x1f83+i]==0x8e,"shared subtank starts at the retail refill cap");
  for(unsigned i=0;i<8;++i) check(g_ram[0x1f88+i*2]==0xdc,"X1 weapon starts owned and full");
  check(!(g_ram[0x1f7e]&128),"Hadouken is not acquired");
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();
    check(c.initialized && c.players[0].status==MMX_COOP_ALIVE && c.players[1].status==MMX_COOP_ALIVE,
        "both co-op players start alive");
    check((c.players[0].body[0x27]&127)==32 && (c.players[1].body[0x27]&127)==32,"both co-op players start at maximum health");
  }
  unsigned stage_bank=g_ram[0xfffd],furthest_camera=0;
  unsigned walk_right=SNES_PAD_RIGHT|(coop_assets?SNES_PAD_RIGHT<<12:0);
  bool native_door=false,music_replayed=false,stopped_inside=false;
  for(unsigned t=0;t<1200 && MmxBossRushGetState().mode==MMX_RUSH_PREPARING;++t) {
    if(t%60==0) fprintf(stderr,"rush approach tick=%u x=%04x camera=%04x limits=%04x/%04x targets=%04x/%04x\n",
        t,eagle_word(g_ram+0xbad),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e56),
        eagle_word(g_ram+0x1e58),eagle_word(g_ram+0x1e5e),eagle_word(g_ram+0x1e60));
    if(!stopped_inside && eagle_word(g_ram+0xbad)>=0x1e60 &&
        g_ram[0xbaa]!=0x18 && g_ram[0xbaa]!=0x1a && !g_ram[0xc16]) {
      zero_replay(90);
      MmxBossRushState waiting=MmxBossRushGetState();
      check(waiting.mode==MMX_RUSH_PREPARING && !waiting.bosses[0].phase &&
          !waiting.bosses[1].phase && g_ram[0xfffd]==stage_bank,
          "stopping inside the room keeps both bosses and battle music pending");
      check(eagle_word(g_ram+0x1e58)>eagle_word(g_ram+0x1e56),
          "Mammoth's native room retains horizontal scrolling limits");
      check(MmxRendererSaveCapture("rush-arena-left.cap"),"left side of the scrolling arena capture saves");
      stopped_inside=true;
    }
    if(!music_replayed && MmxBossRushGetState().loaded==MMX_RUSH_MUSIC_PENDING) {
      size_t pending=RtlSaveSnapshotToMemory(expected,cap);
      check(pending!=0,"pending native music upload snapshot saves");
      frame(walk_right);size_t en=RtlSaveSnapshotToMemory(actual,cap);
      check(RtlLoadSnapshotFromMemory(expected,pending),"pending native music upload restores");
      frame(walk_right);size_t an=RtlSaveSnapshotToMemory(start,cap);
      same(actual,en,start,an,"pending native music upload replays deterministically");
      music_replayed=true;
    }
    frame(walk_right);native_door|=g_ram[0xbaa]==0x18 || g_ram[0xbaa]==0x1a;
    if(eagle_word(g_ram+0x1e4d)>furthest_camera) furthest_camera=eagle_word(g_ram+0x1e4d);
  }
  fprintf(stderr,"rush music bank before=%02x after=%02x door=%u\n",
      stage_bank,g_ram[0xfffd],native_door);
  fprintf(stderr,"rush door mode=%u camera=%u player=%u action=%u:%u lock=%u flags=%u/%u/%u section=%u\n",
      MmxBossRushGetState().mode,g_ram[0x1e4d]|g_ram[0x1e4e]<<8,
      g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbaa],g_ram[0xbab],g_ram[0xc16],
      g_ram[0x1f23],g_ram[0x1f26],g_ram[0x1f13],g_ram[0x1f08]);
  check(native_door,"second door runs the native player door controller");
  /* Derive the expected track from the retail Penguin health-fill controller,
   * independently of Rush's chosen command. A valid SPC upload alone would
   * also pass for the victory song, which is the wrong encounter music. */
  const uint8_t *rom=g_snes->cart->rom;
  static const uint8_t music_call[]={0x22,0xa2,0x87,0x80};
  check(rom[0xb617]==0xa9 && !memcmp(rom+0xb619,music_call,sizeof(music_call)),
      "retail Penguin entrance supplies a native music command");
  unsigned boss_song=rom[0xb618];
  unsigned boss_bank=rom[0x3007b+rom[0x3006b+boss_song]];
  fprintf(stderr,"retail Penguin battle command=%02x bank=%02x\n",boss_song,boss_bank);
  check(g_ram[0xfffd]==boss_bank && g_ram[0xfffd]!=stage_bank,
      "native boss music bank replaces the stage bank");
  check(stopped_inside && furthest_camera>0x1e00,"walking through the arena scrolls beyond its first screen");
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING &&
      eagle_word(g_ram+0xbad)>=MmxBossRushGetState().camera_x+MMX_RUSH_ARENA_CENTER_X,
      "reaching the room's center starts the encounter");
  int player_screen_x=(int)eagle_word(g_ram+0xbad)-(int)eagle_word(g_ram+0x1e4d);
  check(player_screen_x>=104 && player_screen_x<=160,
      "the encounter begins with the player near the middle of the view");
  if(getenv("MMX_BOSS_RUSH_FOREIGN_SPIKE_TEST")) {rush_foreign_spike_checks(start,cap);return;}
  if(getenv("MMX_BOSS_RUSH_RHINO_TEST")) {rush_rhino_checks(start,cap,getenv("MMX_BOSS_RUSH_X3_ROM"));return;}
  if(getenv("MMX_BOSS_RUSH_SPONGE_TEST")) {rush_sponge_checks(start,cap,getenv("MMX_BOSS_RUSH_X2_ROM"));return;}
  if(getenv("MMX_BOSS_RUSH_ARENA_TEST")) {
    MmxBossRushState s=MmxBossRushGetState();
    memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
    memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);
    s.queue[0]=0;s.queue[1]=7;s.queued=8;
    for(unsigned i=1;i<7;++i)s.queue[i+1]=(uint8_t)i;
    check(MmxBossRushSetState(&s),"arena fixture selects Penguin and Mammoth");
    bool framed=false;unsigned visible_frames=0;
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[0].object && s.bosses[1].object &&
          g_ram[s.bosses[0].object+1]>=2 && g_ram[s.bosses[1].object+1]>=2 &&
          g_ram[s.bosses[0].object+14] && g_ram[s.bosses[1].object+14]) ++visible_frames;
      else visible_frames=0;
      if(!framed && visible_frames>=3) {
        for(unsigned i=0;i<2;++i) {
          int x=(int)eagle_word(g_ram+s.bosses[i].object+5)-(int)eagle_word(g_ram+0x1e4d);
          check(x>=32 && x<=224,"both boss introductions begin inside the visible arena");
          check(i?x>player_screen_x:x<player_screen_x,"boss entrances flank the centered player");
        }
        check(MmxRendererSaveCapture("rush-mammoth-arena.cap"),"visible boss introductions capture saves");
        framed=true;
      }
      if(t%60==0) fprintf(stderr,"arena tick=%u defeated=%u bosses=%u:%u:%u/%u:%u:%u xy=%04x,%04x/%04x,%04x\n",
          t,s.defeated,s.bosses[0].id,s.bosses[0].phase,g_ram[s.bosses[0].object+1],
          s.bosses[1].id,s.bosses[1].phase,g_ram[s.bosses[1].object+1],
          eagle_word(g_ram+s.bosses[0].object+5),eagle_word(g_ram+s.bosses[0].object+8),
          eagle_word(g_ram+s.bosses[1].object+5),eagle_word(g_ram+s.bosses[1].object+8));
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
    }
    check(s.bosses[0].id==0 && s.bosses[1].id==7 && !s.defeated &&
        s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
        "Penguin and Mammoth complete entrances in the new arena");
    check(framed,"both boss introductions were visible together before combat");
    rush_scrolling_checks(start,cap);
    rush_conveyor_checks(start,cap);
    for(unsigned t=0;t<7;++t) {frame(0);rush_keep_players_alive();}
    check(MmxRendererSaveCapture("rush-mammoth-arena-later.cap"),"later Mammoth arena capture saves");
    fprintf(stderr,"arena floor=%02x player=%u,%u camera=%u,%u palette-state=%u/%u\n",
        MmxWeaponsTerrainClass(g_ram,eagle_word(g_ram+0xbad),eagle_word(g_ram+0xbb0)+17),
        eagle_word(g_ram+0xbad),eagle_word(g_ram+0xbb0),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),g_ram[0x1f09],g_ram[0x1f0a]);
    size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"live Mammoth arena saves");
    s=MmxBossRushGetState();g_ram[s.bosses[1].object+0x27]=0;frame(0);
    s=MmxBossRushGetState();
    check(s.defeated==1 && s.bosses[1].phase==MMX_RUSH_DYING && !MmxBossRushConveyorRunning(&s),
        "native Mammoth defeat stops the conveyor immediately");
    for(unsigned t=0;t<90;++t) {frame(0);rush_keep_players_alive();}
    static uint32_t stopped[256*224],later[256*224];
    unsigned stopped_camera=eagle_word(g_ram+0x1e4d);
    check(MmxRendererDraw(stopped,(MmxRenderView){256,0,4.0/3.0},true),"stopped conveyor renders");
    for(unsigned t=0;t<17;++t) {frame(0);rush_keep_players_alive();}
    check(MmxRendererDraw(later,(MmxRenderView){256,0,4.0/3.0},true),"stopped conveyor renders later");
    unsigned later_camera=eagle_word(g_ram+0x1e4d);
    unsigned belt_x=(stopped_camera>later_camera?stopped_camera:later_camera)+32;
    /* Follow one world-space belt patch; player follow can keep scrolling
     * after a hit, and the room's right-hand machinery animates separately. */
    for(unsigned y=200;y<208;++y)
      check(!memcmp(stopped+y*256+belt_x-stopped_camera,later+y*256+belt_x-later_camera,96*sizeof(*stopped)),
          "conveyor pixels stay stationary after Mammoth is replaced");
    check(MmxRendererSaveCapture("rush-conveyor-stopped.cap"),"stopped conveyor capture saves");
    check(RtlLoadSnapshotFromMemory(start,n),"live Mammoth arena restores after defeat checks");
    frame(0);rush_keep_players_alive();
    puts("MMX BOSS RUSH ARENA CHECKS PASSED");
    return;
  }
  if(getenv("MMX_BOSS_RUSH_REPORT_TEST")) {rush_report_checks(start,expected,cap);return;}
  if(getenv("MMX_BOSS_RUSH_GRAVITY_TEST")) {rush_gravity_checks(start,expected,actual,cap);return;}
  if(getenv("MMX_ZERO_SABER_PERF_TEST")) {
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();MmxBossRushState s=MmxBossRushGetState();
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING)break;
    }
    saber_perf_checks("boss-rush",start,expected,actual,cap);return;
  }
  if(getenv("MMX_BOSS_RUSH_COOP_COMBAT_TEST")) {rush_coop_combat_checks(start,expected,cap);return;}
  if(getenv("MMX_BOSS_RUSH_MENU_TEST")) {rush_return_title_checks();return;}
  if(getenv("MMX_BOSS_RUSH_STATE_FORMAT_TEST")) {rush_state_format_checks(start,expected,actual,cap);return;}
  if(getenv("MMX_BOSS_RUSH_AUDIO_TEST")) {rush_audio_checks(start,cap);return;}
  if(getenv("MMX_BOSS_RUSH_DROP_TEST")) {rush_drop_checks(start,expected,actual,cap);return;}
  if(getenv("MMX_BOSS_RUSH_DEATH_TEST")) {rush_natural_death_checks();return;}
  if(getenv("MMX_BOSS_RUSH_HIT_VISUAL_TEST") || getenv("MMX_BOSS_RUSH_HIT_AUDIO_TEST")) {rush_hit_visual_checks(start,expected,cap);return;}
  if(getenv("MMX_BOSS_RUSH_CHAMELEON_TEST")) {rush_chameleon_replacement_checks(start,cap);return;}
  if(getenv("MMX_BOSS_RUSH_DEATH_LATCH_TEST")) {rush_death_latch_checks();return;}
  if(getenv("MMX_BOSS_RUSH_COMBO_SOUND_TEST")) {rush_combo_sound_checks();return;}
  if(getenv("MMX_BOSS_RUSH_REPLACEMENT_VISUAL_TEST") || getenv("MMX_BOSS_RUSH_PAUSE_VISUAL_TEST")) {
    rush_replacement_menu_checks(start,expected,cap);return;
  }
  if(getenv("MMX_BOSS_RUSH_FORCE_VISUAL")) {rush_forced_visual_checks(start,cap);return;}
  if(getenv("MMX_BOSS_RUSH_AIR_VISUAL_TEST")) {rush_air_visual_trace();return;}
  if(getenv("MMX_BOSS_RUSH_VISUAL_TEST")) {rush_visual_checks(start,cap);return;}
  size_t baseline=RtlSaveSnapshotToMemory(start,cap);check(baseline!=0,"arena snapshot saves");
  /* Place this independent controller probe inside the room so
   * wall/tether limits cannot mask input. */
  eagle_putword(g_ram+0xbad,MmxBossRushGetState().camera_x+128);
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i)eagle_putword(c.players[i].body+5,MmxBossRushGetState().camera_x+128+i*64);
    MmxCoopSetState(&c);memcpy(g_ram+0xba8,c.players[c.current].body,sizeof(c.players[c.current].body));
  }
  MmxCoopState before_coop=MmxCoopGetState();
  unsigned before=eagle_word(coop_assets && before_coop.current!=0?
      before_coop.players[0].body+5:g_ram+0xbad);
  unsigned partner_before=eagle_word(before_coop.current==1?g_ram+0xbad:before_coop.players[1].body+5);
  for(unsigned t=0;t<8;++t) frame(SNES_PAD_RIGHT|(coop_assets?SNES_PAD_LEFT<<12:0));
  MmxCoopState after_coop=MmxCoopGetState();
  unsigned after=eagle_word(coop_assets && after_coop.current!=0?
      after_coop.players[0].body+5:g_ram+0xbad);
  unsigned partner_after=eagle_word(after_coop.current==1?g_ram+0xbad:after_coop.players[1].body+5);
  fprintf(stderr,"rush input probe projected=%u/%u p1=%04x/%04x p2=%04x/%04x\n",
      before_coop.current,after_coop.current,before,after,partner_before,partner_after);
  check(after>before,"player moves while bosses enter");
  if(coop_assets) {
    check(partner_after<partner_before,"partner uses port 2 while bosses enter");
  }
  check(RtlLoadSnapshotFromMemory(start,baseline),"restore arena after input check");
  for(unsigned a=0;a<8;++a) for(unsigned b=a+1;b<8;++b) {
    check(RtlLoadSnapshotFromMemory(start,baseline),"restore empty arena for boss pair");
    MmxBossRushState s=MmxBossRushGetState();
    memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
    s.queued=8;for(unsigned i=0;i<8;++i) s.queue[i]=(uint8_t)i;
    unsigned pos=2;s.queue[0]=(uint8_t)a;s.queue[1]=(uint8_t)b;
    for(unsigned i=0;i<8;++i) if(i!=a && i!=b) s.queue[pos++]=(uint8_t)i;
    check(MmxBossRushSetState(&s),"test pair queue installs");
    memset(g_ram+0xe68,0,0x3c0);
    for(unsigned t=0;t<1500;++t) {
      frame(0);s=MmxBossRushGetState();
      rush_keep_players_alive(); /* Isolate controller compatibility from survival. */
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING) break;
    }
    fprintf(stderr,"rush pair %u/%u phases=%u/%u hp=%u/%u native=%u:%u/%u:%u xy=%u,%u/%u,%u\n",
      a,b,s.bosses[0].phase,s.bosses[1].phase,s.bosses[0].health,s.bosses[1].health,
      g_ram[s.bosses[0].object+1],g_ram[s.bosses[0].object+2],g_ram[s.bosses[1].object+1],g_ram[s.bosses[1].object+2],
      g_ram[s.bosses[0].object+5]|g_ram[s.bosses[0].object+6]<<8,g_ram[s.bosses[0].object+8]|g_ram[s.bosses[0].object+9]<<8,
      g_ram[s.bosses[1].object+5]|g_ram[s.bosses[1].object+6]<<8,g_ram[s.bosses[1].object+8]|g_ram[s.bosses[1].object+9]<<8);
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,"both native bosses complete their own entrances");
    check(s.bosses[0].health==32 && s.bosses[1].health==32,"both boss meters fill independently");
    char capture[40];snprintf(capture,sizeof(capture),"rush-pair-%u-%u.cap",a,b);
    check(MmxRendererSaveCapture(capture),"native boss pair capture saves");
    if(a==0 && b==4) {
      size_t pair_snapshot=RtlSaveSnapshotToMemory(expected,cap);
      frame(SNES_PAD_START);zero_replay(90);
      check(MmxRendererSaveCapture("rush-weapon-pause.cap"),"native weapon pause capture saves");
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore combat after pause capture");
      for(unsigned seat=0;seat<2;++seat) {
        check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore weakness finishing-hit fixture");
        uint32_t generation=MmxBossRushGetState().bosses[seat].generation;
        rush_projectile_defeat(seat,seat?1:4);
        for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
        check(MmxBossRushGetState().bosses[seat].phase && MmxBossRushGetState().defeated==1 &&
            MmxBossRushGetState().bosses[seat].generation>generation,
            "native weakness finishing hit replaces its boss");
      }
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore boss pair after native finishing hits");
      s=MmxBossRushGetState();s.bosses[0].phase=MMX_RUSH_ENTERING;MmxBossRushSetState(&s);
      g_ram[s.bosses[0].object+1]=8;MmxBossRushAfterFrame(g_ram);
      check(MmxBossRushGetState().bosses[0].phase==MMX_RUSH_FIGHTING,
          "entrance completion followed by a hit reaction still enters combat");
      s=MmxBossRushGetState();s.bosses[0].phase=MMX_RUSH_ENTERING;MmxBossRushSetState(&s);
      g_ram[s.bosses[0].object+1]=6;g_ram[s.bosses[0].object+0x27]=0;MmxBossRushAfterFrame(g_ram);
      check(MmxBossRushGetState().defeated==1,"death after an unobserved combat state still counts once");
      for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
      fprintf(stderr,"rush transition cleanup phase=%u ticks=%u actor=%u object=%x S=%x gates=%u/%u/%u\n",
          MmxBossRushGetState().bosses[0].phase,MmxBossRushGetState().bosses[0].ticks,
          MmxBossRushGetState().actor,MmxBossRushGetState().actor_object,MmxBossRushGetState().actor_s,
          g_ram[0xd3],g_ram[0x1f19],MmxCoopGetState().menu_owner);
      check(MmxBossRushGetState().bosses[0].phase!=MMX_RUSH_DYING,
          "death after an unobserved combat state advances the queue");
      check(RtlLoadSnapshotFromMemory(expected,pair_snapshot),"restore boss pair after entrance regression");
    }
    if(!a && b==1) {
      for(unsigned t=0;t<180;++t) {frame(t%12==0?SNES_PAD_Y:0);rush_keep_players_alive();}
      s=MmxBossRushGetState();
      check(s.bosses[0].health<32 || s.bosses[1].health<32,"native buster damages a fighting boss");
    }
    int audio_peak=0;
    for(unsigned t=0;t<600;++t) {
      frame(0);rush_keep_players_alive();
      if(a==0 && b==4) {
        /* Consume an audio callback's worth each frame. The last gameplay
         * frame need not contain an active effect, especially after a
         * boomerang's owning object has retired. */
        int16_t pcm[1068]={0};
        RtlApuLock();snes_mod_audio_mix(pcm,534,32040,2);RtlApuUnlock();
        for(unsigned i=0;i<1068;++i) if(abs(pcm[i])>audio_peak) audio_peak=abs(pcm[i]);
      }
    }
    if(a==0 && b==4) {
      check(MmxBossRushAudioGetState().sequence>0,"native boss updates emit snapshot-owned sound cues");
      check(audio_peak>0,"committed native boss cues reach the independent audio mixer");
    }
    size_t n=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(expected,n),"two-boss combat restores");
    frame(0);size_t en=RtlSaveSnapshotToMemory(actual,cap);
    check(RtlLoadSnapshotFromMemory(expected,n),"two-boss combat restores again");
    frame(0);size_t an=RtlSaveSnapshotToMemory(expected,cap);
    same(actual,en,expected,an,"boss pair replays deterministically");
    s=MmxBossRushGetState();unsigned survivor=s.bosses[1].object,id=s.bosses[1].id;
    uint32_t generation=s.bosses[0].generation;
    g_ram[s.bosses[0].object+0x27]=0;frame(0);
    check(MmxBossRushGetState().defeated==1,"one defeat increments the counter once");
    for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
    s=MmxBossRushGetState();
    check(s.bosses[1].object==survivor && s.bosses[1].id==id && g_ram[survivor],"replacement preserves the surviving boss");
    check(s.bosses[0].phase && s.bosses[0].phase!=MMX_RUSH_DYING &&
        s.bosses[0].generation>generation && s.bosses[0].id!=id && s.defeated==1,
        "replacement is a new distinct boss and death counts once");
  }
  if(coop_assets) {
    MmxCoopState c=MmxCoopGetState();c.players[0].body[0x27]=128;c.players[0].body[2]=12;c.players[0].body[3]=0;
    if(c.current==0) {g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;}
    MmxCoopSetState(&c);frame(0);
    check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"one co-op death leaves the run playing");
    c=MmxCoopGetState();
    for(unsigned i=0;i<2;++i) {c.players[i].body[0x27]=128;c.players[i].body[2]=12;c.players[i].body[3]=0;}
    g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;MmxCoopSetState(&c);frame(0);
  } else {g_ram[0xbcf]=128;g_ram[0xbaa]=12;g_ram[0xbab]=0;frame(0);}
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"fatal damage lets the native death controller run");
  zero_replay(40);
  check(MmxBossRushGetState().mode==MMX_RUSH_PLAYING,"results wait while native death orbs animate");
  check(MmxRendererSaveCapture("rush-player-death.cap"),"native player death capture saves");
  for(unsigned t=0;t<300 && MmxBossRushGetState().mode==MMX_RUSH_PLAYING;++t) frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_FINISHED,"last player death ends the run");
  check(MmxRendererSaveCapture("rush-results.cap"),"results capture saves");
  frame(0);frame(SNES_PAD_START);frame(0);
  for(unsigned t=0;t<1800 && MmxBossRushGetState().mode==MMX_RUSH_LOADING;++t) frame(0);
  check(MmxBossRushGetState().mode==MMX_RUSH_PREPARING && !MmxBossRushGetState().defeated,"Retry returns to preparation with a new full run");
  rush_return_title_checks();
  puts("MMX BOSS RUSH ROM CHECKS PASSED (28 distinct pairs)");
}
