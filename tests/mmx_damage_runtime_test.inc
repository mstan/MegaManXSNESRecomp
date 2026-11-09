#include "mmx_damage.h"
extern RecompReturn bank_84_9B03_M1X0(CpuState *cpu);
extern RecompReturn bank_84_9F2A_M1X1(CpuState *cpu);

static void damage_call(unsigned path, unsigned kind, unsigned enemy) {
  CpuState cpu = g_cpu;
  cpu.S = 0x1ff; cpu.D = kind == 1 ? (uint16_t)enemy : 0xe68;
  cpu.X = 0x1228; cpu.Y = 0; cpu.DB = kind == 2 ? 0 : 0x86;
  cpu.PB = path == 2 ? 4 : 0x84;
  cpu.P = kind == 2 ? 0x30 : 0x20; cpu.emulation = 0;
  cpu_p_to_mirrors(&cpu); cpu_push_jsl_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  unsigned pc = kind == 2 ? 0x849f2a : kind == 1 ? 0x849b03 : 0x849e15;
  int ok = path == 0 ? interp_bridge_run(&cpu, pc) : kind == 2 ?
      bank_84_9F2A_M1X1(&cpu) == RECOMP_RETURN_NORMAL : kind == 1 ?
      bank_84_9B03_M1X0(&cpu) == RECOMP_RETURN_NORMAL : path == 2 ?
      cpu_dispatch_call_pc_pushed(&cpu,0x049e15,0x048000,3,NULL) == RECOMP_RETURN_NORMAL :
      bank_84_9E15_M1X0(&cpu) == RECOMP_RETURN_NORMAL;
  check(ok && cpu.S == 0x1ff, "damage routine returns with balanced stack");
}

static void damage_runtime_checks(const char *rom_path) {
  const char *catalog = "catalog";
  /* Only the test's copied catalog is writable; no player profile is used. */
  remove("catalog/state.toml");
  check(catalog && snes_mod_runtime_initialize_c(catalog, "megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),
      "damage catalog parses");
  const RecompLauncherCModProvider *p = snes_mod_runtime_launcher_provider_c();
  const char *out = "megaman-x.gameplay.damage-dealt";
  const char *in = "megaman-x.gameplay.damage-received";
  check(!snes_mod_runtime_feature_enabled_c(out,"damage") &&
      !snes_mod_runtime_feature_enabled_c(in,"damage"), "both damage mods default off");
  check(snes_mod_runtime_feature_enabled_c("megaman-x.enhancement.widescreen","widescreen"),
      "fresh packaged catalog enables adaptive widescreen");
  check(p->feature_enable(p->ctx,out,"damage",1) &&
      p->feature_set_option(p->ctx,out,"damage","multiplier","2") &&
      p->feature_enable(p->ctx,in,"damage",1) &&
      p->feature_set_option(p->ctx,in,"damage","multiplier","0.5") &&
      p->commit(p->ctx,rom_path), "launcher commits independent multipliers");
  snes_mod_runtime_activate_plugins_c();
  check(MmxDamageDealt(g_ram,0xe68,4) == 8 && MmxDamageReceived(g_ram,4,false) == 2,
      "real plugins activate outgoing 2x and incoming 0.5x");
  check(!p->feature_set_option(p->ctx,out,"damage","multiplier","100000"),
      "launcher rejects out-of-range multiplier");
  const unsigned scales[] = {10,25,50,100,125,200,1000};
  for (unsigned path = 0; path < 3; ++path) for (unsigned i = 0; i < 7; ++i) {
    MmxDamageSetDealt(scales[i]);
    weakness_actor_setup(2,9,0,4,0); /* Fire Wave against Chill Penguin: 3 HP. */
    damage_call(path,0,0);
    unsigned expected = 3 * scales[i] / 100;
    check((g_ram[0xe8f]&127) == 32-expected,"native X1 weakness damage scales in each execution path");
    for (unsigned row = 0; row <= 5; row += 5) {
      weakness_actor_setup(2,row,0,4,0); damage_call(path,0,0);
      check((g_ram[0xe8f]&127) == 32,"multipliers preserve immunity and reflection");
    }
  }
  const char *x2=getenv("MMX_DAMAGE_X2_ASSETS"), *x3=getenv("MMX_DAMAGE_X3_ASSETS");
  if (x2 && x3) {
    check(MmxWeaponsLoadPage(x2,1) && MmxWeaponsLoadPage(x3,2),"damage test loads imported weapons");
    for (unsigned path=0;path<3;++path) for (unsigned i=0;i<sizeof(weakness_cases)/sizeof(*weakness_cases);++i) {
      const WeaknessCase *w=weakness_cases+i;
      for (unsigned charged=0;charged<2;++charged) {
        MmxDamageSetDealt(200); weakness_actor_setup(w->boss,w->row,w->page,w->weapon,charged);
        damage_call(path,0,0);
        check((g_ram[0xe8f]&127)==32-2*(charged?w->charged:w->normal),
            "imported normal and charged weakness damage is multiplied once");
      }
    }
    MmxWeaponsDisable();
  }
  const char *zero = getenv("MMX_DAMAGE_ZERO_ASSETS");
  if (zero) {
    check(MmxZeroLoad(zero), "damage test loads Zero's original assets");
    for (unsigned character=0;character<2;++character) for (unsigned path=0;path<3;++path) {
      MmxZeroState z={0}; z.active_x=character==0; MmxZeroSetState(z);
      MmxDamageSetDealt(200); weakness_actor_setup(2,9,0,4,0); damage_call(path,0,0);
      check((g_ram[0xe8f]&127)==(character?29:26),
          "real character selection multiplies X attacks and preserves Zero attacks");
    }
    MmxZeroDisable();
  }
  MmxDamageReset();
  hitbox_boot();
  unsigned enemy=0;
  for (unsigned i=0;i<400 && !enemy;++i) {
    frame(SNES_PAD_RIGHT);
    if (!g_ram[0xbb6] || g_ram[0xba9]!=2 || g_ram[0xbaa]==4) continue;
    for (unsigned d=0xe68;d<0x1228;d+=64)
      if (g_ram[d] && g_ram[d+14] && (g_ram[d+0x27]&127) &&
          (g_ram[d+0x20]|g_ram[d+0x21])) { enemy=d; break; }
  }
  check(enemy!=0,"real native enemy found for damage contact");
  static uint8_t world[0x20000]; memcpy(world,g_ram,sizeof(world));
  for (unsigned kind=1;kind<=2;++kind) for (unsigned path=0;path<2;++path)
      for (unsigned armor=0;armor<2;++armor) for (unsigned i=0;i<7;++i) {
    memcpy(g_ram,world,sizeof(world));
    MmxDamageSetReceived(scales[i]);
    memcpy(g_ram+0xbad,g_ram+enemy+5,2);memcpy(g_ram+0xbb0,g_ram+enemy+8,2);
    g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbcf]=32;g_ram[0xbce]=8;
    g_ram[enemy+0x26]=8;g_ram[0x1f99]=armor?4:0;
    damage_call(path,kind,enemy);
    unsigned expected=(armor?4:8)*scales[i]/100;
    if ((g_ram[0xbcf]&127)!=(expected>=32?0:32-expected))
      fprintf(stderr,"damage mismatch kind=%u path=%u armor=%u scale=%u enemy=%04x class=%02x HP=%u expected=%u scratch=%u hurt=%u\n",
          kind,path,armor,scales[i],enemy,g_ram[enemy+10],g_ram[0xbcf]&127,
          expected>=32?0:32-expected,g_ram[0],g_ram[0xbaa]);
    check((g_ram[0xbcf]&127)==(expected>=32?0:32-expected),
        "native enemy and scripted damage scale after armor without health wrap");
  }
  for(unsigned kind=1;kind<=2;++kind) for(unsigned path=0;path<2;++path) {
    memcpy(g_ram,world,sizeof(world));MmxDamageSetReceived(10);
    memcpy(g_ram+0xbad,g_ram+enemy+5,2);memcpy(g_ram+0xbb0,g_ram+enemy+8,2);
    g_ram[0xbcf]=16;g_ram[0xbce]=2;g_ram[enemy+0x26]=2;g_ram[0x1f99]=0;
    uint8_t *snapshot=malloc(2*1024*1024);size_t saved=0;
    check(snapshot!=NULL,"fractional damage snapshot allocates");
    for(unsigned hit=0;hit<10;++hit) {
      g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbcf]&=127;
      damage_call(path,kind,enemy);
      if((g_ram[0xbcf]&127)!=16-(hit+1)*2/10)
        fprintf(stderr,"fraction hit=%u kind=%u path=%u HP=%u carry=%u scratch=%u contact=%u actor=%u\n",
            hit,kind,path,g_ram[0xbcf]&127,MmxDamageGetState().received[0],g_ram[0xbce],g_ram[enemy+0x26],g_ram[0xbaa]);
      check((g_ram[0xbcf]&127)==16-(hit+1)*2/10,
          "0.1x native damage loses exactly 1 HP per five 2-HP hits");
      if(hit==2) {
        frame(0);
        saved=RtlSaveSnapshotToMemory(snapshot,2*1024*1024);
        check(saved!=0 && MmxDamageGetState().received[0]==60,
            "save captures a partial incoming HP");
      }
    }
    check(RtlLoadSnapshotFromMemory(snapshot,saved) && MmxDamageGetState().received[0]==60,
        "current-build save/load restores fractional damage");
    for(unsigned hit=0;hit<2;++hit) {
      g_ram[0xc30]=g_ram[0xc32]=0;g_ram[0xbcf]&=127;damage_call(path,kind,enemy);
    }
    check((g_ram[0xbcf]&127)==15 && MmxDamageGetState().received[0]==0,
        "replaying restored fractional damage reaches the same HP");
    free(snapshot);
  }
  for (unsigned path=0;path<2;++path) for (unsigned armor=0;armor<2;++armor) {
    memcpy(g_ram,world,sizeof(world));g_ram[0xbcf]=32;g_ram[0xbce]=127;g_ram[0x1f99]=armor?4:0;
    MmxDamageSetReceived(10);damage_call(path,2,enemy);
    check(!(g_ram[0xbcf]&127),"0.1x keeps native instant-death hazards fatal with or without armor");
  }
  check(p->feature_enable(p->ctx,out,"damage",0) && p->feature_enable(p->ctx,in,"damage",0) &&
      p->commit(p->ctx,rom_path),"launcher disables both mods");
  snes_mod_runtime_activate_plugins_c();
  check(MmxDamageDealt(g_ram,0xe68,4)==4 && MmxDamageReceived(g_ram,4,false)==4,
      "disabling mods resets both multipliers to native behavior");
  puts("MMX DAMAGE RUNTIME CHECKS PASSED");
}
