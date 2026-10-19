/* Exercise the original collision routine, not just the mapping helper.
 * Run from an empty private working directory with both owner-extracted packs. */
extern RecompReturn bank_84_9E15_M1X0(CpuState *cpu);
typedef struct WeaknessCase {
  const char *name;
  uint8_t boss, row, page, weapon, normal, charged, reaction;
} WeaknessCase;
static const WeaknessCase weakness_cases[] = {
  {"Penguin / Speed Burner",0x02,9,1,8,3,4,0x0a},
  {"Mandrill / Strike Chain",0x31,13,1,6,3,4,0},
  {"Mandrill / Frost Shield",0x31,13,2,7,3,4,7},
  {"Armadillo / Triad Thunder",0x14,11,2,3,3,6,0x0c},
  {"Octopus / Triad Thunder",0x07,7,2,3,3,3,0x0c},
  {"Octopus / Electric Spark",0x07,7,0,6,3,3,0x0c},
  {"Kuwanger / Parasitic Bomb",0x05,10,2,2,3,4,0},
  {"Chameleon / Sonic Slicer",0x0a,6,1,5,3,4,0x0d},
  {"Chameleon / Spinning Blade",0x0a,6,2,4,3,4,0x0d},
  {"Eagle / Spin Wheel",0x52,8,1,4,3,4,0},
  {"Eagle / Sonic Slicer",0x52,8,1,5,3,4,0},
  {"Eagle / Spinning Blade",0x52,8,2,4,3,4,0},
  {"Mammoth / Bubble Splash",0x0c,12,1,2,3,4,0},
};
static unsigned weakness_native_class(unsigned page,unsigned weapon,unsigned charged) {
  static const unsigned x1[]={0,14,11,8,10,9,12,13,7};
  unsigned cls=page==0 ? x1[weapon] : page==1 && weapon==8 ? 10 :
      page==2 && weapon==3 ? 12 : 0;
  return cls && charged ? cls+9 : cls;
}
static void weakness_actor_setup(unsigned boss,unsigned row,unsigned page,unsigned weapon,unsigned charged) {
  const unsigned enemy=0xe68,d=0x1228;
  memset(g_ram+enemy,0,64);memset(g_ram+d,0,64);
  g_ram[enemy]=1;g_ram[enemy+10]=(uint8_t)boss;
  g_ram[enemy+0x27]=32;g_ram[enemy+0x28]=(uint8_t)row;
  g_ram[d]=1;g_ram[d+1]=2;
  g_ram[d+10]=(uint8_t)weakness_native_class(page,weapon,charged);
  MmxWeaponCombatState c={0};
  if(page) {
    static const uint8_t groups[2][9]={{0,16,68,72,70,65,71,15,37},{0,5,6,11,12,13,15,16,19}};
    unsigned group=groups[page-1][weapon];
    if(charged && page==1 && weapon==5) group=135;
    if(charged && page==1 && weapon==7) group=19;
    if(charged && page==1 && weapon==8) group=38;
    if(charged && page==2 && weapon==3) group=51;
    g_ram[d+0x3e]=0x58;g_ram[d+0x3f]=0x57;
    c.shots[0]=(MmxWeaponShot){.active=1,.page=(uint8_t)page,.group=(uint8_t)group,
        .weapon=(uint8_t)weapon,.charged=(uint8_t)charged};
  }
  if(!MmxWeaponsValidCombatState(&c)) check(0,"valid imported projectile fixture");
  MmxWeaponsSetCombatState(c);
}
static void weakness_native_contact(unsigned path) {
  CpuState cpu=g_cpu;
  cpu.S=0x1ff;cpu.D=0xe68;cpu.X=0x1228;cpu.Y=0;cpu.DB=0x86;
  cpu.PB=0x84;cpu.P=0x20;cpu.emulation=0;
  cpu_p_to_mirrors(&cpu);cpu_push_jsl_return_frame(&cpu);
  interp_bridge_set_master_deadline(0);
  int ok=path==0 ? interp_bridge_run(&cpu,0x849e15) :
      bank_84_9E15_M1X0(&cpu)==RECOMP_RETURN_NORMAL;
  if(!ok || cpu.S!=0x1ff) {
    fprintf(stderr,"collision path %u returned %d with stack %04x\n",path,ok,cpu.S);
    check(0,"native collision returns with balanced stack");
  }
}
static void weapon_weakness_checks(void) {
  const char *x2=getenv("MMX_WEAKNESS_X2_ASSETS"),*x3=getenv("MMX_WEAKNESS_X3_ASSETS");
  check(x2 && x3,"both private source asset paths supplied");
  check(MmxWeaponsLoadPage(x2,1) && MmxWeaponsLoadPage(x3,2),"X2 and X3 packs load");
  MmxZeroRegisterHooks();
  for(unsigned path=0;path<2;++path) {
    for(unsigned i=0;i<sizeof(weakness_cases)/sizeof(weakness_cases[0]);++i) {
      const WeaknessCase *w=weakness_cases+i;
      for(unsigned charged=0;charged<2;++charged) {
        weakness_actor_setup(w->boss,w->row,w->page,w->weapon,charged);
        unsigned native=g_ram[0x1232],damage=charged?w->charged:w->normal;
        weakness_native_contact(path);
        unsigned reaction=w->reaction ? w->reaction+charged*9 : 0;
        if((g_ram[0xe8f]&127)!=32-damage || g_ram[0x1f1d]!=reaction || g_ram[0x1232]!=native) {
          fprintf(stderr,"%s charged=%u path=%u: HP=%u reaction=%02x (wanted %u/%02x)\n",
              w->name,charged,path,g_ram[0xe8f]&127,g_ram[0x1f1d],32-damage,reaction);
          check(0,"native weakness damage and reaction");
        }
        /* Invulnerable and reflecting native rows still own the result. */
        for(unsigned row=0;row<=5;row+=5) {
          weakness_actor_setup(w->boss,row,w->page,w->weapon,charged);
          weakness_native_contact(path);
          check((g_ram[0xe8f]&127)==32,"weakness preserves native invulnerability/reflection");
        }
      }
      printf("ok: %s normal/charged, native path %u\n",w->name,path);
    }
    static const WeaknessCase originals[]={
      {"Fire Wave",2,9,0,4,3,4,10},{"Shotgun Ice",0x31,13,0,8,3,4,7},
      {"Electric Spark",0x14,11,0,6,3,6,12},{"Rolling Shield",7,7,0,3,3,0,8},
      {"Homing Torpedo",5,10,0,1,3,4,14},{"Boomerang Cutter",10,6,0,7,3,4,13},
      {"Chameleon Sting",0x52,8,0,2,3,4,11},{"Storm Tornado",12,12,0,5,3,4,9},
    };
    for(unsigned i=0;i<sizeof(originals)/sizeof(originals[0]);++i) for(unsigned charged=0;charged<2;++charged) {
      const WeaknessCase *w=originals+i;
      weakness_actor_setup(w->boss,w->row,0,w->weapon,charged);
      weakness_native_contact(path);
      check((g_ram[0xe8f]&127)==32-(charged?w->charged:w->normal) &&
          g_ram[0x1f1d]==w->reaction+9*charged,"original X1 weakness unchanged");
    }
    /* Cutter still reaches the original Octopus/Mammoth appendage reactions. */
    for(unsigned boss=7;boss<=12;boss+=5) {
      weakness_actor_setup(boss,boss,0,7,0);weakness_native_contact(path);
      check(g_ram[0x1f1d]==13,"original Boomerang appendage reaction preserved");
    }
    weakness_actor_setup(0x02,9,1,5,0);weakness_native_contact(path);
    check((g_ram[0xe8f]&127)==31 && !g_ram[0x1f1d],"unlisted boss/weapon pairing unchanged");
    /* Source IDs differ from pause-menu order: do not confuse Blade with
     * Ray Splasher, Wheel with Bubble Splash, or Bubble with Magnet Mine. */
    static const uint8_t nonweak[][4]={{10,6,2,5},{0x52,8,2,5},{0x52,8,1,2},{12,12,1,7}};
    for(unsigned i=0;i<sizeof(nonweak)/sizeof(nonweak[0]);++i) {
      const uint8_t *n=nonweak[i];weakness_actor_setup(n[0],n[1],n[2],n[3],0);
      /* Magnet Mine's normal sprite group is already supplied by setup. */
      weakness_native_contact(path);
      check((g_ram[0xe8f]&127)==31,"neighboring source weapon ID does not inherit a weakness");
    }
    weakness_actor_setup(0x02,9,1,8,0);
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();c.shots[0].variant=2;
    MmxWeaponsSetCombatState(c);g_ram[0x1232]=0;weakness_native_contact(path);
    check((g_ram[0xe8f]&127)==31 && !g_ram[0x1f1d],"underwater Speed Burner is not a fire weakness");
  }
  MmxWeaponsDisable();
  weakness_actor_setup(7,7,0,6,0);weakness_native_contact(0);
  check((g_ram[0xe8f]&127)==31 && g_ram[0x1f1d]==12,"disabled weapon mods preserve stock Octopus/Spark damage");
  puts("MMX WEAKNESS CHECKS PASSED");
}
