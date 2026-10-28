/* Donor buster port. Every damaging component occupies an ordinary X1
 * projectile slot. Original X1 hit testing, armor, immunity and boss reactions
 * still decide whether a contact is accepted. Host state only supplies the
 * donor controller/graphics and translated positive damage. */
static const ArmorGroup *effect_group(unsigned id) {
  for(unsigned i=0;i<extra_count;++i)if(extra_groups[i].id==id)return extra_groups+i;
  return NULL;
}
static void barrier_record(unsigned at) {
  const ArmorGroup *g=effect_group(0x65);
  if(!g || at<g->records_begin || at+3>g->animation_size || !g->animation[at] ||
     g->animation[at+2]>=g->count) {state.barrier_timer=0;return;}
  state.barrier_offset=(uint16_t)at;state.barrier_timer=g->animation[at];
  state.barrier_flags=g->animation[at+1];state.barrier_pose=g->animation[at+2];
}
static void barrier_start(void) {
  const ArmorGroup *g=effect_group(0x65);state.barrier_phase=0;
  if(g && g->sequences>=3) {barrier_record(word(g->animation));state.barrier_delay=1;}
}
static void barrier_tick(uint8_t *r) {
  if(!state.barrier_ticks) {state.barrier_level=state.barrier_delay=state.barrier_timer=state.barrier_phase=0;return;}
  state.barrier_x=(int32_t)word(r+0xbad)*256;state.barrier_y=(int32_t)word(r+0xbb0)*256;
  const ArmorGroup *g=effect_group(0x65);
  if(!g || !state.barrier_timer) {
    if(!--state.barrier_ticks)state.barrier_level=state.barrier_delay=0;
    if(state.barrier_delay)--state.barrier_delay;return;
  }
  /* $81:86D0 waits for the complete five-pose appearance, then chooses
   * standard sequence 2 or enhanced sequence 1. The 300-tick active
   * lifetime starts after this setup, rather than during the windup. */
  if(!state.barrier_phase && (state.barrier_flags&128)) {
    state.barrier_phase=state.barrier_level;state.barrier_delay=0;
    barrier_record(word(g->animation+(state.barrier_level==2?1:2)*2));
  } else if(state.barrier_phase && !--state.barrier_ticks) {
    state.barrier_level=state.barrier_delay=state.barrier_timer=state.barrier_phase=0;return;
  }
  if(!--state.barrier_timer) {
    unsigned next=state.barrier_offset+3;
    if(state.barrier_flags&128) {
      if(next+2>g->animation_size) {state.barrier_ticks=0;return;}
      next=(unsigned)((int)next+(int16_t)word(g->animation+next));
    }
    barrier_record(next);
  }
}
static bool armor_slot(unsigned d) {return d>=0x1228 && d<0x1428 && (d&63)==0x28;}
bool MmxArmorProjectileOwned(const uint8_t *r,unsigned d) {
  return r && armor_slot(d) && r[d] && word(r+d+0x3e)==0x414d;
}
static void shot_record(MmxArmorShot *s,unsigned at) {
  const ArmorGroup *g=effect_group(s->group);
  if(!g || at<g->records_begin || at+3>g->animation_size || !g->animation[at] ||
     g->animation[at+2]>=g->count) {s->active=0;return;}
  s->animation=(uint16_t)at;s->timer=g->animation[at];
  s->flags=g->animation[at+1];s->pose=g->animation[at+2];
}
static void shot_sequence(MmxArmorShot *s,unsigned sequence) {
  const ArmorGroup *g=effect_group(s->group);
  if(!g || sequence>=g->sequences) {s->active=0;return;}
  shot_record(s,word(g->animation+sequence*2));
}
static void shot_animate(MmxArmorShot *s) {
  if(!s->timer || --s->timer)return;
  const ArmorGroup *g=effect_group(s->group);unsigned next=s->animation+3;
  if(s->flags&128) {
    if(!g || next+2>g->animation_size) {s->active=0;return;}
    next=(unsigned)((int)next+(int16_t)word(g->animation+next));
  }
  shot_record(s,next);
}
static void armor_sound(uint8_t *r,unsigned command) {
  unsigned i=r[0xba3]&30;r[0xb72+i]=(uint8_t)command;r[0xb73+i]=0;r[0xba3]=(uint8_t)((i+2)&30);
}
static void armor_clear_charge(uint8_t *r) {
  if(r[0xc2f]&64) {armor_sound(r,0x17);r[0xc2f]&=(uint8_t)~64;}
  memset(r+0xbff,0,5);r[0xc2a]=0;
}
static void retire_shot(uint8_t *r,unsigned i) {
  unsigned d=0x1228+i*64;
  if(MmxArmorProjectileOwned(r,d)) {memset(r+d,0,64);if(r[0xbdd])--r[0xbdd];}
  memset(state.shots+i,0,sizeof(state.shots[i]));
}
static void buster_cancel(uint8_t *r) {
  if(state.burst || state.stored_shot)armor_clear_charge(r);
  state.burst=state.burst_end=state.burst_queued=state.stored_shot=state.charge=state.fire_held=0;
}
enum { ARMOR_BOUNDS_MAIN=0xff00,ARMOR_BOUNDS_HALF=0xff10,ARMOR_BOUNDS_GIGA=0xff28 };
void MmxArmorCollisionRom(uint8_t *rom,size_t size) {
  static uint8_t previous[44];static bool installed;
  collision_rom=rom;collision_rom_size=size;
  if(!rom || size<0x37f2c || (!pack && !installed))return;
  uint8_t empty[44],boxes[44];memset(empty,255,sizeof(empty));memcpy(boxes,empty,sizeof(boxes));
  if(pack && config[2] && config[5]) {
    memcpy(boxes,config[5],4);memcpy(boxes+4,config[2],8);
    memcpy(boxes+12,game==3?config[6]+4:config[2]+4,4);
    if(config[8])memcpy(boxes+16,config[8],24);
    if(game==2 && config[9])memcpy(boxes+40,config[9],4);
  }
  /* $86:FF00..2B is verified padding. Keep all armor records in this one
   * span: Saber Zero owns FF40..67, FFA0..AF and FFD8..FFFF; other imported
   * weapons own FF80..9F and the original Zero owns FFB0..D7. */
  if(!memcmp(rom+0x37f00,empty,sizeof(empty)) ||
      (installed && !memcmp(rom+0x37f00,previous,sizeof(previous)))) {
    memcpy(rom+0x37f00,boxes,sizeof(boxes));memcpy(previous,boxes,sizeof(previous));installed=pack!=NULL;
  }
}
static void sync_shot(uint8_t *r,unsigned i) {
  MmxArmorShot *s=state.shots+i;unsigned d=0x1228+i*64;
  putword(r+d+5,(unsigned)(s->x>>8));r[d+4]=(uint8_t)s->x;
  putword(r+d+8,(unsigned)(s->y>>8));r[d+7]=(uint8_t)s->y;
  putword(r+d+0x1a,(uint16_t)s->vx);putword(r+d+0x1c,(uint16_t)s->vy);
  unsigned phase=s->flags&7;if(phase>4)phase=4;
  putword(r+d+0x20,s->kind==7?ARMOR_BOUNDS_GIGA:s->kind==8?ARMOR_BOUNDS_HALF+phase*4:ARMOR_BOUNDS_MAIN+(s->kind==1?0:s->kind==4?8:s->kind==6?12:4));
  r[d+0xe]=0x81;r[d+0x10]=0xff;r[d+0x11]=(uint8_t)(0x26|s->facing);
}
static MmxArmorShot *allocate_shot(uint8_t *r,unsigned kind,unsigned group,unsigned sequence,
                                  int32_t x,int32_t y,unsigned facing) {
  for(unsigned i=0;i<8;++i) {
    unsigned d=0x1228+i*64;if(word(r+d))continue;
    MmxArmorShot *s=state.shots+i;memset(s,0,sizeof(*s));
    s->active=1;s->kind=(uint8_t)kind;s->group=(uint8_t)group;s->facing=(uint8_t)(facing&64);s->palette=3;
    s->x=x;s->y=y;if(kind!=7)shot_sequence(s,sequence);if(!s->active)return NULL;
    memset(r+d,0,64);r[d]=1;r[d+1]=2;r[d+10]=(uint8_t)(kind==8?1:3);putword(r+d+0x3e,0x414d);++r[0xbdd];
    sync_shot(r,i);state.shot_mask|=(uint16_t)(1u<<i);return s;
  }
  return NULL;
}
static unsigned burst_sequence(void) {
  return (game==2?41:48)+(state.burst==2?6:0)+(state.burst_air?19:0);
}
static void burst_record(unsigned offset) {
  if(offset<records_begin || offset+3>animation_size || !animation[offset] || animation[offset+2]>=frames) {
    state.burst=0;return;
  }
  state.burst_offset=(uint16_t)offset;state.burst_timer=animation[offset];
  state.burst_flags=animation[offset+1];state.burst_pose=animation[offset+2];
}
static void start_armor_burst(uint8_t *r,unsigned which) {
  end_dash(r);state.burst=(uint8_t)which;state.burst_end=state.burst_fired=state.burst_transition=0;
  state.burst_air=!(r[0xbd3]&4);state.burst_facing=r[0xc11]&64;
  burst_record(word(animation+burst_sequence()*2));armor_clear_charge(r);
}
static void emit_armor_burst(uint8_t *r) {
  int dx=24,dy=-3;
  if(game==2 && config[1]) {
    unsigned index=burst_sequence()*2;
    if(index+2<=config_size[1]) {dx=(int8_t)config[1][index];dy=(int8_t)config[1][index+1];}
  } else if(game==3 && config[1] && state.burst_pose<100) {
    unsigned a=config[1][state.burst_pose];
    if(a && a+2<=256) {dy=(int8_t)config[1][100+a];dx=-(int8_t)config[1][101+a];}
  }
  if(!state.burst_facing)dx=-dx;
  int32_t x=(int32_t)word(r+0xbad)*256+dx*256,y=(int32_t)word(r+0xbb0)*256+dy*256;
  bool special=state.stored_shot==2;
  unsigned kind=game==2?(!special?8:state.burst==2?2:1):
      (state.burst==1?3:special?1:8);
  MmxArmorShot *s=allocate_shot(r,kind,kind==8?14:kind==2?0x54:kind==3?0x3a:0x9e,kind==2?2:0,x,y,state.burst_facing);
  if(!s)return;
  s->vx=(int16_t)((state.burst_facing?1:-1)*(kind==2?0x200:kind==3?0x80:0x600));
  if(kind==1)shot_sequence(s,0);
  if(kind==2 || kind==3) {
    unsigned parent=(unsigned)(s-state.shots),count=kind==2?2:3;
    for(unsigned sub=count;sub;--sub) {
      MmxArmorShot *p=allocate_shot(r,4,kind==2?0x55:s->group,kind==2?1:3,x,y,s->facing);if(!p)break;
      p->parent=(uint8_t)parent;p->sub=(uint8_t)sub;p->segment_timer=1;
      const uint8_t *table=config[3];unsigned start=game==2?4:6;
      p->segment=table?table[start+sub]:0;
      if(game==3 && table)p->palette=(uint8_t)(table[10+sub]>>1&7);
      if(table) {p->x+=(int8_t)table[(sub-1)*2]*256;p->y+=(int8_t)table[(sub-1)*2+1]*256;}
    }
  }
  armor_sound(r,2);r[0x1f0d]=4;
}
static void buster_player(uint8_t *r) {
  if(!config[1] || r[0xbdb] || (buster_query && !buster_query())) {buster_cancel(r);return;}
  bool held=(r[0xbdf]&64)!=0,pressed=(r[0xbe3]&64)!=0;
  unsigned live=0;for(unsigned i=0;i<8;++i)if(state.shots[i].active && MmxArmorProjectileOwned(r,0x1228+i*64))live|=1u<<i;
  state.shot_mask=(uint16_t)live;
  if(state.hyper_active && state.gold && game==3 && (r[0x1f99]&2) && pressed &&
      !state.burst && !state.stored_shot && config[10]) {
    unsigned cost=word(config[10]);if((r[0x1f99]&5)==5)cost/=2;
    if(cost && state.hyper_energy>=cost) {
      state.hyper_energy=(uint16_t)(state.hyper_energy-cost);state.stored_shot=2;start_armor_burst(r,1);
    } else state.hyper_active=0;
  }
  if(state.burst==1 && state.burst_fired && pressed)state.burst_queued=1;
  if(state.burst_end) {
    state.burst=state.burst_end=0;state.anim_valid=0;r[0xbab]=0;
    if(!state.stored_shot)state.charge=0;
  }
  if(!state.burst) {
    if(state.stored_shot && (pressed || state.burst_queued) && !r[0x1f0d]) {
      state.burst_queued=0;start_armor_burst(r,2);
    }
    else if(!state.stored_shot) {
      if(held) {
        unsigned cap=(r[0x1f99]&2)?201:81;if(state.charge<cap)++state.charge;
        /* Feed native charging sound/motes at the donor's thresholds, then
         * explicitly select the release tier. The first held frame fires
         * the pellet, so thresholds 20/80/140/200 need 21/81/141/201 input
         * frames in the original ROM trials. */
        if(state.charge==21)r[0xbff]=0x97;
        else if(state.charge>21 && state.charge<81)r[0xbff]=0x90;
        else if(state.charge==81)r[0xbff]=0x51;
        else if(state.charge>81)r[0xbff]=0x40;
        if(state.charge>=141) {r[0xc03]=1;r[0xc2a]=4;}
      }
      else if(state.fire_held) {
        unsigned charge=state.charge;
        if(charge>=141 && (r[0x1f99]&2)) {
          state.stored_shot=charge>=201?2:1;start_armor_burst(r,1);
        } else {armor_clear_charge(r);if(charge>=21)r[0xc01]=charge>=81?8:2;}
        state.charge=0;
      }
    }
  }
  state.fire_held=(uint8_t)held;
  if(!state.burst) {
    /* A stored follow-up owns the next fire press. Never let native release
     * logic produce an additional X1 shot while it waits. */
    if(state.stored_shot) {armor_clear_charge(r);r[0xbdf]&=(uint8_t)~64;r[0xbe3]&=(uint8_t)~64;}
    return;
  }
  if(state.burst_transition) {
    burst_record(word(animation+(burst_sequence()+(state.burst_flags&7))*2));state.burst_transition=0;
  }
  if(state.burst_flags&128) {
    state.burst_end=1;
    if(state.burst==2)state.stored_shot=0;
  } else {
    if((state.burst_flags&64) && !state.burst_fired) {emit_armor_burst(r);state.burst_fired=1;}
    bool air=!(r[0xbd3]&4);
    if(air!=!!state.burst_air) {state.burst_air=(uint8_t)air;state.burst_transition=1;}
    else if(!--state.burst_timer)burst_record(state.burst_offset+3);
  }
  armor_clear_charge(r);r[0xc11]=state.burst_facing;
  r[0xbdf]&=(uint8_t)~64;r[0xbe3]&=(uint8_t)~64;
  if(!state.burst_air) {
    r[0xbde]=r[0xbe2]=0;
    if(r[0xbaa]!=0 && r[0xbaa]!=8 && r[0xbaa]!=10)r[0xbaa]=r[0xbab]=0;
  }
}
static void cross_fragments(uint8_t *r,MmxArmorShot *s) {
  if(!config[6])return;
  /* $81:C49D creates the central class $1F and four class $20 lobes. */
  for(unsigned sub=0;sub<5;++sub) {
    unsigned variant=sub?1:0;
    MmxArmorShot *p=allocate_shot(r,6,variant?0x3a:0x54,variant?0:1,s->x,s->y,s->facing);
    if(!p)continue;
    const uint8_t *v=config[6]+8+sub*8;
    p->sub=(uint8_t)sub;p->segment=v[0];p->segment_timer=v[1];p->cross_fragment=1;
    /* $81:C5AC..D9: the lobes initially travel back toward X, then their
     * signed acceleration reverses them. The primary travels forward. */
    p->vx=(int16_t)word(v+2);if(s->facing)p->vx=(int16_t)-p->vx;
    p->vy=(int16_t)word(v+4);p->facing=(uint8_t)((s->facing&64)|(v[6]&128));
    p->palette=(uint8_t)(v[6]>>1&7);
  }
}
unsigned MmxArmorProjectileTick(uint8_t *r,unsigned d,unsigned original) {
  if(pack && armor_slot(d) && r[d] && !r[d+1] && (r[d+10]==1 || r[d+10]==3) &&
     armor_actor() && !r[0xbdb] && (!buster_query || buster_query())) {
    /* The native firing command still owns allocation, count limits and the
     * ordinary movement/firing overlay. Translate its new half/full shot
     * before the X1 initializer chooses CHR or starts its beam controller. */
    unsigned i=(d-0x1228)/64;MmxArmorShot *s=state.shots+i;memset(s,0,sizeof(*s));
    s->active=1;s->kind=r[d+10]==1?8:1;s->group=s->kind==8?14:0x9e;s->palette=3;
    s->facing=r[0xc11]&64;int dx=24,dy=-3;
    if(game==2 && config[1]) {
      unsigned sequence=source_sequence(r[d+0x3c]/2),index=sequence*2;
      if(index+2<=config_size[1]) {dx=(int8_t)config[1][index];dy=(int8_t)config[1][index+1];}
    } else if(config[1] && state.anim_valid && state.anim_pose<100) {
      unsigned a=config[1][state.anim_pose];if(a && a+2<=256) {dy=(int8_t)config[1][100+a];dx=-(int8_t)config[1][101+a];}
    }
    s->x=((int32_t)word(r+0xbad)+(s->facing?dx:-dx))*256;s->y=((int32_t)word(r+0xbb0)+dy)*256;
    s->vx=(int16_t)(s->facing?0x600:-0x600);shot_sequence(s,0);
    putword(r+d+0x3e,0x414d);r[d+1]=2;
  }
  if(!MmxArmorProjectileOwned(r,d))return original;
  unsigned i=(d-0x1228)/64;MmxArmorShot *s=state.shots+i;
  if(!pack || !s->active || !r[0xbcf]) {retire_shot(r,i);return 0;}
  ++s->age;
  if(s->kind==1) {
    if(s->age==1)s->pose=game==2?4:3;
    else if(s->age==2)shot_sequence(s,0);
    else if(s->age==7) {shot_sequence(s,game==2?1:2);s->vx=(int16_t)(s->facing?0x800:-0x800);}
    else shot_animate(s);
    if(s->age>7)s->x+=s->vx;
  } else if(s->kind==2 || s->kind==3) {
    /* Original initialization submits the muzzle position before integrating. */
    if(s->age>1) {
      int speed=s->vx<0?-s->vx:s->vx;
      speed+=s->kind==2 || s->age>16?64:8;
      int limit=s->kind==2?0x800:0x500;if(speed>limit)speed=limit;
      s->vx=(int16_t)(s->facing?speed:-speed);s->x+=s->vx;
      if(s->kind==2)shot_animate(s);
    }
    if(s->kind==3)for(unsigned j=0;j<8;++j) {
      MmxArmorShot *p=state.shots+j;
      if(p->active && p->kind==1 && p->age>=7 &&
         abs((int)((p->x-s->x)>>8))<=30 && abs((int)((p->y-s->y)>>8))<=20) {
        s->x=p->x;s->y=p->y;s->kind=5;s->group=0x54;s->vx=s->vy=0;s->age=1;
        memset(&s->hit_slots,0,sizeof(s->hit_slots));shot_sequence(s,0);retire_shot(r,j);break;
      }
    }
  } else if(s->kind==4) {
    MmxArmorShot *parent=state.shots+s->parent;
    if(!parent->active || (parent->kind!=2 && parent->kind!=3)) {
      if(game==3 && s->sub==1) {retire_shot(r,i);return 0;}
      s->kind=6;s->vx=(int16_t)(s->facing?0x800:-0x800);s->vy=0;
    } else if(config[3]) {
      const uint8_t *t=config[3]+(game==2?7:21);unsigned extent=config_size[3]-(game==2?7:21);
      if(s->segment_timer && !--s->segment_timer) {
        unsigned at=s->segment;
        if(at>=extent || !t[at])at=0;
        if(at+6>extent) {retire_shot(r,i);return 0;}
        s->segment_timer=t[at];s->vx=(int16_t)word(t+at+2);s->vy=(int16_t)word(t+at+4);
        s->segment=(uint8_t)(at+6);
      }
      s->x+=s->vx+(parent->age>1?parent->vx:0);s->y-=s->vy;
      shot_animate(s);
      unsigned at=s->history_pos;s->trail_x[at]=(int16_t)(s->x>>8);s->trail_y[at]=(int16_t)(s->y>>8);
      s->trail_flags[at]=s->flags;s->history_pos=(uint8_t)((at+1)&31);
    }
  } else if(s->kind==5) {
    if(s->flags && !s->sub) {s->sub=1;cross_fragments(r,s);}
    if(s->flags&128) {retire_shot(r,i);return 0;}
    shot_animate(s);
  } else if(s->kind==6) {
    if(s->age>1) {
      if(s->cross_fragment) {
        /* $81:C610 caps the velocity before $82:D7C2 subtracts the signed
         * acceleration and integrates. Preserve that ordering, including
         * the small post-cap overshoot in the original. */
        int ax=(int8_t)s->segment_timer,ay=(int8_t)s->segment;
        if(s->facing&64) {ax=-ax;ay=-ay;if(s->vx>0x800)s->vx=0x800;}
        else if(s->vx < -0x800)s->vx=-0x800;
        s->vx=(int16_t)(s->vx-ax);s->vy=(int16_t)(s->vy-ay);
      }
      s->x+=s->vx;s->y-=s->vy;shot_animate(s);
    }
  } else if(s->kind==8) {
    if(s->age>1) {s->x+=s->vx;shot_animate(s);}
  } else if(s->kind==7 && s->age>120) {
    retire_shot(r,i);return 0;
  }
  /* Later games give projectile/effect queues a 32px camera margin. Use
   * the native shared-camera origin here; renderer margins never extend life. */
  int x=(int)(s->x>>8)-(int)word(r+0x1e4d),y=(int)(s->y>>8)-(int)word(r+0x1e50);
  if(!s->active || x < -64 || x>320 || y < -64 || y>288 || s->age>600) {retire_shot(r,i);return 0;}
  sync_shot(r,i);return 0;
}
static unsigned armor_enemy_bit(unsigned e) {
  return e>=0xe68 && e<0x1228 && (e&63)==0x28?1u<<((e-0xe68)/64):0;
}
unsigned MmxArmorHitbox(const uint8_t *r,unsigned enemy,unsigned d,unsigned original) {
  if(state.giga_tick && armor_actor() && (enemy==0xba8 || d==0xba8))return 0;
  if(MmxArmorProjectileOwned(r,d) && (state.shots[(d-0x1228)/64].hit_slots&armor_enemy_bit(enemy)))return 0;
  return original;
}
unsigned MmxArmorDamage(uint8_t *r,unsigned enemy,unsigned d,unsigned original) {
  if(!MmxArmorProjectileOwned(r,d) || !original || (original&128))return original;
  MmxArmorShot *s=state.shots+(d-0x1228)/64;unsigned bit=armor_enemy_bit(enemy);
  if(!bit || !s->active)return original;
  if(s->hit_slots&bit)return 0;s->hit_slots|=(uint16_t)bit;
  /* Boss-specific X1 full-buster response remains authoritative. Ordinary
   * enemies use the donor's neutral buster ratios on X1's basic HP scale. */
  if(r[enemy+0x28]>=6 || !collision_rom || collision_rom_size<=0x37013 || !config[4])return original;
  unsigned row=0x36f37+word(collision_rom+0x36f37+r[enemy+0x28]*2);
  if(row>=collision_rom_size)return original;
  unsigned cls=s->kind==7?15:s->kind==8?1:s->kind==2 || s->kind==3 || s->kind==4 || s->kind==5?29:s->kind==6?(s->sub?32:31):3;
  unsigned damage=(collision_rom[row]*config[4][cls]+1)/3;return damage>127?127:damage;
}
unsigned MmxArmorResponse(uint8_t *r,unsigned enemy,unsigned d,unsigned original) {
  (void)r;(void)enemy;(void)d;
  /* Preserve native deflection, immunity and armored/boss reactions. */
  return original;
}
