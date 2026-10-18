static void rush_rhino_save_ppm(const char *path,const uint32_t *pixels) {
  FILE *f=fopen(path,"wb");check(f!=NULL,"Rhino capture opens");
  fprintf(f,"P6\n256 224\n255\n");
  for(unsigned i=0;i<256*224;++i) {
    uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
    fwrite(rgb,1,3,f);
  }
  check(!fclose(f),"Rhino capture writes");
}
/* Count pixels around an object that use the boss's own source palette. */
static unsigned rush_rhino_art_pixels(const uint32_t *pixels,unsigned object,unsigned id) {
  const uint16_t *colors=MmxBossRushForeignColors(id);
  if(!colors) return 0;
  int cx=(int)eagle_word(g_ram+object+5)-(int)eagle_word(g_ram+0x1e4d);
  int cy=(int)eagle_word(g_ram+object+8)-(int)eagle_word(g_ram+0x1e50);
  unsigned hits=0;
  for(int y=cy-48;y<cy+24;++y) for(int x=cx-48;x<cx+48;++x) {
    if(x<0 || x>=256 || y<0 || y>=224) continue;
    uint32_t p=pixels[y*256+x];
    for(unsigned c=1;c<16;++c) {
      unsigned r=(colors[c]&31)*255/31,g=((colors[c]>>5)&31)*255/31,b=((colors[c]>>10)&31)*255/31;
      int dr=(int)((p>>16)&255)-(int)r,dg=(int)((p>>8)&255)-(int)g,db=(int)(p&255)-(int)b;
      if(dr*dr+dg*dg+db*db<=48) {++hits;break;}
    }
  }
  return hits;
}
/* Fire one native shot at an idle Rhino and report his first damage, whether
 * his relocated weakness test made him flinch, and his profile row then.
 * page 0 fires X1 weapon `weapon` (0 = buster); pages 1/2 fire that loaded
 * source pack's weapon through its own selection, as a player would.
 * profile: force a row before firing (0 = drill-charge deflect), or -1.
 * button: the character's control for this attack (Saber Zero fires the
 * buster/specials with X and swings with Y; the others fire with Y). */
typedef struct RhinoHit {unsigned damage;bool flinched;unsigned profile;} RhinoHit;
static RhinoHit rush_rhino_shoot(uint8_t *snapshot,size_t size,unsigned object,unsigned page,
                                 unsigned weapon,int profile,uint16_t button) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Rhino weakness case restores combat");
  MmxBossRushState s=MmxBossRushGetState();
  unsigned penguin=s.bosses[0].object;RhinoHit result={0,false,0};
  unsigned start_hp=g_ram[object+0x27]&127;bool hit=false;
  MmxWeaponsState w=MmxWeaponsGetState();
  w.initialized=1;w.page=(uint8_t)page;w.weapon=(uint8_t)(page?weapon:0);
  if(page) w.energy[(page-1)*8+weapon-1]=28;
  MmxWeaponsSetState(w);
  for(unsigned t=0;t<300 && !result.flinched;++t) {
    unsigned cx=s.camera_x+160,cy=s.camera_y+164;
    if(!hit) {
      /* Idle (behavior $0C step 1) with a long wait: no charge or volley. */
      g_ram[object+2]=0x0c;g_ram[object+3]=1;g_ram[object+0x39]=100;
      g_ram[object+5]=(uint8_t)cx;g_ram[object+6]=(uint8_t)(cx>>8);
      g_ram[object+8]=(uint8_t)cy;g_ram[object+9]=(uint8_t)(cy>>8);
      if(profile>=0) g_ram[object+0x28]=(uint8_t)profile;
    }
    g_ram[penguin+5]=(uint8_t)(s.camera_x-128);g_ram[penguin+6]=(uint8_t)((s.camera_x-128)>>8);
    /* Close enough for melee swings; ranged shots are re-centered below. */
    g_ram[0xbad]=(uint8_t)(s.camera_x+136);g_ram[0xbae]=(uint8_t)((s.camera_x+136)>>8);
    g_ram[0xbb0]=(uint8_t)cy;g_ram[0xbb1]=(uint8_t)(cy>>8);
    g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc2d]=0;g_ram[0xbb9]&=~64u;g_ram[0xbdb]=(uint8_t)(page?0:weapon*2);
    for(unsigned d=0xe68;d<0x1628;d+=64) if(d<0x1228 || d>=0x1428) {
      if(MmxBossRushOwner(d)>=0 && g_ram[d]) g_ram[d+0x26]=0; /* no contact knockback */
    }
    for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
      memcpy(g_ram+d+5,g_ram+object+5,2);memcpy(g_ram+d+8,g_ram+object+8,2);
      g_ram[d+4]=g_ram[d+7]=0;
    }
    /* Pack shots follow their combat record; keep those on the target too. */
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
      c.shots[i].x=(int32_t)((g_ram[object+5]|g_ram[object+6]<<8)<<8);
      c.shots[i].y=(int32_t)((g_ram[object+8]|g_ram[object+9]<<8)<<8);
    }
    MmxWeaponsSetCombatState(c);
    frame(t%90==0 && !hit?button:0);rush_keep_players_alive();
    unsigned hp=g_ram[object+0x27]&127;
    if(!hit && hp<start_hp) {hit=true;result.damage=start_hp-hp;}
    if(hit) {
      result.profile=g_ram[object+0x28];
      if(g_ram[object+1]==2 && (g_ram[object+2]==8 || g_ram[object+2]==10)) result.flinched=true;
      if(t>200) break;
    }
    if(!hit && profile==0 && t>=120) break; /* deflected: never damaged */
  }
  return result;
}
/* Which character the fixture plays: x (default), zero, modern or saber. */
static const char *rush_rhino_character(void) {
  const char *c=getenv("MMX_BOSS_RUSH_RHINO_CHARACTER");return c && *c?c:"x";
}
static void rush_rhino_weakness_checks(uint8_t *snapshot,size_t size,unsigned object,const char *x3) {
  const char *who=rush_rhino_character();
  bool saber=!strcmp(who,"saber"),x=!strcmp(who,"x");
  uint16_t fire=saber?SNES_PAD_X:SNES_PAD_Y;
  /* Modern Zero's Fire with the buster selected is its saber swing. */
  RhinoHit buster=rush_rhino_shoot(snapshot,size,object,0,0,-1,fire);
  RhinoHit deflect=rush_rhino_shoot(snapshot,size,object,0,0,0,fire);
  RhinoHit torpedo={1,false,0},slash={1,false,0},slash_deflect={0,false,0};
  if(x) torpedo=rush_rhino_shoot(snapshot,size,object,0,1,-1,fire);
  if(saber) {
    slash=rush_rhino_shoot(snapshot,size,object,0,0,-1,SNES_PAD_Y);
    slash_deflect=rush_rhino_shoot(snapshot,size,object,0,0,0,SNES_PAD_Y);
  }
  /* Acid Burst needs the X3 weapon pack, built from the same X3 ROM (the
   * launcher-driven runs already enabled it). */
  bool own_pack=!MmxWeaponsPageEnabled(2);
  if(own_pack) {
    char cache[512],error[512];
    check(snesrecomp_exe_dir_path("rhino-x3-weapons.bin",cache,sizeof(cache)) &&
        MmxSourceAssetsBuild(x3,3,0,cache,error,sizeof(error)) && MmxWeaponsLoadPage(cache,2),
        "the X3 weapon pack loads for Rhino's weakness");
    MmxZeroRegisterHooks();
  }
  RhinoHit acid=rush_rhino_shoot(snapshot,size,object,2,1,-1,fire);
  MmxWeaponsCancelShots(g_ram);
  if(own_pack) MmxWeaponsDisable();
  fprintf(stderr,"rhino hits (%s): buster=%u/%u torpedo=%u/%u slash=%u/%u acid=%u/%u row=%u deflect=%u/%u\n",
      who,buster.damage,buster.flinched,torpedo.damage,torpedo.flinched,slash.damage,slash.flinched,
      acid.damage,acid.flinched,acid.profile,deflect.damage,slash_deflect.damage);
  check(buster.damage>=1 && !buster.flinched,"the character's basic attack deals ordinary damage");
  check(!x || buster.damage==1,"X's buster deals Rhino's ordinary damage");
  check(torpedo.damage==1 && !torpedo.flinched,"Homing Torpedo is not one of Rhino's weaknesses");
  check(slash.damage>=1 && !slash.flinched,"Saber Zero's slash deals ordinary damage");
  check(acid.damage==3 && acid.flinched,"Acid Burst is Rhino's weakness and makes him flinch");
  check(acid.profile==5,"Rhino's weakness flinch is invulnerable");
  check(!deflect.damage && !slash_deflect.damage,"his drill-charge row deflects every attack");
}
/* His stop-and-fire charge, started from the left wall with free projectile
 * slots: he stops past the arena's center, turns to the player and launches
 * the hand drill (40 ahead, 8 up) and both shoulder drills (24 up, at +14
 * and -18; X3 does not mirror them), each after a 4-pixel spawn nudge. */
static void rush_rhino_spread_checks(uint8_t *snapshot,size_t size,unsigned object) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Rhino spread case restores combat");
  MmxBossRushState s=MmxBossRushGetState();unsigned penguin=s.bosses[0].object;
  for(unsigned d=0x1428;d<0x1628;d+=64) memset(g_ram+d,0,64);
  unsigned x=s.camera_x+60;
  g_ram[object+5]=(uint8_t)x;g_ram[object+6]=(uint8_t)(x>>8);
  g_ram[object+2]=0;g_ram[object+3]=4;g_ram[object+0x3b]=1;g_ram[object+0x35]=0;g_ram[object+0x3d]=0;
  g_ram[object+0x11]|=0x40;g_ram[object+0x1a]=0;g_ram[object+0x1b]=4;g_ram[object+0x28]=0x0a;
  int found[4][2]={{0}};unsigned seen=0,age[8]={0};
  for(unsigned t=0;t<300 && seen!=0x0e;++t) {
    g_ram[penguin+5]=(uint8_t)(s.camera_x-128);g_ram[penguin+6]=(uint8_t)((s.camera_x-128)>>8);
    frame(0);rush_keep_players_alive();
    for(unsigned i=0;i<8;++i) {
      unsigned d=0x1428+i*64;
      bool spread=g_ram[d] && g_ram[d+10]==0x26 && g_ram[d+0x3c]==4 && MmxBossRushOwner(d)==1;
      if(spread && age[i]==1 && g_ram[d+3] && g_ram[d+0x0b]<4) {
        unsigned k=g_ram[d+0x0b];seen|=1u<<k;
        found[k][0]=(int)eagle_word(g_ram+d+5)-(int)eagle_word(g_ram+object+5);
        found[k][1]=(int)eagle_word(g_ram+d+8)-(int)eagle_word(g_ram+object+8);
      }
      age[i]=!spread?0:age[i]==1 && g_ram[d+3]?2:age[i]?age[i]:1;
    }
  }
  fprintf(stderr,"rhino spread: drills=%x hand=%d,%d shoulders=%d,%d / %d,%d\n",seen,
      found[1][0],found[1][1],found[2][0],found[2][1],found[3][0],found[3][1]);
  check(seen==0x0e,"his stop-and-fire spread launches three drills");
  /* He turns to the player before firing; the nudge follows his facing. */
  int dir=found[1][0]<0?-1:1;
  check(found[1][0]==44*dir && found[1][1]==-8,"the hand drill starts at his hand");
  check(found[2][0]==14+4*dir && found[2][1]==-24 && found[3][0]==-18+4*dir && found[3][1]==-24,
      "both shoulder drills start on his shoulders");
}
/* A long fight: every hit flashes him with the invulnerable profile and the
 * flash restores his own profile (X3 $1F57), so hits keep landing; dead drills
 * give their projectile slots back, so he keeps firing volleys; and every
 * charge, including stop-and-fire, keeps him inside the arena. */
static void rush_rhino_long_fight_checks(uint8_t *snapshot,size_t size,unsigned object) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Rhino long fight restores combat");
  MmxBossRushState s=MmxBossRushGetState();
  int width=256+(g_ram[0x1f7a]==4?MMX_RUSH_MAMMOTH_SCROLL:0),min_x=1<<30,max_x=-(1<<30);
  unsigned hits=0,hp=g_ram[object+0x27]&127,spreads=0,volleys=0,last=0,stale=0,deflecting=0,charging=0,phantom=0,shoulders=0,hands=0;
  unsigned spread_age[8]={0},locked_frames=0,hurt_frames=0;
  bool drills_alive=false;
  for(unsigned t=0;t<6000;++t) {
    s=MmxBossRushGetState();
    if(s.bosses[1].object!=object || s.bosses[1].id!=23) break;
    for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
      memcpy(g_ram+d+5,g_ram+object+5,2);memcpy(g_ram+d+8,g_ram+object+8,2);
    }
    frame(t%24==0?SNES_PAD_Y:0);rush_keep_players_alive();
    unsigned h=g_ram[object+0x27]&127,behavior=g_ram[object+1]<<8|g_ram[object+2];
    if(h<hp) ++hits;
    if(h<4) {g_ram[object+0x27]=(uint8_t)((g_ram[object+0x27]&128)|30);h=30;}
    hp=h;
    if(behavior==0x0206 && last!=0x0206) ++spreads;
    /* Spread drills start at X3's offsets ($86:EFE7): the hand drill 40
     * pixels ahead and 8 up, the shoulder drills 24 up at +14 / -18. */
    for(unsigned i=0;i<8;++i) {
      unsigned d=0x1428+i*64;
      bool spread=g_ram[d] && g_ram[d+10]==0x26 && g_ram[d+0x3c]==4 && MmxBossRushOwner(d)==1;
      /* Sample once its spread step has applied the offset ($03 leaves 0). */
      if(spread && spread_age[i]==1 && g_ram[d+3]) {
        int dx=(int)eagle_word(g_ram+d+5)-(int)eagle_word(g_ram+object+5);
        int dy=(int)eagle_word(g_ram+d+8)-(int)eagle_word(g_ram+object+8);
        fprintf(stderr,"rhino spread drill t=%u index=%u dx=%d dy=%d facing=%s\n",t,g_ram[d+0x0b],dx,dy,
            g_ram[object+0x11]&0x40?"right":"left");
        if(dy<=-20 && dy>=-28) ++shoulders;
        if(dy<=-4 && dy>=-12) ++hands;
      }
      spread_age[i]=!spread?0:spread_age[i]==1 && g_ram[d+3]?2:spread_age[i]?spread_age[i]:1;
    }
    /* The deflect row belongs to his drill charge (2) and its wind-down (10). */
    if(!g_ram[object+0x28] && behavior!=0x0202 && behavior!=0x020a) ++deflecting;
    if(behavior==0x0202) ++charging;
    if(g_ram[0xbaa]==0x1e) ++locked_frames;
    if(g_ram[0xbaa]==0x0e) ++hurt_frames;
    last=behavior;
    bool alive=false;unsigned live=0;
    for(unsigned d=0x1428;d<0x1628;d+=64) {
      if(g_ram[d] && g_ram[d+10]==0x26 && MmxBossRushOwner(d)==1) {alive=true;if(g_ram[d+0x3c]!=2) ++live;}
      if(!g_ram[d] && g_ram[d+1]) ++stale;
    }
    /* $3D counts drills he believes are alive; outside the volley and
     * spread spawns it must never exceed the drills that exist. */
    if(behavior!=0x0204 && behavior!=0x0206 && g_ram[object+0x3d]>live) ++phantom;
    if(alive && !drills_alive) ++volleys;
    drills_alive=alive;
    int x=(int)eagle_word(g_ram+object+5)-(int)s.camera_x;
    if(x<min_x) min_x=x;
    if(x>max_x) max_x=x;
  }
  fprintf(stderr,"rhino long fight: hits=%u volleys=%u spreads=%u stale=%u deflecting=%u phantom=%u drill-charge=%u locked=%u hurt=%u x=%d..%d width=%d\n",
      hits,volleys,spreads,stale,deflecting,phantom,charging,locked_frames,hurt_frames,min_x,max_x,width);
  check(hits && !deflecting,"Rhino keeps taking damage after his hit flashes end");
  check(!stale,"deleted drills leave their projectile slots free");
  check(!phantom && volleys>=2,"Rhino keeps firing drills for the whole fight");
  /* A spread fires only into free projectile slots; count the ones that did. */
  check(shoulders==2*hands,"every stop-and-fire spread launches his hand drill and both shoulder drills from their X3 positions");
  check(min_x>=24 && max_x<=width-24,"Rhino stays inside the arena on every charge");
  uint8_t tail=0;
  check(!MmxCartExtensionWrite(0x86ff40,&tail,1),"relocation never writes the saber/weapon tables at $86:FF40");
  /* Zero's air and dash slashes use the $86:FF40 window; an overwritten
   * window disables them for the rest of the session. */
  check(!MmxSaberAttackCollisionWarningCount(),"Zero's ground and air saber windows stay installed");
}
/* Character matrix: enable the real packages through the launcher's mod
 * provider on an isolated catalog copy, exactly as a player would: Boss
 * Rush, X3 Mavericks and X3 Weapons with the X3 ROM, and for the Zero
 * characters Add Zero starting as Zero with the chosen behavior.
 * MMX_BOSS_RUSH_LAUNCHER_ROOT, MMX_BOSS_RUSH_X1_ROM, MMX_BOSS_RUSH_X3_ROM. */
static void rush_rhino_launcher(void) {
  if(!getenv("MMX_BOSS_RUSH_RHINO_CHARACTER")) return;
  const char *who=rush_rhino_character();
  const char *root=getenv("MMX_BOSS_RUSH_LAUNCHER_ROOT"),*x1=getenv("MMX_BOSS_RUSH_X1_ROM"),
      *x3=getenv("MMX_BOSS_RUSH_X3_ROM");
  check(root && x1 && x3,"character matrix receives a catalog root and both ROMs");
  check(snes_mod_runtime_initialize_c(root,"megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),"Rhino catalog parses");
  const RecompLauncherCModProvider *p=snes_mod_runtime_launcher_provider_c();
  check(p && p->feature_enable && p->feature_set_option && p->feature_resource_set_path && p->commit,
      "Rhino catalog exposes the launcher provider");
  const char *bosses="megaman-x.boss-rush.x3-bosses";
  bool rush=p->feature_enable(p->ctx,"megaman-x.mode.boss-rush","boss_rush",1);
  bool mavericks=p->feature_enable(p->ctx,bosses,"bosses",1);
  bool rom=p->feature_resource_set_path(p->ctx,bosses,"bosses","x3-rom",x3);
  bool weapons=p->feature_enable(p->ctx,"megaman-x.weapons.x3","weapons",1);
  if(!(rush && mavericks && rom && weapons))
    fprintf(stderr,"rhino launcher: rush=%u mavericks=%u rom=%u weapons=%u error=%s\n",rush,mavericks,
        rom,weapons,snes_mod_runtime_last_error_c());
  check(rush && mavericks && rom && weapons,"launcher enables Boss Rush, X3 Mavericks and X3 weapons");
  bool zero=strcmp(who,"x")!=0;
  if(zero) {
    const char *behavior=!strcmp(who,"modern")?"modern":!strcmp(who,"saber")?"saber":"x3";
    const char *package="megaman-x.character.zero";
    check(p->feature_enable(p->ctx,package,"zero",1) &&
        p->feature_set_option(p->ctx,package,"zero","start","zero") &&
        p->feature_set_option(p->ctx,package,"zero","behavior",behavior),
        "launcher selects Add Zero, starting as Zero with the chosen behavior");
    check(p->feature_enable(p->ctx,MMX_SABER_SETTINGS_PACKAGE,MMX_SABER_SETTINGS_FEATURE,
        !strcmp(who,"saber")),"Saber Zero settings follow the behavior");
  }
  bool committed=p->commit(p->ctx,x1);
  if(!committed) {
    fprintf(stderr,"rhino launcher commit: %s\n",p->last_error?p->last_error(p->ctx):snes_mod_runtime_last_error_c());
    for(int i=0;p->feature_count && i<p->feature_count(p->ctx);++i) {
      RecompLauncherCModFeature f;memset(&f,0,sizeof(f));
      if(!p->feature_get(p->ctx,i,&f)) continue;
      for(int k=0;p->diagnostic_count && k<p->diagnostic_count(p->ctx,f.package_id,f.id);++k) {
        RecompLauncherCModDiagnostic d;memset(&d,0,sizeof(d));
        if(p->diagnostic_get(p->ctx,f.package_id,f.id,k,&d))
          fprintf(stderr,"  %s/%s: [%d] %s (related %s/%s)\n",f.package_id,f.id,d.severity,
              d.message,d.related_package_id,d.related_feature_id);
      }
    }
  }
  check(committed,"the character matrix plan commits");
  snes_mod_runtime_activate_plugins_c();
  check(MmxBossRushForeignReady(3) && MmxWeaponsPageEnabled(2),
      "the shared X3 ROM reaches X3 Mavericks and X3 weapons");
  if(zero) check(MmxZeroEnabled() && MmxZeroActive(),"the run starts as Zero");
  if(!strcmp(who,"modern")) check(MmxZeroModern(),"Modern Zero is active");
  if(!strcmp(who,"saber")) check(MmxSaberEnabled(),"Saber Zero is active");
  fprintf(stderr,"rhino character matrix: %s\n",who);
}
/* Start Rhino's drill charge (behavior 2) at X and follow the pin and his
 * private palette: the carrier catches X, X is locked in $1E during his
 * recoil and released after landing; colors rotate, then fade back. */
static void rush_rhino_pin_palette_checks(uint8_t *snapshot,size_t size,unsigned object) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Rhino drill-charge case restores combat");
  MmxBossRushState s=MmxBossRushGetState();unsigned seat=1,penguin=s.bosses[0].object;
  const uint16_t *base=MmxBossRushForeignColors(23);check(base!=NULL,"Rhino's base palette is available");
  unsigned rx=s.camera_x+200,px=s.camera_x+120,floor_y=g_ram[object+8]|g_ram[object+9]<<8;
  g_ram[object+5]=(uint8_t)rx;g_ram[object+6]=(uint8_t)(rx>>8);
  g_ram[object+2]=2;g_ram[object+3]=0;g_ram[object+0x36]=0;
  g_ram[0xbad]=(uint8_t)px;g_ram[0xbae]=(uint8_t)(px>>8);
  g_ram[0xbb0]=(uint8_t)floor_y;g_ram[0xbb1]=(uint8_t)(floor_y>>8);
  g_ram[0xbaa]=g_ram[0xbab]=0;
  /* Ordinary health (no keep-alive refill) so the drill hit really hurts X. */
  /* Start unhurt: the fixture snapshot may be mid-hurt, and forcing the
   * state skips the end of X1's hurt (its latch countdown). */
  g_ram[0xbcf]=0x1c;g_ram[0xc30]=g_ram[0xc31]=g_ram[0xc32]=g_ram[0xc33]=0;
  g_ram[0xbd8]=g_ram[0xc13]=0;
  bool carrier=false,locked=false,rotated=false,dragged=false;unsigned lock_frame=0,release_frame=0,carriers_max=0;
  unsigned start_x=px;
  for(unsigned t=0;t<420;++t) {
    g_ram[penguin+5]=(uint8_t)(s.camera_x-128);g_ram[penguin+6]=(uint8_t)((s.camera_x-128)>>8);
    frame(0);
    if((g_ram[0xbcf]&127)<8) g_ram[0xbcf]=(uint8_t)((g_ram[0xbcf]&0x80)|0x1c);
    unsigned carriers=0;
    for(unsigned d=0x1428;d<0x1628;d+=64)
      if(g_ram[d] && g_ram[d+10]==0x26 && g_ram[d+0x3c]==2 && MmxBossRushOwner(d)==(int)seat) {carrier=true;++carriers;}
    if(carriers>carriers_max) carriers_max=carriers;
    unsigned x=g_ram[0xbad]|g_ram[0xbae]<<8;
    if(carrier && (x>start_x+12 || x+12<start_x)) dragged=true;
    if(g_ram[0xbaa]==0x1e && !locked) {locked=true;lock_frame=t;}
    if(locked && !release_frame && g_ram[0xbaa]!=0x1e) release_frame=t;
    MmxBossRushVisualState v=MmxBossRushVisualGetState();
    if(v.initialized[seat] && memcmp(v.colors[seat],base,sizeof(v.colors[seat]))) rotated=true;
    if(t<160 || t%30==0) fprintf(stderr,"pin t=%u rhino=%02x/%02x/%02x x=%04x player=%02x/%02x px=%04x carrier=%u hp=%02x guard=%02x/%02x/%02x contact=%u\n",
        t,g_ram[object+1],g_ram[object+2],g_ram[object+3],g_ram[object+5]|g_ram[object+6]<<8,
        g_ram[0xbaa],g_ram[0xbab],x,carrier,g_ram[0xbcf],g_ram[0x1f0c],g_ram[0xc30],g_ram[0xc32],g_ram[object+0x26]);
    if(release_frame && t>release_frame+90 && g_ram[object+2]!=2 && g_ram[object+2]!=10) break;
  }
  MmxBossRushVisualState v=MmxBossRushVisualGetState();
  bool restored=!v.initialized[seat] || !memcmp(v.colors[seat],base,sizeof(v.colors[seat]));
  fprintf(stderr,"rhino pin: carrier=%u dragged=%u locked=%u@%u released@%u rotated=%u restored=%u\n",
      carrier,dragged,locked,lock_frame,release_frame,rotated,restored);
  check(carrier && dragged,"Rhino's drill charge catches X and carries him");
  check(carriers_max==1,"one drill-charge catch spawns one pin carrier");
  check(rotated,"his drill charge cycles his private palette");
  check(restored,"his palette fades back to his original colors");
  /* However the catch ended, X1's post-hit countdown clears the hurt
   * latches; a player locked while hurt must not stay invulnerable. */
  unsigned cleared=0;
  for(unsigned t=0;t<240 && !cleared;++t) {
    g_ram[penguin+5]=(uint8_t)(s.camera_x-128);g_ram[penguin+6]=(uint8_t)((s.camera_x-128)>>8);
    frame(0);rush_keep_players_alive();
    if(g_ram[0xbaa]!=0x0e && g_ram[0xbaa]!=0x1e && !g_ram[0xbd8] && !g_ram[0xc32]) cleared=t+1;
  }
  fprintf(stderr,"rhino pin latches cleared after %u frames (bd8=%02x c32=%02x)\n",cleared,g_ram[0xbd8],g_ram[0xc32]);
  check(cleared,"the caught player's hurt latches clear after the pin");
  /* Without his pin carrier there is no catch: an ordinary hit during his
   * drill recoil (behavior 2, step 6) must not lock the player. */
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Rhino pin case restores combat");
  g_ram[object+2]=2;g_ram[object+3]=6;g_ram[object+0x39]=60;g_ram[object+0x36]=0;
  g_ram[0xbaa]=0x0e;g_ram[0xbab]=0;
  locked=false;
  for(unsigned t=0;t<120;++t) {
    g_ram[penguin+5]=(uint8_t)(s.camera_x-128);g_ram[penguin+6]=(uint8_t)((s.camera_x-128)>>8);
    frame(0);rush_keep_players_alive();
    if(g_ram[0xbaa]==0x1e) locked=true;
  }
  check(!locked && !g_ram[object+0x36],"an ordinary hit during his recoil is not a pin");
}
/* Tunnel Rhino's original X3 code, relocated from the user's ROM, fights in
 * seat 1 beside a native Penguin. MMX_BOSS_RUSH_X3_ROM names the X3 ROM. */
static void rush_rhino_checks(uint8_t *start,size_t cap,const char *x3) {
  char error[256];
  /* The character matrix loads the pack through the launcher already. */
  check(x3 && (MmxBossRushForeignReady(3) || MmxBossRushForeignLoad(3,x3,error,sizeof(error))),
      "X3 ROM loads for the Rhino fixture");
  check(MmxBossRushForeignBuild(error,sizeof(error)),"Rhino relocates into the cart padding");
  if(error[0]) fprintf(stderr,"rhino build: %s\n",error);
  check(MmxBossRushForeignController(23)==0xbfe5e4 && MmxBossRushForeignChild(23,0x26)==0xb8ab9b,
      "Rhino body and drill controllers register");
  static const uint8_t effects[]={0x15,0x21,0x49,0x70,0x72,0x73};
  for(unsigned i=0;i<sizeof(effects);++i) {
    uint32_t frames=0;const int16_t *clip=MmxBossRushAudioForeignClip(23,effects[i],&frames);
    int peak=0;
    for(uint32_t k=0;clip && k<frames*2;++k) if(abs(clip[k])>peak) peak=abs(clip[k]);
    fprintf(stderr,"rhino effect $%02x frames=%u (%.2fs) peak=%d\n",effects[i],frames,frames/32040.0,peak);
    check(clip && frames>=320 && peak>=256,"Rhino's X3 effects render from his own driver and bank");
    if(clip && getenv("MMX_BOSS_RUSH_RHINO_WAV")) {
      /* Private listening copies of the rendered clips (32040 Hz stereo). */
      char path[32];snprintf(path,sizeof(path),"rhino-%02x.wav",effects[i]);
      FILE *w=fopen(path,"wb");check(w!=NULL,"Rhino effect WAV opens");
      uint32_t bytes=frames*4,rate=32040,byte_rate=rate*4,chunk=36+bytes,fmt_size=16;
      uint16_t format=1,channels=2,align=4,bits=16;
      fwrite("RIFF",1,4,w);fwrite(&chunk,4,1,w);fwrite("WAVEfmt ",1,8,w);fwrite(&fmt_size,4,1,w);
      fwrite(&format,2,1,w);fwrite(&channels,2,1,w);fwrite(&rate,4,1,w);fwrite(&byte_rate,4,1,w);
      fwrite(&align,2,1,w);fwrite(&bits,2,1,w);fwrite("data",1,4,w);fwrite(&bytes,4,1,w);
      fwrite(clip,4,frames,w);check(!fclose(w),"Rhino effect WAV writes");
    }
  }
  check(!MmxBossRushAudioForeignClip(23,0x5a,NULL),"only Rhino's own effects are cached");
  check(MmxBossRushOffered()&(1u<<23),"a loaded X3 pack offers Tunnel Rhino to normal runs");
  MmxBossRushState s=MmxBossRushGetState();
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
  memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);s.queued=0;
  check(MmxBossRushSetState(&s),"Rhino fixture clears the opening pair");
  MmxBossRushRosterState roster={.available=MMX_RUSH_X1_MASK|(1u<<23),.queued=2};
  roster.queue[0]=0;roster.queue[1]=23;
  check(MmxBossRushRosterSetState(&roster),"Rhino fixture queues Penguin and Rhino");
  unsigned object=0;
  for(unsigned t=0;t<600 && !object;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[1].phase && s.bosses[1].id==23) object=s.bosses[1].object;
  }
  check(object!=0,"Rhino spawns in seat 1");
  unsigned seen_behavior=0,drill_frames=0,min_x=0xffff,max_x=0,max_hp=0,last=0xffffff,fighting_at=0;
  uint32_t cue_sequence=MmxBossRushAudioGetState().sequence,heard[8]={0};
  for(unsigned t=0;t<1800;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    MmxBossRushAudioState audio=MmxBossRushAudioGetState();
    for(;cue_sequence!=audio.sequence;++cue_sequence) {
      const MmxBossRushAudioCue *cue=&audio.cues[cue_sequence%MMX_RUSH_AUDIO_CUES];
      if(cue->reserved==23) heard[cue->command>>5]|=1u<<(cue->command&31);
    }
    if(!s.bosses[1].phase || s.bosses[1].id!=23) break;
    unsigned state=g_ram[object+1]<<16|g_ram[object+2]<<8|g_ram[object+3];
    unsigned x=eagle_word(g_ram+object+5);
    if(state!=last && (t<600 || (g_ram[object+2]>=2 && g_ram[object+2]<=6))) fprintf(stderr,"rhino t=%u state=%02x/%02x/%02x x=%04x y=%04x hp=%u anim=%02x/%02x live=%u\n",
        t,state>>16,(state>>8)&255,state&255,x,eagle_word(g_ram+object+8),g_ram[object+0x27]&127,
        g_ram[object+0x0f],g_ram[object+0x13],g_ram[object+0x3d]);
    last=state;
    if(getenv("MMX_BOSS_RUSH_RHINO_ANIM") && g_ram[object+1]==2 && g_ram[object+2]==4) {
      unsigned record=eagle_word(g_ram+object+0x14);
      const uint8_t *at=RomPtr(0x3f0000|record);
      fprintf(stderr,"anim t=%u set=%02x step=%02x f=%02x timer=%02x rec=%04x [%02x %02x %02x] pose=%02x\n",
          t,g_ram[object+0x16],g_ram[object+3],g_ram[object+0x0f],g_ram[object+0x13],record,at[0],at[1],at[2],g_ram[object+0x17]);
    }
    if(g_ram[object+1]==2) {
      seen_behavior|=1u<<(g_ram[object+2]/2);
      if(x<min_x) min_x=x;
      if(x>max_x) max_x=x;
      if(!fighting_at) fighting_at=t;
    }
    if((g_ram[object+0x27]&127)>max_hp) max_hp=g_ram[object+0x27]&127;
    unsigned live_drills=0,drill=0;
    for(unsigned d=0x1428;d<0x1628;d+=64)
      if(g_ram[d] && g_ram[d+10]==0x26 && MmxBossRushOwner(d)==1) {++drill_frames;++live_drills;drill=d;}
    static unsigned first_drill;
    if(live_drills && !first_drill) first_drill=t;
    static uint32_t image[256*224];
    if(t==fighting_at+40 && fighting_at) {
      check(MmxRendererDraw(image,(MmxRenderView){256,0,4.0/3.0},true),"Rhino combat frame renders");
      unsigned art=rush_rhino_art_pixels(image,object,23);
      fprintf(stderr,"rhino art pixels=%u at t=%u\n",art,t);
      check(art>=400,"Rhino draws with his own X3 art and palette");
      rush_rhino_save_ppm("rush-rhino-combat.ppm",image);
    }
    if(first_drill && t==first_drill+40 && drill) {
      check(MmxRendererDraw(image,(MmxRenderView){256,0,4.0/3.0},true),"Rhino drill frame renders");
      unsigned art=rush_rhino_art_pixels(image,drill,23);
      const MmxForeignPose *dp=MmxBossRushForeignPose(23,g_ram[drill+0x16],g_ram[drill+0x17]&0x7f);
      fprintf(stderr,"rhino drill art pixels=%u at t=%u drills=%u obj=%04x active=%02x set=%02x pose=%02x xy=%04x,%04x cam=%04x,%04x attr=%02x on=%02x layout=%s\n",
          art,t,live_drills,drill,g_ram[drill],g_ram[drill+0x16],g_ram[drill+0x17],eagle_word(g_ram+drill+5),
          eagle_word(g_ram+drill+8),eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e50),g_ram[drill+0x11],
          g_ram[drill+0x0e],dp?"ok":"none");
      check(g_ram[drill+0x16]==g_ram[object+0x16],"Rhino's drills run their relocated X3 controller");
      check(art>=40,"Rhino's drills draw with his own X3 art");
      rush_rhino_save_ppm("rush-rhino-drills.ppm",image);
    }
  }
  s=MmxBossRushGetState();
  fprintf(stderr,"rhino fighting_at=%u behaviors=%03x x=%04x..%04x hp=%u drill-frames=%u phase=%u penguin=%u\n",
      fighting_at,seen_behavior,min_x,max_x,max_hp,drill_frames,s.bosses[1].phase,s.bosses[0].phase);
  check(fighting_at && s.bosses[1].phase==MMX_RUSH_FIGHTING,"Rhino's original intro hands over to combat");
  check(max_hp==32,"Rhino's intro fills his native health meter");
  check(max_x-min_x>=64,"Rhino moves through the arena");
  unsigned behaviors=0;
  for(unsigned b=seen_behavior;b;b&=b-1) ++behaviors;
  check(behaviors>=2,"Rhino chooses between original behaviors");
  check(s.bosses[0].id==0 && s.bosses[0].phase,"native Penguin keeps fighting beside Rhino");
  const uint8_t *row=MmxBossRushForeignRom(3,NULL)+0x30000+(0xe28e&0x7fff)+(0x55-1)*5;
  check(g_ram[object+0x16]==row[0] && g_ram[object+0x16],"Rhino uses his own X3 sprite set");
  check(drill_frames>0,"Rhino's drill volley spawns relocated drill projectiles");
  #define RHINO_HEARD(c) ((heard[(c)>>5]>>((c)&31))&1)
  fprintf(stderr,"rhino cues: fill=%u crash=%u spread=%u stop=%u grind=%u\n",RHINO_HEARD(0x15),
      RHINO_HEARD(0x49),RHINO_HEARD(0x70),RHINO_HEARD(0x72),RHINO_HEARD(0x73));
  check(RHINO_HEARD(0x15) && RHINO_HEARD(0x49),"Rhino's intro fill and wall crash queue his own X3 sounds");
  check(RHINO_HEARD(0x73),"his drills queue their grinding sound");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"Rhino seat snapshot saves");
  static uint8_t first[0x20000];
  for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
  memcpy(first,g_ram,sizeof(first));
  check(RtlLoadSnapshotFromMemory(start,n),"Rhino seat snapshot restores");
  for(unsigned t=0;t<60;++t) {frame(0);rush_keep_players_alive();}
  check(!memcmp(first,g_ram,sizeof(first)),"Rhino replay is deterministic");
  rush_rhino_pin_palette_checks(start,n,object);
  rush_rhino_spread_checks(start,n,object);
  rush_rhino_long_fight_checks(start,n,object);
  rush_rhino_weakness_checks(start,n,object,x3);
  check(RtlLoadSnapshotFromMemory(start,n),"Rhino seat snapshot restores for defeat");
  unsigned defeated=MmxBossRushGetState().defeated;
  uint32_t generation=MmxBossRushGetState().bosses[1].generation;
  g_ram[object+0x27]=0;frame(0);s=MmxBossRushGetState();
  check(s.defeated==defeated+1 && s.bosses[1].phase==MMX_RUSH_DYING,"Rush counts Rhino's defeat");
  for(unsigned t=0;t<90;++t) {frame(0);rush_keep_players_alive();}
  s=MmxBossRushGetState();
  bool leftovers=false;
  for(unsigned d=0x1428;d<0x1628;d+=64)
    if(g_ram[d] && g_ram[d+10]==0x26 && MmxBossRushOwner(d)==1) leftovers=true;
  /* The replacement may be Rhino again: the roster only excludes bosses
   * still present. Require a new generation instead of a different id. */
  check(!leftovers && s.bosses[1].phase && s.bosses[1].generation!=generation,
      "Rhino and his drills are cleaned up and replaced");
  MmxBossRushForeignUnload();MmxBossRushForeignSync();
  check(!MmxBossRushForeignController(23) && !(MmxBossRushOffered()&(1u<<23)),
      "unloading the X3 pack removes Rhino");
  puts("MMX BOSS RUSH RHINO CHECKS PASSED");
}
