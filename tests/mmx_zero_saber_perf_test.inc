/* Compare the same ROM-backed scene/input with developer diagnostics off/on.
 * Readiness is a fixture shortcut; Y still starts the real X3 Zero swing. */
#include "snes/tier2_capture.h"
#include "saber/mmx_saber_plugin.h"
#include "saber/mmx_saber_attack.h"
static void saber_perf_activate(void) {
  const char *root=getenv("MMX_PERF_MOD_ROOT");
  check(root && snes_mod_runtime_initialize_c(root,"megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),
      "performance test opens its private mod catalog");
  const RecompLauncherCModProvider *p=snes_mod_runtime_launcher_provider_c();
  check(p && p->feature_enable(p->ctx,"megaman-x.character.zero","zero",1) &&
      p->feature_set_option(p->ctx,"megaman-x.character.zero","zero","behavior","saber") &&
      p->feature_resource_set_path(p->ctx,"megaman-x.character.zero","zero","x3-rom",getenv("MMX_PERF_X3_ROM")) &&
      p->commit(p->ctx,getenv("MMX_PERF_X1_ROM")),"performance test selects Saber Zero and the original X3 ROM");
  check(MmxSaberActivate("megaman-x.character.zero","zero",MmxCoopEnabled()),
      "performance test activates the complete Saber Zero renderer and controller");
}
static int saber_perf_compare(const void *a,const void *b) {
  double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
static MmxZeroState saber_perf_zero(void) {
  MmxCoopState c=MmxCoopGetState();
  return MmxCoopEnabled() && c.current!=0?c.players[0].zero:MmxZeroGetState();
}
static void saber_perf_set_zero(MmxZeroState z) {
  if(MmxCoopEnabled()) {
    MmxCoopState c=MmxCoopGetState();c.players[0].zero=z;MmxCoopSetState(&c);
    if(!c.current)MmxZeroSetState(z);
  } else MmxZeroSetState(z);
}
static void saber_perf_checks(const char *scene,uint8_t *start,uint8_t *expected,
                              uint8_t *actual,size_t cap) {
  enum { N=768 };
  check(getenv("MMX_REPLAY_RECORD") && !strcmp(getenv("MMX_REPLAY_RECORD"),"0"),
      "automatic replay recording is disabled for the diagnostics-off comparison");
  static uint32_t pixels[MMX_RENDER_MAX_WIDTH*224];
  double times[N];
  MmxZeroState z=saber_perf_zero();z.active_x=0;saber_perf_set_zero(z);
  RtlEnableExtendedFrameTiming();
  MmxZeroSetModern(false);frame(0);
  check(MmxCoopEnabled()?MmxCoopGetState().players[0].character==MMX_COOP_ZERO:MmxZeroActive(),
      "performance fixture uses original X3 Zero as the attacking player");
  bool saber=getenv("MMX_PERF_MOD_ROOT")!=NULL;
  if(saber)saber_perf_activate();
  size_t n=RtlSaveSnapshotToMemory(start,cap),en=0;
  FILE *csv=fopen("saber-performance.csv","w");check(csv!=NULL,"performance results open");
  fputs("scene,moveset,players,diagnostics,attack,frames,swing_frames,damage_observed,mean_ms,p50_ms,p95_ms,max_ms,simulation_ms,draw_ms,max_guest_periods\n",csv);
  for(unsigned attack=0;attack<(saber?3:4);++attack) for(unsigned pass=0;pass<3;++pass) {
    bool warmup=pass==0;unsigned diagnostics=pass!=1,frames=attack==3?N:N/2;
    check(RtlLoadSnapshotFromMemory(start,n),"performance scenario restores the identical machine");
    tier2_capture_configure(1,0,(int)diagnostics);
    MmxReplayEnable(diagnostics!=0);MmxCoopSetDiagnosticsEnabled(diagnostics!=0);
    double total=0,sim=0,draw=0,periods=0;
    unsigned swings=0,damage=0;bool rendered=true;
    for(unsigned i=0;i<frames;++i) {
      /* Same protection and attack input for both logging modes. */
      g_ram[0xbcf]=0xa0;
      /* Let the full input chain finish while bosses remain live. This
       * protects the player; charge and projectile prerequisites stay real. */
      if(attack==3)g_ram[0xbd8]=120;
      if(MmxCoopEnabled()) {
        MmxCoopState c=MmxCoopGetState();
        for(unsigned seat=0;seat<2;++seat)c.players[seat].body[0x27]=0xa0;
        if(attack==3)c.players[0].body[0x30]=120;
        MmxCoopSetState(&c);
      }
      unsigned enemy=0,hp=0;
      if(attack==2)for(unsigned d=0xe68;d<0x1228;d+=64)
        if(g_ram[d] && (g_ram[d+0x27]&127) && (g_ram[d+0x20]|g_ram[d+0x21])) {enemy=d;break;}
      if(enemy)hp=g_ram[enemy+0x27]&127;
      if(attack==2 && enemy && !(i%48)) {
        if(!MmxCoopEnabled() || !MmxCoopGetState().current) {
          eagle_putword(g_ram+0xbad,eagle_word(g_ram+enemy+5)-24);
          eagle_putword(g_ram+0xbb0,eagle_word(g_ram+enemy+8)+12);g_ram[0xc11]=64;
        }
        if(MmxCoopEnabled()) {
          MmxCoopState c=MmxCoopGetState();
          eagle_putword(c.players[0].body+5,eagle_word(g_ram+enemy+5)-24);
          eagle_putword(c.players[0].body+8,eagle_word(g_ram+enemy+8)+12);
          c.players[0].body[0x69]=64;MmxCoopSetState(&c);
        }
      }
      if(attack && attack!=3 && !saber && !(i%48)) {
        z=saber_perf_zero();z.combo=2;z.saber_ready=1;saber_perf_set_zero(z);
      }
      unsigned keys=attack && !(i%48)?SNES_PAD_Y:0;
      if(attack==3) {
        /* Exercise both busters and the saber through ordinary input. This
         * case bypasses neither charge time nor projectile prerequisites. */
        z=saber_perf_zero();keys=0;
        if(!z.slash && !z.burst)keys=z.combo?(i&1?0:SNES_PAD_Y):(z.charge<201?SNES_PAD_Y:0);
      }
      uint64_t begin=SDL_GetPerformanceCounter();
      frame(keys);
      uint64_t middle=SDL_GetPerformanceCounter();
      /* Exercise the product's presenter. Native frame producers may have
       * already composed the captured image; drawing it again here would
       * charge a second composition that ordinary play does not perform. */
      rendered &= s_render_valid && MmxDrawFrame((uint8_t *)pixels,
          (size_t)g_mmx_custom_view.width * 4, g_my_pixels,
          g_mmx_custom_view.width, 224, 0) != 0;
      uint64_t end=SDL_GetPerformanceCounter();
      double scale=1000.0/(double)SDL_GetPerformanceFrequency();
      times[i]=(end-begin)*scale;total+=times[i];sim+=(middle-begin)*scale;draw+=(end-middle)*scale;
      if(RtlLastFramePeriods()>periods)periods=RtlLastFramePeriods();
      swings+=saber?MmxSaberAttackSnapshotGet().phase!=SABER_PHASE_IDLE:saber_perf_zero().slash!=0;
      if(enemy && (g_ram[enemy+0x27]&127)<hp)damage+=hp-(g_ram[enemy+0x27]&127);
    }
    if(warmup) {MmxReplayEnable(false);continue;}
    qsort(times,frames,sizeof(*times),saber_perf_compare);
    check(rendered,"all measured performance frames render");
    fprintf(csv,"%s,%s,%u,%u,%u,%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        scene,saber?"saber":"x3",MmxCoopEnabled()?2:1,diagnostics,attack,frames,swings,damage,total/frames,times[frames/2],times[frames*95/100],times[frames-1],sim/frames,draw/frames,periods);
    fflush(csv);
    fprintf(stderr,"saber-perf scene=%s moveset=%s diagnostics=%u attack=%u swing=%u damage=%u mean=%.3f p95=%.3f max=%.3f sim=%.3f draw=%.3f guest=%.3f\n",
        scene,saber?"saber":"x3",diagnostics,attack,swings,damage,total/frames,times[frames*95/100],times[frames-1],sim/frames,draw/frames,periods);
    if(attack==1)check(swings>=100,"measured frames include repeated real saber swings");
    if(attack==2 && !strcmp(scene,"boss-rush"))check(damage>0,"measured attack frames deliver real boss damage");
    if(attack==3)check(swings>0,"ordinary full-charge/two-buster input reaches and measures the X3 saber");
    if(!diagnostics)en=RtlSaveSnapshotToMemory(expected,cap);
    else {
      size_t an=RtlSaveSnapshotToMemory(actual,cap);
      same(expected,en,actual,an,"diagnostics do not change the repeated saber simulation");
    }
    MmxReplayEnable(false);
  }
  fclose(csv);tier2_capture_configure(1,0,0);
  puts("MMX X3 ZERO SABER PERFORMANCE CHECKS PASSED");
}
