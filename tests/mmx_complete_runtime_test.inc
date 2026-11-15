#include "mmx_complete.h"
#include "mmx_hadouken.h"
#include "mmx_source_assets.h"

static void complete_key(unsigned input) {
  int w,h;MmxPrepareFrame(1280,720,&w,&h);frame(input);
  MmxPrepareFrame(1280,720,&w,&h);frame(0);
}
static void complete_select(unsigned row) {
  MmxCompleteDebugState s = MmxCompleteDebugGetState();
  s.selection = row; MmxCompleteDebugSetState(&s);
}
static void complete_actor(unsigned pc,unsigned actor) {
  CpuState cpu=g_cpu;cpu.S=0x1ff;cpu.D=actor;cpu.DB=0x86;cpu.PB=pc>>16;
  cpu.P=0x30;cpu.emulation=0;cpu_p_to_mirrors(&cpu);cpu_push_jsl_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  check(interp_bridge_run(&cpu,pc) && cpu.S==0x1ff,"native actor returns with balanced stack");
}
static void complete_hoganmer(void) {
  const unsigned actor=0xe68;
  memset(g_ram+actor,0,0x1928-actor);
  g_ram[actor]=1;g_ram[actor+10]=1;
  unsigned x=(g_ram[0xbad]|g_ram[0xbae]<<8)+64,y=g_ram[0xbb0]|g_ram[0xbb1]<<8;
  g_ram[actor+5]=x;g_ram[actor+6]=x>>8;g_ram[actor+8]=y;g_ram[actor+9]=y>>8;
  complete_actor(0x83ae81,actor);
  unsigned shield=g_ram[actor+0x34]|g_ram[actor+0x35]<<8;
  check(shield>=0x1628 && shield<0x1928 && g_ram[shield+10]==3,"Hoganmer's native initializer creates its actual shield object");
  complete_actor(0x81e16b,shield);
  check(g_ram[shield+0x27]>0 && g_ram[shield+0x28]==0,"native shield uses the restored damage row");
  g_ram[shield+0x0e]=1; /* A live, visible collision body. */
  unsigned body_hp=g_ram[actor+0x27];
  memset(g_ram+0x1228,0,64);g_ram[0x1228]=1;g_ram[0x1229]=2;g_ram[0x1232]=18;
  g_ram[0x1236]=1;g_ram[0x1248]=0xae;g_ram[0x1249]=0xbf;
  memcpy(g_ram+0x122d,g_ram+shield+5,2);memcpy(g_ram+0x1230,g_ram+shield+8,2);
  /* An overlapping live charged-shield hitbox is the fixture precondition.
   * The real shield updater must test the contact and perform its own death;
   * no health, hit-result or parent-success flag is written by the test. */
  complete_actor(0x81e16b,shield);
  check(!(g_ram[shield+0x27]&127) && g_ram[shield+1]==4,"native contact breaks the actual shield object");
  g_ram[0x1228]=0;
  for(unsigned i=0;i<60 && !g_ram[actor+0x37];++i)complete_actor(0x81e16b,shield);
  if(!g_ram[actor+0x37])fprintf(stderr,"Hogan fixture: actor=%02x/%02x/%02x hp=%u shield=%04x %02x/%02x hp=%u projectile=%02x/%02x\n",g_ram[actor],g_ram[actor+1],g_ram[actor+2],g_ram[actor+0x27],shield,g_ram[shield],g_ram[shield+1],g_ram[shield+0x27],g_ram[0x1228],g_ram[0x1229]);
  check(g_ram[actor+0x37]!=0,"native shield destruction signals its Hoganmer parent");
  check(g_ram[actor+0x27]==body_hp,"shield breaks while Hoganmer survives");
  g_ram[0x1228]=0;g_ram[0xc30]=0;
  for(unsigned i=0;i<240 && g_ram[actor] && g_ram[actor+2]!=8;++i)frame(0);
  check(g_ram[actor] && g_ram[actor+2]==8,"Hoganmer naturally enters its formerly unused shieldless AI");
}
static void complete_runtime_checks(const char *rom_path) {
  check(snes_mod_runtime_initialize_c("catalog", "megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"), "Complete Edition catalog parses");
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  static const char *const names[] = {"script","colours","typos","hoganmer","damage","debug"};
  for (unsigned i = 0; i < 6; ++i) {
    char id[96]; snprintf(id, sizeof(id), "megaman-x.complete.%s", names[i]);
    check(snes_mod_runtime_feature_enabled_c(id, names[i]), "each Complete Edition feature defaults on");
  }
  check(provider->commit(provider->ctx,rom_path), "fresh Complete Edition plan commits");
  snes_mod_runtime_activate_plugins_c();
  for (unsigned i = 0; i < 6; ++i)
    check(MmxCompleteEnabled((MmxCompleteFeature)i), "each selected native plugin activates");
  char identity[16384];
  snes_mod_runtime_effective_set_c(identity,sizeof(identity));
  for (unsigned i = 0; i < 6; ++i) {
    char id[96]; snprintf(id,sizeof(id),"megaman-x.complete.%s",names[i]);
    check(strstr(identity,id)!=NULL,"guest-memory changes participate in netplay identity");
  }
  /* Real native collision at the exact affected damage rows/classes, including
   * compiled and interpreter paths and the existing outgoing multiplier. */
  for (unsigned path = 0; path < 2; ++path) for (unsigned enabled = 0; enabled < 2; ++enabled) {
    check(MmxCompleteSetEnabled(MMX_COMPLETE_DAMAGE,enabled),"damage toggle applies");
    for (unsigned row_index = 0; row_index < 2; ++row_index) for (unsigned cls = 2; cls <= 3; ++cls) {
      for (unsigned scale = 1; scale <= 2; ++scale) {
        MmxDamageReset(); MmxDamageSetDealt(scale*100); MmxZeroRegisterHooks();
        weakness_actor_setup(0x14,row_index?18:11,0,0,0);
        g_ram[0x1232]=cls;
        weakness_native_contact(path);
        check((g_ram[0xe8f]&127)==32-(enabled?2:1)*scale,
            "buster correction composes once with the damage multiplier in both tiers");
      }
    }
    MmxDamageReset(); weakness_actor_setup(0x50,19,0,0,0); g_ram[0x1232]=4;
    weakness_native_contact(path);
    check((g_ram[0xe8f]&127)==(enabled?0:32),"Wolf Sigma accepts Hadouken only with the correction enabled");
    check(MmxCompleteSetEnabled(MMX_COMPLETE_HOGANMER,enabled),"Hoganmer toggle applies");
    weakness_actor_setup(1,0,0,0,0); g_ram[0x1232]=18;
    weakness_native_contact(path);
    check((g_ram[0xe8f]&127)==(enabled?23:32),"charged Rolling Shield damages the formerly immune shield row in both tiers");
  }
  MmxDamageReset();
  check(MmxCompleteSetEnabled(MMX_COMPLETE_DAMAGE,true) && MmxCompleteSetEnabled(MMX_COMPLETE_HOGANMER,true),"gameplay data re-enabled");
  /* Re-activation uses the current machine and restores old selections before
   * applying the new plan. No pointer to a previous machine is dereferenced. */
  check(provider->feature_enable(provider->ctx,"megaman-x.complete.script","script",0) &&
      provider->commit(provider->ctx,rom_path),"launcher can independently disable retranslation");
  snes_mod_runtime_activate_plugins_c();
  check(!MmxCompleteEnabled(MMX_COMPLETE_SCRIPT) && MmxCompleteEnabled(MMX_COMPLETE_COLOURS),"stock dialogue retains optional character colours");
  check(provider->feature_enable(provider->ctx,"megaman-x.complete.script","script",1) &&
      provider->feature_enable(provider->ctx,"megaman-x.accessibility.hadouken","hadouken",1) &&
      provider->commit(provider->ctx,rom_path),"retranslation and Easy Hadouken coexist");
  snes_mod_runtime_activate_plugins_c();
  memset(g_ram,0,0x20000); g_ram[0xd1]=2;g_ram[0xd2]=g_ram[0xd3]=4;
  g_ram[0xbe3]=0x40;MmxHadoukenInput(g_ram);
  check(g_ram[0xc27]==6 && g_ram[0xc28]==20,"Complete Edition keeps Easy Hadouken's native eligibility recognizer");
  RtlReset(0);
  hitbox_boot();
  size_t cap=2u*1024u*1024u;
  uint8_t *start=malloc(cap),*saved=malloc(cap),*after=malloc(cap);check(start && saved && after,"practice snapshots allocate");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n>0,"retail gameplay checkpoint saves");
  unsigned chord=SNES_PAD_L|SNES_PAD_X|SNES_PAD_A;
  complete_key(chord);
  check(MmxCompleteDebugGetState().open,"real frame input opens native practice menu");
  uint32_t pixels[256*224];
  check(MmxCompleteDebugDraw(pixels,256,224,g_ram),"practice menu draws at native aspect");
  hitbox_bmp(pixels,256,"complete-debug");
  {uint32_t wide[342*224];
    check(MmxCompleteDebugDraw(wide,342,224,g_ram),"practice menu draws in adaptive widescreen");
    hitbox_bmp(wide,342,"complete-debug-wide");}
  unsigned hp=g_ram[0xbcf],x=g_ram[0xbad],y=g_ram[0xbb0];
  for(unsigned i=0;i<12;++i)frame(0);
  check(g_ram[0xbcf]==hp && g_ram[0xbad]==x && g_ram[0xbb0]==y,"practice menu freezes gameplay without forwarding pad input");
  complete_select(2);complete_key(SNES_PAD_Y);check(g_ram[0x1f99]&1,"practice menu grants helmet");
  complete_key(SNES_PAD_B);check(!(g_ram[0x1f99]&1),"practice menu clears helmet independently");
  for(unsigned row=3;row<=5;++row) {
    complete_select(row);complete_key(SNES_PAD_Y);check(g_ram[0x1f99]&(1u<<(row-2)),"practice menu grants each native upgrade");
  }
  complete_select(6);g_ram[0xbcf]=3;complete_key(SNES_PAD_Y);
  check(g_ram[0xbcf]==g_ram[0x1f9a],"practice menu refills current health");
  complete_select(7);complete_key(SNES_PAD_Y);
  check(g_ram[0x1e61]==127 && g_ram[0x1e6f]==127 && !g_ram[0x1e69] && !g_ram[0x1e5f],"practice free-scroll writes match original hack");
  complete_select(9);complete_key(SNES_PAD_Y);check(g_ram[0xbd8]&8,"practice ghost mode enables");
  complete_key(SNES_PAD_B);check(!(g_ram[0xbd8]&8),"practice ghost mode disables");
  size_t sn=RtlSaveSnapshotToMemory(saved,cap);check(sn>0,"open practice menu snapshot saves");
  complete_key(SNES_PAD_DOWN);complete_key(SNES_PAD_UP);
  check(RtlLoadSnapshotFromMemory(saved,sn) && MmxCompleteDebugGetState().open &&
      MmxCompleteDebugGetState().selection==9,"current-build snapshot restores practice selection and input history");
  complete_key(SNES_PAD_DOWN);complete_key(SNES_PAD_UP);
  size_t an=RtlSaveSnapshotToMemory(after,cap);check(an>0,"practice continuation saves");
  check(RtlLoadSnapshotFromMemory(saved,sn),"practice checkpoint restores for replay");
  complete_key(SNES_PAD_DOWN);complete_key(SNES_PAD_UP);
  sn=RtlSaveSnapshotToMemory(saved,cap);same(saved,sn,after,an,"practice input replays byte-identically after save/load");
  complete_key(SNES_PAD_START);check(!MmxCompleteDebugGetState().open,"Start exits practice menu");
  check(RtlLoadSnapshotFromMemory(start,n),"restore gameplay after practice menu");
  check(MmxCompleteSetEnabled(MMX_COMPLETE_DEBUG,false),"debug feature disables");complete_key(chord);
  check(!MmxCompleteDebugGetState().open,"disabled debugger leaves the shortcut inactive");
  MmxCompleteSetEnabled(MMX_COMPLETE_DEBUG,true);
  MmxCompleteDebugState bad={0};bad.selection=255;
  check(!MmxCompleteDebugValidState(&bad),"malformed practice state is rejected");
  const char *x3=getenv("MMX_COMPLETE_X3_ROM"),*x2=getenv("MMX_COMPLETE_X2_ROM");
  if(x3 && x2) {
    char error[512];
    check(MmxSourceAssetsBuild(x3,3,1,"complete-zero.bin",error,sizeof(error)),"owned X3 Zero assets extract through the supported importer");
    check(MmxSourceAssetsBuild(x3,3,0,"complete-x3.bin",error,sizeof(error)) &&
        MmxSourceAssetsBuild(x2,2,0,"complete-x2.bin",error,sizeof(error)),"owned X2 and X3 weapon assets extract through the supported importer");
    for(unsigned character=0;character<2;++character) {
      check(RtlLoadSnapshotFromMemory(start,n),"restore gameplay for independent mod interaction");
      check(MmxZeroLoad("complete-zero.bin") && MmxWeaponsLoadPage("complete-x2.bin",1) &&
          MmxWeaponsLoadPage("complete-x3.bin",2),"existing Zero and both weapon pages load with Complete Edition");
      MmxZeroRegisterHooks();MmxCoopRegisterHooks();
      check(MmxCoopEnable(character),"co-op accepts either player-one character");
      coop_frames(120,0,0);
      MmxCoopState coop=MmxCoopGetState();
      check(coop.initialized && MmxCoopValidState(&coop),"existing co-op roster remains valid");
      complete_key(chord);check(MmxCompleteDebugGetState().open,"practice menu opens with either co-op roster");
      complete_select(2);complete_key(SNES_PAD_Y);complete_key(SNES_PAD_B);
      coop=MmxCoopGetState();check(MmxCoopValidState(&coop),"upgrade edits retain both co-op character states");
      complete_key(SNES_PAD_START);coop_frames(30,SNES_PAD_RIGHT,SNES_PAD_Y);
      coop=MmxCoopGetState();check(MmxCoopValidState(&coop) && MmxWeaponsPageEnabled(1) && MmxWeaponsPageEnabled(2),"co-op movement and attacks continue with both weapon pages");
      check(g_snes->cart->rom[0x36fcb]==9 && g_snes->cart->rom[0x3719d]==32,"existing character collision patches retain Complete Edition combat data");

      sn=RtlSaveSnapshotToMemory(saved,cap);check(sn>0,"combined-mod snapshot saves");
      coop_frames(8,0,0);check(RtlLoadSnapshotFromMemory(saved,sn),"combined-mod snapshot loads");
      MmxCoopDisable();MmxZeroDisable();MmxWeaponsDisable();
    }
    puts("MMX COMPLETE EDITION ZERO / X2 / X3 / CO-OP INTERACTIONS PASSED");
  }
  check(RtlLoadSnapshotFromMemory(start,n),"restore gameplay for Hoganmer fixture");
  complete_hoganmer();
  free(start);free(saved);free(after);
  puts("MMX COMPLETE EDITION RUNTIME CHECKS PASSED");
}
