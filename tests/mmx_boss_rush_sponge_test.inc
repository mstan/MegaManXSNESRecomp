/* Fire a weapon at Wire Sponge from combat: page 0 fires X1 weapon `weapon`
 * (0 = buster), page 1 the loaded X2 pack's weapon. Every player shot is held
 * on him. `kill` drops him to 1 HP first. Reports damage, whether he
 * flinched (behavior $0C) and which death path he took ($3B: 6 splits). */
typedef struct SpongeHit {unsigned damage;bool flinched;unsigned death_path;bool halves;unsigned half_pixels;} SpongeHit;
static SpongeHit rush_sponge_hit(uint8_t *snapshot,size_t size,unsigned object,unsigned page,unsigned weapon,
                                 uint16_t button,bool kill) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"Wire Sponge hit case restores combat");
  SpongeHit result={0,false,0,false,0};unsigned halves_at=0;bool old_debris[31]={false};
  MmxWeaponsState w=MmxWeaponsGetState();
  w.initialized=1;w.page=(uint8_t)page;w.weapon=(uint8_t)(page?weapon:0);
  if(page) w.energy[(page-1)*8+weapon-1]=28;
  MmxWeaponsSetState(w);
  g_ram[0xbdb]=(uint8_t)(page?0:weapon*2);
  if(kill) g_ram[object+0x27]=(uint8_t)((g_ram[object+0x27]&0x80)|1);
  unsigned start_hp=g_ram[object+0x27]&127;
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned t=0;t<400;++t) {
    s=MmxBossRushGetState();
    if(!s.bosses[1].phase) break;
    for(unsigned d=0x1228;d<0x1428;d+=64) if(g_ram[d]) {
      memcpy(g_ram+d+5,g_ram+object+5,2);memcpy(g_ram+d+8,g_ram+object+8,2);
    }
    MmxWeaponCombatState c=MmxWeaponsGetCombatState();
    for(unsigned i=0;i<8;++i) if(c.shots[i].active) {
      c.shots[i].x=(int32_t)(eagle_word(g_ram+object+5)<<8);c.shots[i].y=(int32_t)(eagle_word(g_ram+object+8)<<8);
    }
    MmxWeaponsSetCombatState(c);
    /* Close enough for melee swings, without contact knockback. */
    for(unsigned d=0xe68;d<0x1628;d+=64) if((d<0x1228 || d>=0x1428) && MmxBossRushOwner(d)>=0 && g_ram[d])
      g_ram[d+0x26]=0;
    eagle_putword(g_ram+0xbad,eagle_word(g_ram+object+5)-24);eagle_putword(g_ram+0xbb0,eagle_word(g_ram+object+8));
    g_ram[0xbaa]=g_ram[0xbab]=g_ram[0xc2d]=0;g_ram[0xbb9]&=~64u;
    frame(t%30==0 && !result.damage?button:0);rush_keep_players_alive();
    unsigned hp=g_ram[object+0x27]&127;
    if(!result.damage && hp<start_hp) result.damage=start_hp-hp;
    /* MMX_SPONGE_FRAMES: his damage flash (first hit case only). */
    static unsigned flash_frames;
    if(getenv("MMX_SPONGE_FRAMES") && result.damage && flash_frames<12 && !kill) {
      static uint32_t review[256*224];char name[64];
      if(MmxRendererDraw(review,(MmxRenderView){256,0,4.0/3.0},true)) {
        snprintf(name,sizeof(name),"sponge-flash%02u.ppm",flash_frames);rush_rhino_save_ppm(name,review);
        fprintf(stderr,"flash %u attr=%02x\n",flash_frames,g_ram[object+0x11]);
      }
      ++flash_frames;
    }
    if(g_ram[object+1]==2 && g_ram[object+2]==0x0c) result.flinched=true;
    if(g_ram[object+1]==4 && g_ram[object+0x3b]) result.death_path=g_ram[object+0x3b];
    /* His halves are debris class $2B (spawned as variants $52/$53, which
     * their first update replaces with a left/right flag). His attacks use
     * class $2B debris too, so count it only once he is dying. */
    bool dying=g_ram[object+1]==4 || s.bosses[1].phase==MMX_RUSH_DYING;
    if(!dying) for(unsigned d=0x1928;d<0x1d08;d+=32) old_debris[(d-0x1928)/32]=g_ram[d] && g_ram[d+10]==0x2b;
    for(unsigned d=0x1928;d<0x1d08 && dying;d+=32)
      if(g_ram[d] && g_ram[d+10]==0x2b && MmxBossRushOwner(d)==1 && !old_debris[(d-0x1928)/32]) {
        result.halves=true;
        if(!halves_at) halves_at=t;
        /* They draw from his X2 art (effect-list children). */
        if(t==halves_at+10 && !result.half_pixels) {
          static uint32_t image[256*224];
          if(MmxRendererDraw(image,(MmxRenderView){256,0,4.0/3.0},true)) {
            result.half_pixels=rush_rhino_art_pixels(image,d,13);
            if(getenv("MMX_SPONGE_HALVES_PPM")) rush_rhino_save_ppm("rush-sponge-halves.ppm",image);
          }
        }
      }
    if(result.damage && !kill && t>120) break;
  }
  return result;
}
/* Wire Sponge's original X2 code, relocated from the user's ROM, fights in
 * seat 1 beside a native Penguin. MMX_BOSS_RUSH_X2_ROM names the X2 ROM. */
static void rush_sponge_checks(uint8_t *start,size_t cap,const char *x2) {
  char error[256];
  check(x2 && (MmxBossRushForeignReady(2) || MmxBossRushForeignLoad(2,x2,error,sizeof(error))),
      "X2 ROM loads for the Wire Sponge fixture");
  check(MmxBossRushForeignBuild(error,sizeof(error)),"Wire Sponge relocates into the cart padding");
  if(error[0]) fprintf(stderr,"sponge build: %s\n",error);
  check(MmxBossRushForeignController(13)==0xb59a29 && MmxBossRushForeignChild(13,0x10)==0xb6d5c9 &&
      MmxBossRushForeignEffectChild(13,0x1f)==0xb6ecbe,"Wire Sponge body, shot and effect controllers register");
  static const uint8_t effects[]={0x15,0x21,0x29,0x2b,0x72,0x73,0x74};
  for(unsigned i=0;i<sizeof(effects);++i) {
    uint32_t frames=0;const int16_t *clip=MmxBossRushAudioForeignClip(13,effects[i],&frames);
    fprintf(stderr,"sponge effect $%02x frames=%u\n",effects[i],frames);
    check(clip && frames>=320,"Wire Sponge's X2 effects render from his own driver and bank");
  }
  MmxBossRushForeignSetPreview(false);
  check(MmxBossRushOffered()&(1u<<13),"a loaded X2 pack offers Wire Sponge to normal runs");
  MmxBossRushState s=MmxBossRushGetState();
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
  memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);s.queued=0;
  check(MmxBossRushSetState(&s),"Wire Sponge fixture clears the opening pair");
  MmxBossRushRosterState roster={.available=MMX_RUSH_X1_MASK|(1u<<13),.queued=2};
  roster.queue[0]=0;roster.queue[1]=13;
  check(MmxBossRushRosterSetState(&roster),"Wire Sponge fixture queues Penguin and Wire Sponge");
  unsigned object=0;
  for(unsigned t=0;t<600 && !object;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[1].phase && s.bosses[1].id==13) object=s.bosses[1].object;
  }
  check(object!=0,"Wire Sponge spawns in seat 1");
  unsigned seen_behavior=0,max_hp=0,last=0xffffff,fighting_at=0,shots=0,effects_seen=0,shot_pixels=0,segment_pixels=0;
  int min_x=1<<30,max_x=-(1<<30),min_y=1<<30,max_y=-(1<<30);
  for(unsigned t=0;t<2400;++t) {
    /* Shorten his fall by 24 pixels, below any arena entrance (a 62-frame
     * fall once left his vine waiting forever, playtest 2026-10-18). */
    if(t==1) eagle_putword(g_ram+object+8,eagle_word(g_ram+object+8)+24);
    /* MMX_SPONGE_FORCE=<behavior, hex>: start it once combat begins (art
     * review of attacks his random choice may skip). */
    if(getenv("MMX_SPONGE_FORCE") && fighting_at && t==fighting_at+20) {
      g_ram[object+2]=(uint8_t)strtoul(getenv("MMX_SPONGE_FORCE"),NULL,16);g_ram[object+3]=0;
    }
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    /* MMX_SPONGE_FRAMES: save a render every 5 frames (art review). */
    if(getenv("MMX_SPONGE_FRAMES") && t%5==0 && t<1600) {
      static uint32_t review[256*224];char name[64];
      if(MmxRendererDraw(review,(MmxRenderView){256,0,4.0/3.0},true)) {
        snprintf(name,sizeof(name),"sponge-f%04u.ppm",t);rush_rhino_save_ppm(name,review);
      }
      fprintf(stderr,"objects t=%u",t);
      for(unsigned d=0xe68;d<0x1d08;d+=d<0x1628?64:32) {
        if(d==0x1228) d=0x1428;
        if(d==0x1628) d=0x1928;
        if(g_ram[d] && MmxBossRushOwner(d)==1)
          fprintf(stderr," [%04x x%04x y%04x c%02x v%02x st%02x s%02x p%02x a%02x b%02x d%02x]",d,
              (eagle_word(g_ram+d+5)-eagle_word(g_ram+0x1e4d))&0xffff,(eagle_word(g_ram+d+8)-eagle_word(g_ram+0x1e50))&0xffff,
              g_ram[d+10],g_ram[d+11],g_ram[d+1],g_ram[d+0x16],g_ram[d+0x17],g_ram[d+0x11],g_ram[d+0x18],g_ram[d+0x0e]);
      }
      fputc('\n',stderr);
    }
    if(!s.bosses[1].phase || s.bosses[1].id!=13) {fprintf(stderr,"sponge left seat 1 at t=%u\n",t);break;}
    unsigned state=g_ram[object+1]<<16|g_ram[object+2]<<8|g_ram[object+3];
    int x=(int)eagle_word(g_ram+object+5)-(int)s.camera_x,y=(int)eagle_word(g_ram+object+8)-(int)s.camera_y;
    if(state!=last) fprintf(stderr,"sponge t=%u state=%02x/%02x/%02x x=%d y=%d hp=%u set=%02x pose=%02x prof=%02x draw=%02x\n",
        t,state>>16,(state>>8)&255,state&255,x,y,g_ram[object+0x27]&127,g_ram[object+0x16],g_ram[object+0x17],g_ram[object+0x28],
        g_ram[object+0x0e]);
    last=state;
    if(g_ram[object+1]==2) {
      seen_behavior|=1u<<((g_ram[object+2]/2)&31);
      if(!fighting_at) fighting_at=t;
      if(x<min_x) min_x=x;
      if(x>max_x) max_x=x;
      if(y<min_y) min_y=y;
      if(y>max_y) max_y=y;
    }
    if((g_ram[object+0x27]&127)>max_hp) max_hp=g_ram[object+0x27]&127;
    /* His vine/seed/lightning shots and whip segments draw with his X2 art
     * (once each, a few frames after they appear). */
    unsigned shot=0,segment=0;
    for(unsigned d=0x1428;d<0x1628;d+=64) if(g_ram[d] && g_ram[d+10]==0x10 && MmxBossRushOwner(d)==1) {++shots;shot=d;}
    for(unsigned d=0x1928;d<0x1d08;d+=32) if(g_ram[d] && g_ram[d+10]==0x1f && MmxBossRushOwner(d)==1) {++effects_seen;segment=d;}
    static uint32_t attack_image[256*224];
    if(shot && shots>=8 && !shot_pixels && MmxRendererDraw(attack_image,(MmxRenderView){256,0,4.0/3.0},true)) {
      shot_pixels=rush_rhino_art_pixels(attack_image,shot,13);
      fprintf(stderr,"sponge shot art pixels=%u attr=%02x set=%02x pose=%02x\n",shot_pixels,g_ram[shot+0x11],
          g_ram[shot+0x16],g_ram[shot+0x17]);
      if(getenv("MMX_SPONGE_HALVES_PPM")) rush_rhino_save_ppm("rush-sponge-shot.ppm",attack_image);
    }
    if(segment && effects_seen>=8 && !segment_pixels && MmxRendererDraw(attack_image,(MmxRenderView){256,0,4.0/3.0},true)) {
      segment_pixels=rush_rhino_art_pixels(attack_image,segment,13);
      fprintf(stderr,"sponge whip art pixels=%u attr=%02x set=%02x pose=%02x\n",segment_pixels,g_ram[segment+0x11],
          g_ram[segment+0x16],g_ram[segment+0x17]);
      if(getenv("MMX_SPONGE_HALVES_PPM")) rush_rhino_save_ppm("rush-sponge-whip.ppm",attack_image);
    }
  }
  s=MmxBossRushGetState();
  fprintf(stderr,"sponge fighting_at=%u behaviors=%08x x=%d..%d y=%d..%d hp=%u shot-frames=%u effect-frames=%u phase=%u\n",
      fighting_at,seen_behavior,min_x,max_x,min_y,max_y,max_hp,shots,effects_seen,s.bosses[1].phase);
  check(fighting_at && s.bosses[1].phase==MMX_RUSH_FIGHTING,"Wire Sponge's original intro hands over to combat");
  check(max_hp==32,"Wire Sponge's intro fills his health meter");
  check(shot_pixels>=8 && segment_pixels>=16,"Wire Sponge's shots and whip draw with his X2 art");
  const MmxForeignPose *art=MmxBossRushForeignPose(13,g_ram[object+0x16],g_ram[object+0x17]&0x7f);
  fprintf(stderr,"sponge art: colors=%d set=%02x pose=%02x -> %dx%d\n",MmxBossRushForeignColors(13)!=NULL,
      g_ram[object+0x16],g_ram[object+0x17],art?art->width:0,art?art->height:0);
  check(MmxBossRushForeignColors(13) && art && art->width>=24 && art->height>=24,
      "Wire Sponge's poses draw from his X2 art");
  static uint32_t image[256*224];
  check(MmxRendererDraw(image,(MmxRenderView){256,0,4.0/3.0},true),"Wire Sponge combat frame renders");
  unsigned art_pixels=rush_rhino_art_pixels(image,object,13);
  fprintf(stderr,"sponge art pixels=%u attr=%02x\n",art_pixels,g_ram[object+0x11]);
  rush_rhino_save_ppm("rush-sponge-combat.ppm",image);
  check(art_pixels>=400,"Wire Sponge draws with his own X2 art and palette");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"Wire Sponge seat snapshot saves");
  /* Weaknesses and deaths. Sonic Slicer needs the X2 weapon pack built from
   * the same ROM. */
  bool own_pack=!MmxWeaponsPageEnabled(1);
  if(own_pack) {
    char cache[512];
    check(snesrecomp_exe_dir_path("sponge-x2-weapons.bin",cache,sizeof(cache)) &&
        MmxSourceAssetsBuild(x2,2,0,cache,error,sizeof(error)) && MmxWeaponsLoadPage(cache,1),
        "the X2 weapon pack loads for Wire Sponge's weakness");
  }
  /* Saber Zero fires weapons with X and slashes with Y; Modern Zero's Fire
   * with the buster selected is its saber swing, so its basic attack is the
   * saber (checked below). */
  const char *who=rush_rhino_character();
  bool saber=!strcmp(who,"saber"),modern=!strcmp(who,"modern");
  uint16_t fire_button=saber?SNES_PAD_X:SNES_PAD_Y;
  SpongeHit buster=rush_sponge_hit(start,n,object,0,0,fire_button,false);
  SpongeHit slicer=rush_sponge_hit(start,n,object,1,5,fire_button,false);
  SpongeHit fire=rush_sponge_hit(start,n,object,0,4,fire_button,false);
  SpongeHit buster_kill=rush_sponge_hit(start,n,object,0,0,fire_button,true);
  SpongeHit slicer_kill=rush_sponge_hit(start,n,object,1,5,fire_button,true);
  fprintf(stderr,"sponge hits (%s): buster=%u/%u slicer=%u/%u fire=%u/%u kill: buster path=%u halves=%u slicer path=%u halves=%u\n",
      who,buster.damage,buster.flinched,slicer.damage,slicer.flinched,fire.damage,fire.flinched,
      buster_kill.death_path,buster_kill.halves,slicer_kill.death_path,slicer_kill.halves);
  check(buster.damage>=1 && !buster.flinched,"the character's basic attack deals Wire Sponge ordinary damage");
  check(slicer.damage>=2 && slicer.flinched,"Sonic Slicer is Wire Sponge's weakness and makes him flinch");
  check(fire.damage>=1 && !fire.flinched,"Fire Wave is not one of Wire Sponge's weaknesses");
  if(!modern) check(buster_kill.death_path==2 && !buster_kill.halves,"an ordinary kill explodes Wire Sponge");
  check(slicer_kill.death_path==6 && slicer_kill.halves,"a Sonic Slicer kill splits Wire Sponge in two");
  fprintf(stderr,"sponge halves art pixels=%u\n",slicer_kill.half_pixels);
  check(slicer_kill.half_pixels>=200,"Wire Sponge's halves draw with his X2 art");
  /* Zero's saber splits him too. */
  if(modern || saber) {
    SpongeHit slash=rush_sponge_hit(start,n,object,0,0,SNES_PAD_Y,false);
    SpongeHit slash_kill=rush_sponge_hit(start,n,object,0,0,SNES_PAD_Y,true);
    fprintf(stderr,"sponge saber (%s): damage=%u flinched=%u kill path=%u halves=%u\n",who,
        slash.damage,slash.flinched,slash_kill.death_path,slash_kill.halves);
    check(slash.damage>=1 && !slash.flinched,"Zero's saber deals Wire Sponge ordinary damage");
    check(slash_kill.death_path==6 && slash_kill.halves,"a Zero saber kill splits Wire Sponge in two");
  }
  MmxWeaponsCancelShots(g_ram);
  if(own_pack) MmxWeaponsDisable();
  MmxBossRushForeignSetPreview(false);
  puts("MMX BOSS RUSH WIRE SPONGE CHECKS PASSED");
}
