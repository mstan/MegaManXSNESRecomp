/* Independent PR review. Expected damage comes from the selected original
 * ROM's table, never from the relocated X1 row or a copied expected constant.
 * Run with both source ROMs and the real launcher character matrix enabled.
 * Cheats queue/position/protect actors; hits, pauses and AI execute natively. */
static unsigned import_review_failures;
static void import_review_save_wide(const char *path,const uint32_t *pixels,unsigned width) {
  FILE *f=fopen(path,"wb");check(f!=NULL,"wide import evidence opens");
  fprintf(f,"P6\n%u 224\n255\n",width);
  for(unsigned i=0;i<width*224;++i) {
    uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
    fwrite(rgb,1,3,f);
  }
  check(!fclose(f),"wide import evidence writes");
}
static void import_review_result(const char *name,bool passed) {
  fprintf(stderr,"IMPORT REVIEW %s: %s\n",passed?"PASS":"FAIL",name);
  if(!passed) ++import_review_failures;
}
static unsigned import_source_damage(unsigned game,unsigned profile,unsigned cls) {
  size_t size=0;const uint8_t *rom=MmxBossRushForeignRom(game,&size);
  unsigned base=game==2?0x373a4:0x364a5;
  check(rom && base+profile*2+2<size,"original damage directory is in the supplied ROM");
  unsigned at=base+eagle_word(rom+base+profile*2)+cls;
  check(at<size,"original damage entry is in the supplied ROM");
  return rom[at];
}
static void import_review_live_hit(uint8_t *snapshot,size_t n,unsigned page,unsigned weapon,bool charged) {
  check(RtlLoadSnapshotFromMemory(snapshot,n),"live projectile case restores combat");
  MmxBossRushState s=MmxBossRushGetState();unsigned d=s.bosses[0].object;
  MmxWeaponsState w=MmxWeaponsGetState();w.initialized=1;w.page=(uint8_t)page;w.weapon=(uint8_t)weapon;
  w.energy[(page-1)*8+weapon-1]=28;MmxWeaponsSetState(w);g_ram[0xbdb]=0;
  g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc2d]=0;g_ram[0xbb9]&=~64u;
  unsigned hp=g_ram[d+0x27]&127,damage=0,shot_charged=0,variant=0;
  for(unsigned t=0;t<420 && !damage;++t) {
    if(s.bosses[0].id==23) {
      g_ram[d+2]=0x0c;g_ram[d+3]=1;g_ram[d+0x39]=100;g_ram[d+0x28]=0x0a;
    }
    for(unsigned object=0xe68;object<0x1628;object+=64) if(object<0x1228 || object>=0x1428)
      if(g_ram[object] && MmxBossRushOwner(object)>=0) g_ram[object+0x26]=0;
    eagle_putword(g_ram+0xbad,eagle_word(g_ram+d+5)-48);
    eagle_putword(g_ram+0xbb0,eagle_word(g_ram+d+8));
    g_ram[0xbb9]&=~64u;
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
      /* The first press emits an ordinary shot before charging. Remove
       * that setup shot while holding; test the actual charged release. */
      if(charged && t<=181) {
        memset(c.shots+i,0,sizeof(c.shots[i]));memset(g_ram+0x1228+i*64,0,64);continue;
      }
      c.shots[i].x=(int32_t)eagle_word(g_ram+d+5)<<8;c.shots[i].y=(int32_t)eagle_word(g_ram+d+8)<<8;
      shot_charged=c.shots[i].charged;variant=c.shots[i].variant;
    }
    MmxWeaponsSetCombatState(c);
    frame(charged?(t<181?SNES_PAD_Y:0):(t%30==0?SNES_PAD_Y:0));rush_keep_players_alive();
    damage=hp-(g_ram[d+0x27]&127);
  }
  unsigned game=s.bosses[0].id==13?2:3;
  unsigned expected=import_source_damage(game,game==2?9:24,6+weapon+9*charged);
  char name[160];snprintf(name,sizeof(name),"live X%u weakness input charged %u: expected %u, got %u (projectile charged %u variant %u)",
      game,charged,expected,damage,shot_charged,variant);
  if(!damage || shot_charged!=(unsigned)charged)
    fprintf(stderr,"IMPORT REVIEW UNVERIFIED: %s; no qualifying projectile contact\n",name);
  else import_review_result(name,damage==expected);
  check(RtlLoadSnapshotFromMemory(snapshot,n),"restore after live projectile contact");
}
static void import_review_pair(uint8_t *snapshot,size_t cap,unsigned first,unsigned second) {
  MmxBossRushState s=MmxBossRushGetState();
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));s.queued=0;
  memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);
  check(MmxBossRushSetState(&s),"review clears the previous encounter");
  MmxBossRushRosterState roster={.available=MMX_RUSH_X1_MASK|(1u<<13)|(1u<<23),.queued=2};
  roster.queue[0]=(uint8_t)first;roster.queue[1]=(uint8_t)second;
  check(MmxBossRushRosterSetState(&roster),"review queues the two imported bosses");
  bool fighting=false;
  for(unsigned t=0;t<2400 && !fighting;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    fighting=s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING;
  }
  check(fighting && s.bosses[0].id==first && s.bosses[1].id==second,"both imported introductions reach combat together");
  check(RtlSaveSnapshotToMemory(snapshot,cap)>0,"combined imported encounter saves");
  check(RtlSaveSnapshot("imports-combat.sav"),"private combined encounter saves to disk");
}
static void import_review_damage(uint8_t *snapshot,size_t n) {
  MmxBossRushState s=MmxBossRushGetState();
  unsigned id=s.bosses[0].id,game=id==13?2:3,row=id==13?8:10;
  check(s.bosses[0].object==0xe68,"review collision uses the real seat-zero object");
  FILE *csv=fopen(id==13?"sponge-damage.csv":"rhino-damage.csv","w");check(csv!=NULL,"damage evidence opens");
  fputs("game,profile,page,weapon,charged,source_class,expected_raw,actual_damage,reaction,passed\n",csv);
  for(unsigned page=0;page<=2;++page) for(unsigned weapon=1;weapon<=8;++weapon)
    for(unsigned charged=0;charged<2;++charged) {
      check(RtlLoadSnapshotFromMemory(snapshot,n),"damage contact restores the real encounter");
      weakness_actor_setup(2,row,page,weapon,charged);
      MmxBossRushHostFrame();weakness_native_contact(0);
      unsigned damage=32-(g_ram[0xe8f]&127),reaction=g_ram[0x1f1d];
      /* Same-game standard projectile families: class $07 + weapon - 1,
       * charged +9. Other-game weaknesses are explicitly unassigned. */
      unsigned cls=6+weapon+9*charged;
      bool same_game=page==game-1;
      unsigned raw=same_game?import_source_damage(game,id==13?9:24,cls):0;
      bool passed=damage==((raw&128)?0:raw);
      /* Other-game observations have no assigned source expectation.
       * Record them as unverified (-1), never as a fabricated pass. */
      fprintf(csv,"%u,%u,%u,%u,%u,%u,%d,%u,%u,%d\n",game,id==13?9:24,page,weapon,charged,cls,
          same_game?(int)raw:-1,damage,reaction,same_game?(int)passed:-1);
      if(page==game-1) {
        char name[128];snprintf(name,sizeof(name),"X%u original damage weapon %u charged %u: expected %u, got %u",
            game,weapon,charged,(raw&128)?0:raw,damage);
        import_review_result(name,passed);
      }
      if(page==0 && id==13 && weapon==2) {
        char name[128];snprintf(name,sizeof(name),"Chameleon Sting is not accidentally Sponge's Slicer weakness (charged %u)",charged);
        import_review_result(name,damage==(charged?2u:1u) && reaction!=0x0b && reaction!=0x14);
      }
    }
  fclose(csv);
  if(id==13) {
    check(RtlLoadSnapshotFromMemory(snapshot,n),"restore for Sponge child defense profile");
    weakness_actor_setup(2,0,1,5,0);MmxBossRushHostFrame();weakness_native_contact(0);
    unsigned source=import_source_damage(2,10,11),damage=32-(g_ram[0xe8f]&127);
    char name[128];snprintf(name,sizeof(name),"Sponge source child defense profile $0A accepts Slicer: expected %u, got %u",source,damage);
    import_review_result(name,source==damage);
  }
  check(RtlLoadSnapshotFromMemory(snapshot,n),"restore combat after the damage matrix");
}
static void import_review_pause(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap,unsigned ordinal) {
  MmxBossRushState s=MmxBossRushGetState();
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"pre-pause combined encounter saves");
  frame(SNES_PAD_START);zero_replay(90);
  import_review_result("native menu opens with both imports active",!MmxWidePolicy_IsStageScene(g_ram));
  char path[96];snprintf(path,sizeof(path),"imports-pause-%u.cap",ordinal);MmxRendererSaveCapture(path);
  uint8_t bodies[128],shots[512];
  for(unsigned seat=0;seat<2;++seat) memcpy(bodies+seat*64,g_ram+s.bosses[seat].object,64);
  memcpy(shots,g_ram+0x1428,sizeof(shots));
  zero_replay(90);
  bool frozen=!memcmp(shots,g_ram+0x1428,sizeof(shots));
  for(unsigned seat=0;seat<2;++seat) frozen&=!memcmp(bodies+seat*64,g_ram+s.bosses[seat].object,64);
  import_review_result("bosses and projectiles stay frozen while paused",frozen);
  static uint32_t paused[256*224],native[256*224];
  check(MmxRendererDraw(paused,(MmxRenderView){256,0,4.0/3.0},true),"import pause renders");
  size_t pn=RtlSaveSnapshotToMemory(expected,cap);
  /* Compare the paused native PPU composition directly. Never disable Rush
   * and execute foreign objects through X1's unrelated class dispatch. */
  const uint32_t *stock=MmxRendererStockFrame();
  memcpy(native,stock,sizeof(native));
  import_review_result("paused menu pixels match the native PPU menu",!memcmp(native,paused,sizeof(native)));
  check(RtlLoadSnapshotFromMemory(expected,pn),"paused imports restore");
  frame(SNES_PAD_START);zero_replay(90);
  MmxBossRushState resumed=MmxBossRushGetState();
  import_review_result("unpause restores both imported seats and arena",MmxWidePolicy_IsStageScene(g_ram) &&
      resumed.bosses[0].id==s.bosses[0].id && resumed.bosses[1].id==s.bosses[1].id &&
      resumed.bosses[0].phase==MMX_RUSH_FIGHTING && resumed.bosses[1].phase==MMX_RUSH_FIGHTING);
  n=RtlSaveSnapshotToMemory(start,cap);zero_replay(60);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"post-pause imports restore for replay");
  zero_replay(60);size_t an=RtlSaveSnapshotToMemory(actual,cap);
  import_review_result("both imports replay byte exactly after unpause",en==an && !memcmp(expected,actual,en));
}
static void import_review_endings(uint8_t *start,size_t cap) {
  import_review_pair(start,cap,13,23);
  MmxBossRushState before=MmxBossRushGetState();unsigned rhino=before.bosses[1].object;
  g_ram[rhino+0x27]=(uint8_t)((g_ram[rhino+0x27]&128)|1);
  size_t n=RtlSaveSnapshotToMemory(start,cap);
  RhinoHit hit=rush_rhino_shoot(start,n,rhino,0,0,-1,SNES_PAD_Y);
  import_review_result("a real buster hit kills one-HP Rhino",hit.damage==1);
  for(unsigned t=0;t<900;++t) {frame(0);rush_keep_players_alive();}
  MmxBossRushState after=MmxBossRushGetState();
  import_review_result("lethal-hit Rhino cleanup counts once and replaces his seat",
      after.defeated==before.defeated+1 && after.bosses[1].id!=23 && after.bosses[1].phase);
  import_review_pair(start,cap,13,23);n=RtlSaveSnapshotToMemory(start,cap);
  for(unsigned target=0;target<2;++target) {
    check(RtlLoadSnapshotFromMemory(start,n),"restore living imports before each fatal-contact case");
    before=MmxBossRushGetState();
    unsigned d=before.bosses[target].object,other=before.bosses[1-target].object;
    g_ram[0xbcf]=0x81;g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc16]=0;
    g_ram[0xbd8]=g_ram[0xc32]=g_ram[0xc13]=0;
    bool died=false;
    for(unsigned t=0;t<900 && MmxBossRushGetState().mode!=MMX_RUSH_FINISHED;++t) {
      if(!died) {
        g_ram[other+0x26]=0;
        eagle_putword(g_ram+other+5,before.camera_x-128);
        eagle_putword(g_ram+0xbad,eagle_word(g_ram+d+5));
        eagle_putword(g_ram+0xbb0,eagle_word(g_ram+d+8)-8);
      }
      frame(0);died|=!(g_ram[0xbcf]&127) || g_ram[0xbaa]==0x0c;
    }
    char name[128];snprintf(name,sizeof(name),"natural fatal contact with %s reaches Game Over",
        target?"Tunnel Rhino":"Wire Sponge");
    import_review_result(name,died && MmxBossRushGetState().mode==MMX_RUSH_FINISHED);
  }
}
static void rush_import_review_checks(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  check(MmxBossRushForeignReady(2) && MmxBossRushForeignReady(3),"review has both original source ROMs");
  FILE *csv=fopen("imports-ai.csv","w");check(csv!=NULL,"AI evidence opens");
  fputs("frame,seat,id,state,behavior,step,x,y,hp,profile,palette,player_hp,player_hurt,player_timer\n",csv);
  for(unsigned swap=0;swap<2;++swap) {
    import_review_pair(start,cap,swap?23:13,swap?13:23);
    size_t n=RtlSaveSnapshotToMemory(start,cap);
    import_review_damage(start,n);
    if(!strcmp(rush_rhino_character(),"x")) {
      unsigned page=swap?2:1,weapon=swap?1:5;
      import_review_live_hit(start,n,page,weapon,false);
      import_review_live_hit(start,n,page,weapon,true);
    }
    unsigned contact_hits=0;
    for(unsigned t=0;t<2400;++t) {
      /* Set the low-health prerequisite, then let each original controller
       * choose and execute its own desperation attacks. */
      if(t==1200) {
        MmxBossRushState low=MmxBossRushGetState();
        for(unsigned seat=0;seat<2;++seat) {
          unsigned d=low.bosses[seat].object;g_ram[d+0x27]=(uint8_t)((g_ram[d+0x27]&128)|8);
        }
      }
      frame(0);MmxBossRushState s=MmxBossRushGetState();
      unsigned player_hp=g_ram[0xbcf]&127;
      if(player_hp<32) ++contact_hits;
      for(unsigned seat=0;seat<2;++seat) {
        unsigned d=s.bosses[seat].object;
        fprintf(csv,"%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u\n",t+swap*2400,seat,s.bosses[seat].id,
            g_ram[d+1],g_ram[d+2],g_ram[d+3],(int)eagle_word(g_ram+d+5)-(int)s.camera_x,
            (int)eagle_word(g_ram+d+8)-(int)s.camera_y,g_ram[d+0x27]&127,g_ram[d+0x28],g_ram[d+0x11],
            player_hp,g_ram[0xbd8],g_ram[0xc13]);
      }
      rush_keep_players_alive();
      if(t%120==0) {
        char path[80];snprintf(path,sizeof(path),"imports-%u-f%04u.cap",swap,t);MmxRendererSaveCapture(path);
        static uint32_t image[256*224];
        if(MmxRendererDraw(image,(MmxRenderView){256,0,4.0/3.0},true)) {
          snprintf(path,sizeof(path),"imports-%u-f%04u.ppm",swap,t);rush_rhino_save_ppm(path,image);
        }
        static uint32_t wide[456*224];
        if(MmxRendererDraw(wide,(MmxRenderView){456,100,21.0/9.0},true)) {
          snprintf(path,sizeof(path),"imports-wide-%u-f%04u.ppm",swap,t);import_review_save_wide(path,wide,456);
        }
      }
      if(t==300 || t==1000 || t==1800) import_review_pause(start,expected,actual,cap,swap*3+t);
    }
    fprintf(stderr,"IMPORT REVIEW combined encounter swap %u: %u actual damaging frames\n",swap,contact_hits);
    import_review_result("combined imports can repeatedly damage the player",contact_hits>=2);
  }
  fclose(csv);
  import_review_endings(start,cap);
  fprintf(stderr,"IMPORT REVIEW COMPLETE: %u failed assertions; see CSV and captures\n",import_review_failures);
  check(!import_review_failures,"independent source fidelity and combined encounter review");
}
