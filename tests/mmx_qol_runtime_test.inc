/* These controls execute cartridge instructions through the normal bridge.
 * Temporary stops isolate a completed native store; they never grant an
 * outcome, award or inventory item, and are inactive outside this fixture. */
#include "mmx_qol.h"
#include "mmx_dash.h"
static uint32_t qol_stop_pc;
static bool qol_stop_seen;
static void qol_stop(CpuState *cpu,uint32_t pc) {
  (void)cpu;
  if((pc&0x7fffff)!=qol_stop_pc)return;
  qol_stop_seen=true;interp_bridge_pre_opcode_redirect(0x8196d4);
}
static CpuState qol_call(unsigned pc,unsigned d,bool wide,unsigned stop) {
  CpuState cpu=g_cpu;
  uint8_t stack[256];memcpy(stack,g_ram+0x100,sizeof(stack));
  cpu.PB=(uint8_t)(pc>>16);cpu.DB=0x86;cpu.D=(uint16_t)d;cpu.S=0x1ff;
  cpu.P=wide?0x10:0x30;cpu.emulation=0;cpu.X=cpu.Y=0;
  cpu.cycles=cpu.master_cycles=cpu.coprocessor_master_cycles=0;
  cpu_p_to_mirrors(&cpu);cpu_push_jsr_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  qol_stop_pc=stop&0x7fffff;qol_stop_seen=false;
  if(stop)check(interp_bridge_add_pre_opcode_hook(stop,qol_stop),"native fixture stop installs");
  check(interp_bridge_run(&cpu,pc)==1 && cpu.S==0x1ff,"native convenience routine returns balanced");
  if(stop)check(qol_stop_seen,"native completed the observed stores");
  qol_stop_pc=0;memcpy(g_ram+0x100,stack,sizeof(stack));
  return cpu;
}
static void qol_compiled_probe(void) {
  extern const char *g_last_recomp_func;
  MmxQolReset();MmxQolEnable(MMX_QOL_NO_DOUBLE_TAP);
  check(MmxQolRegisterHooks(),"compiled probe installs ordinary interpreter hooks too");
  g_ram[0x1f99]=8;g_ram[0x1f23]=0;g_ram[0xbfc]=g_ram[0xbf9]=g_ram[0xbfb]=0;
  g_ram[0xbe3]=1;
  CpuState native=g_cpu;native.PB=0x81;native.DB=0x86;native.D=0xba8;native.S=0x1ff;
  native.P=0x30;native.X=native.Y=0;cpu_p_to_mirrors(&native);
  native.cycles=native.master_cycles=native.coprocessor_master_cycles=0;
  check(cpu_dispatch_has_entry(&native,0x81971c),"real compiled double-tap body exists");
  check(cpu_dispatch_call_pc(&native,0x81971c,0x818000)==RECOMP_RETURN_NORMAL && native.S==0x1ff,
        "real compiled double-tap call returns balanced");
  printf("compiled double-tap detector=%u timer=%u last=%s\n",g_ram[0xbfc],g_ram[0xbf9],g_last_recomp_func);
  if(getenv("MMX_QOL_EXPECT_AOT_GAP"))check(g_ram[0xbfc]==2,"baseline proves interpreter-only control is bypassed");
  else check(!g_ram[0xbfc] && !g_ram[0xbf9],"candidate compiled owner honors disabled double-tap");
}
static void qol_air_state_checks(void) {
  RtlEnableExtendedFrameTiming();
  hitbox_boot();
  for(unsigned n=0;n<240 && (g_ram[0xba9]!=2 || g_ram[0xbaa] || !(g_ram[0xbd3]&4));++n)
    frame(0);
  zero_replay(60);
  check(g_ram[0xba9]==2 && !g_ram[0xbaa] && (g_ram[0xbd3]&4),"X lands ready in the native Highway field");
  /* Set only the existing prerequisite. The native controller and player
   * owners start the jump, transition into the dash and advance the phase. */
  g_ram[0x1f99]|=8;
  MmxDashEnable(MMX_DASH_AIR);
  check(MmxQolRegisterHooks(),"standalone Air Dash hooks register");
  for(unsigned n=0;n<10;++n)frame(SNES_PAD_B);
  check(!(g_ram[0xbd3]&4),"mapped Jump leaves the ground");
  frame(SNES_PAD_A|SNES_PAD_RIGHT);
  MmxDashState phase=MmxDashGetState();
  check(phase.used && phase.ticks && g_ram[0xbaa]==0x14,"mapped Dash starts a native air dash");
  size_t capacity=2u*1024u*1024u;
  uint8_t *state=malloc(capacity),*rollback=malloc(capacity);
  uint8_t *expected=malloc(capacity),*actual=malloc(capacity);
  check(state && rollback && expected && actual,"current state buffers allocated");
  size_t saved=RtlSaveSnapshotToMemory(state,capacity);
  size_t rolled=RtlRollbackSaveToMemory(rollback,capacity);
  check(saved && rolled,"current state and rollback capture an active dash");
  for(unsigned n=0;n<6;++n)frame(SNES_PAD_A|SNES_PAD_RIGHT);
  size_t end=RtlSaveSnapshotToMemory(expected,capacity);
  check(RtlLoadSnapshotFromMemory(state,saved),"restore a current mid-dash state");
  MmxDashState restored=MmxDashGetState();
  check(!memcmp(&phase,&restored,sizeof(phase)),"state restores all Air Dash phase bytes");
  for(unsigned n=0;n<6;++n)frame(SNES_PAD_A|SNES_PAD_RIGHT);
  size_t count=RtlSaveSnapshotToMemory(actual,capacity);
  same(expected,end,actual,count,"current-state Air Dash continuation matches");
  check(RtlRollbackLoadFromMemory(rollback,rolled),"restore a current mid-dash rollback");
  restored=MmxDashGetState();
  check(!memcmp(&phase,&restored,sizeof(phase)),"rollback restores all Air Dash phase bytes");
  for(unsigned n=0;n<6;++n)frame(SNES_PAD_A|SNES_PAD_RIGHT);
  count=RtlSaveSnapshotToMemory(actual,capacity);
  same(expected,end,actual,count,"rollback Air Dash continuation matches");
  free(actual);free(expected);free(rollback);free(state);
  puts("MMX QOL AIR STATE AND ROLLBACK PASS");
}
extern RecompReturn bank_84_9E15_M1X0(CpuState *cpu);
static bool qol_apply_boss_contact(unsigned enemy,unsigned shot,bool compiled) {
  CpuState cpu=g_cpu;
  cpu.S=0x1ff;cpu.D=(uint16_t)enemy;cpu.X=(uint16_t)shot;
  cpu.DB=0x86;cpu.PB=0x84;cpu.P=0x20;cpu.emulation=0;
  cpu.cycles=cpu.master_cycles=cpu.coprocessor_master_cycles=0;
  cpu_p_to_mirrors(&cpu);cpu_push_jsl_return_frame(&cpu);interp_bridge_set_master_deadline(0);
  bool ok=compiled ? bank_84_9E15_M1X0(&cpu)==RECOMP_RETURN_NORMAL : interp_bridge_run(&cpu,0x849e15);
  return ok && cpu.S==0x1ff;
}
static void qol_boss_hit_checks(void) {
  enum { enemy=0xe68, shot=0x1228 };
  static const struct {
    unsigned feature, actor, table, projectile, stage;
    bool lethal;
    const char *description;
  } cases[]={
    {0,0x14,5,4,3,false,"Off keeps the hitstun contact immune to Hadouken"},
    {MMX_QOL_HADOUKEN_IFRAMES,0x14,5,0,3,false,"On keeps a Buster pellet blocked during hitstun"},
    {MMX_QOL_HADOUKEN_IFRAMES,0x14,5,4,3,true,"On lets Hadouken kill the hitstun boss"},
    {0,0x65,19,4,12,false,"Off keeps Wolf Sigma immune to Hadouken"},
    {MMX_QOL_HADOUKEN_SIGMA,0x65,19,0,12,false,"On keeps Wolf Sigma's ordinary Buster response"},
    {MMX_QOL_HADOUKEN_SIGMA,0x65,19,4,12,true,"On lets Hadouken kill the Wolf Sigma head"},
  };
  static uint8_t original[0x20000];
  memcpy(original,g_ram,sizeof(original));
  for(unsigned path=0;path<2;++path)for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
    memcpy(g_ram,original,sizeof(original));
    MmxQolReset();MmxQolEnable(cases[i].feature);
    check(MmxQolRegisterHooks(),"native contact hooks register");
    /* Feed the already-overlapping original projectile and the real native
     * boss damage mode to the cartridge contact handler. The handler owns
     * health, hits, reflection and the lethal marker. */
    g_ram[0x1f7a]=(uint8_t)cases[i].stage;
    g_ram[0x1f0e]=(uint8_t)(enemy&255);g_ram[0x1f0f]=(uint8_t)(enemy>>8);
    g_ram[enemy]=1;g_ram[enemy+1]=2;g_ram[enemy+10]=(uint8_t)cases[i].actor;
    g_ram[enemy+39]=16;g_ram[enemy+40]=(uint8_t)cases[i].table;
    g_ram[shot]=1;g_ram[shot+10]=(uint8_t)cases[i].projectile;
    check(qol_apply_boss_contact(enemy,shot,path!=0),"native contact returns balanced");
    /* Non-damaging contact can still set the native hit marker in bit 7. */
    bool expected=cases[i].lethal ? g_ram[enemy+39]==0x80 : (g_ram[enemy+39]&127)==16;
    if(!expected)fprintf(stderr,"native contact path=%u case=%u HP=%u action=%u\n",
        path,i,g_ram[enemy+39],g_ram[enemy+1]);
    check(expected,cases[i].description);
  }
  puts("MMX QOL NATIVE BOSS CONTACT PASS");
}
static void qol_armadillo_defeat(const char *fixture,const char *projectile) {
  enum { enemy=0xe68, shot=0x1228 };
  RtlEnableExtendedFrameTiming();
  check(RtlLoadSnapshot(fixture),"current original Armadillo encounter loads");
  check(g_ram[0x1f7a]==3 && g_ram[enemy] && g_ram[enemy+10]==0x14 &&
        g_ram[enemy+1]>=4 && (g_ram[enemy+39]&127)==32 && !(g_ram[0x1f8c]&64),
        "the active boss finished native HP fill and the stage is unbeaten");
  MmxQolReset();MmxQolEnable(MMX_QOL_HADOUKEN_IFRAMES);
  check(MmxQolRegisterHooks(),"native Hadouken contact hooks register");
  FILE *input=projectile?fopen(projectile,"rb"):NULL;
  check(input && !fseek(input,shot,SEEK_SET) && fread(g_ram+shot,1,64,input)==64,
        "read the projectile emitted by native Hadouken input");
  fclose(input);
  check(g_ram[shot] && g_ram[shot+10]==4,"the recorded native Hadouken projectile is active");
  /* Stage the original row selected during hitstun. Inject the captured
   * native projectile at the damage owner to skip overlap detection, then
   * enter Armadillo's native post-contact death check with its actual HP. */
  g_ram[enemy+0x28]=5;
  g_ram[enemy+0x35]=g_ram[enemy+0x38]=g_ram[enemy+0x3d]=0x46;
  check(qol_apply_boss_contact(enemy,shot,true),"Hadouken executes the compiled native contact");
  check((g_ram[enemy+39]&127)==0,"native contact makes the live boss lethal");
  check(!g_ram[enemy+0x35] && !g_ram[enemy+0x38] && !g_ram[enemy+0x3d],
        "lethal contact releases the native post-hit waits");
  qol_call(0x83b3a0,enemy,false,0x83b3c3);
  check(g_ram[enemy+1]==6 && g_ram[0x1f0c]==1,
        "Armadillo's original death check starts the native defeat event");
  for(unsigned frame_count=0;frame_count<1800 && !(g_ram[0x1f8c]&64);++frame_count)
    frame(frame_count%60==0 ? SNES_PAD_Y|SNES_PAD_B : 0);
  printf("native defeat: HP=%u, action=%u, phase=%u, stage flags=%02x, mode=%u/%u/%u/%u\n",
      g_ram[enemy+39],g_ram[enemy+1],g_ram[enemy+2],g_ram[0x1f8c],g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0xd4]);
  check((g_ram[0x1f8c]&64)!=0,"native boss AI completes defeat and awards Armadillo progress");
}
static void qol_native_checks(void) {
  if(getenv("MMX_QOL_COMPILED_PROBE")) {qol_compiled_probe();return;}
  if(getenv("MMX_QOL_AIR_STATE")) {qol_air_state_checks();return;}
  if(getenv("MMX_QOL_BOSS_CONTACT")) {qol_boss_hit_checks();return;}
  const char *armadillo=getenv("MMX_QOL_ARMADILLO_DEFEAT");
  if(armadillo) {qol_armadillo_defeat(armadillo,getenv("MMX_QOL_HADOUKEN_DUMP"));return;}
  MmxQolReset();check(MmxQolRegisterHooks(),"native QoL hooks register");
  g_ram[0x1f99]=8;g_ram[0x1f23]=0;
  g_ram[0xbfc]=0;g_ram[0xbe3]=1;
  qol_call(0x81971c,0xba8,false,0);
  check(g_ram[0xbfc]==2,"Off native double-tap recognizer accepts direction");
  g_ram[0xbfc]=g_ram[0xbf9]=g_ram[0xbfb]=0;
  MmxQolEnable(MMX_QOL_NO_DOUBLE_TAP);qol_call(0x81971c,0xba8,false,0);
  check(!g_ram[0xbfc] && !g_ram[0xbfb],"On disables native double-tap only");

  MmxQolReset();g_ram[0xbde]=128;g_ram[0xbfe]=g_ram[0xc14]=0;g_ram[0xc10]=0;
  qol_call(0x818777,0xba8,true,0x818795);
  check((g_ram[0xbc2]|g_ram[0xbc3]<<8)==0xfe88,"Off native untimed wall push is slow");
  MmxQolEnable(MMX_QOL_WALL_JUMP);qol_call(0x818777,0xba8,true,0x818795);
  check((g_ram[0xbc2]|g_ram[0xbc3]<<8)==0xfc8b,"On held Dash gives native signed boosted push");
  g_ram[0xbde]=0;qol_call(0x818777,0xba8,true,0x818795);
  check((g_ram[0xbc2]|g_ram[0xbc3]<<8)==0xfe88,"released Dash keeps native slow wall push");

  MmxQolReset();g_ram[0x1f7a]=1;g_ram[0x1f88]=0;
  CpuState native=qol_call(0x80c93c,0xa08,false,0);
  check(!(native.A&255),"Off native Escape rejects unbeaten stage");
  MmxQolEnable(MMX_QOL_ESCAPE);native=qol_call(0x80c93c,0xa08,false,0);
  check((native.A&255)==64,"On native Escape accepts unbeaten stage");
  g_ram[0x1f7a]=0;native=qol_call(0x80c93c,0xa08,false,0);
  check((native.A&255)==64,"On native Escape also accepts intro stage");

  MmxQolReset();g_ram[0xc00]=g_ram[0xbff]=g_ram[0xbdb]=g_ram[0xbdf]=g_ram[0xbe3]=0;
  qol_call(0x81983f,0xba8,false,0);check(!g_ram[0xc00],"Off buster does not charge idle");
  MmxQolEnable(MMX_QOL_AUTO_CHARGE);qol_call(0x81983f,0xba8,false,0);
  check(g_ram[0xc00]==2,"On idle input starts native charge producer");
  for(unsigned i=0;i<30;++i)qol_call(0x81983f,0xba8,false,0);
  check(g_ram[0xc00]==4 && g_ram[0xc03],"native charge reaches its own first threshold");
  g_ram[0xbaa]=0;g_ram[0xc01]=0;g_ram[0xbdf]=g_ram[0xbe3]=64;
  qol_call(0x81983f,0xba8,false,0);
  check(!g_ram[0xc00] && g_ram[0xc01],"real Shot press releases a native charged request");
  check(g_ram[0xbdf]==64 && g_ram[0xbe3]==64,"native charge preserves mapped physical input");

  MmxQolReset();g_ram[0x1f83]=0x8e;g_ram[0xbcf]=0x90;g_ram[0x1f9a]=16;
  g_ram[0xa12]=10;g_ram[0xa18]=1;g_ram[0xa19]=1;
  qol_call(0x80c723,0xa08,false,0);
  check(g_ram[0x1f83]==0x8d,"Off native Sub Tank still consumes at full health");
  MmxQolEnable(MMX_QOL_SUB_TANK);g_ram[0xa18]=1;unsigned remaining=g_ram[0x1f83];
  qol_call(0x80c723,0xa08,false,0);
  check(g_ram[0x1f83]==remaining && !g_ram[0xa0a],"On native menu finish preserves remainder");
  puts("MMX QOL NATIVE HELPER CONTRACTS PASS; full routes remain separate");
}
