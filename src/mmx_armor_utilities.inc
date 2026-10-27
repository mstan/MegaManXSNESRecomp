static void giga_player(uint8_t *r) {
  if(!playable(r)) {state.giga_tick=0;r[0xbc6]=0x40;state.anim_valid=0;return;}
  if(state.giga_tick>=146 && !state.giga_fired) {
    /* Source class $0F becomes live at tick 146 for 120 ticks. Its
     * canonical simulation field is independent of local aspect ratio. */
    MmxArmorShot *s=allocate_shot(r,7,0,0,((int32_t)word(r+0x1e4d)+128)*256,
        ((int32_t)word(r+0x1e50)+112)*256,0);
    if(s)state.giga_fired=1;
  }
  r[0xbde]=r[0xbe2]=r[0xbe3]=0;r[0xbdf]=128;
  r[0xbaa]=8;r[0xbab]=2;r[0xbc6]=0;
  putword(r+0xbc2,0);putword(r+0xbc4,0);
}
bool MmxArmorFrameTick(uint8_t *r,uint16_t pad,bool available) {
  if(!pack || !r)return false;
  uint8_t *body=body_query?body_query(r):r+0xba8;
  if(r[0xd1]!=2) {MmxArmorResetState();return false;}
  if(r[0xd2]!=4) {
    /* Native checkpoint restarts can reload the same stage number. Make its
     * next player initializer reset transient abilities and refill Gold just
     * as a different stage does, while retaining the real secret reward. */
    state.valid=0;
    state.radar_stage=state.tracer=state.utility_held=state.radar_closing=0;state.giga_tick=0;return false;
  }
  if(!available || !body || !(body[0x27]&127)) {
    state.tracer=state.utility_held=state.radar_closing=0;state.giga_tick=0;return false;
  }
  if(state.giga_tick) {
    if(r[0x1f10])return false;
    if(state.giga_tick==242) {
      state.giga_tick=0;state.anim_valid=0;putword(body+0x1e,0x40);body[2]=8;body[3]=0;return false;
    }
    ++state.giga_tick;
  }
  bool up=(pad&0xc10)==0xc10 && (r[0x1f99]&1);
  bool down=(pad&0xc20)==0xc20 && (game==2?(r[0x1f99]&4):state.gold && (r[0x1f99]&2));
  unsigned combo=up?1:down?2:0;
  bool pressed=combo && state.utility_held!=combo;state.utility_held=(uint8_t)combo;
  if(!state.giga_tick && combo==2 && pressed && r[0xd3]==4 &&
      !r[0x1f10] && !r[0x1f0c] && !r[0x1f23] && !r[0x1f48]) {
    if(game==3) {state.hyper^=1;armor_sound(r,0x2c);}
    else if(state.valid && state.stage==r[0x1f7a] && config[9] && state.energy>=0x1c00 &&
        !body[0x35] && (body[2]<=10 || body[2]==0x14)) {
      state.energy=0;state.giga_tick=1;state.giga_fired=0;state.tracer=0;
      state.burst=state.burst_end=state.burst_queued=state.stored_shot=state.charge=0;
      if(body[0x87]&64) {armor_sound(r,0x17);body[0x87]&=(uint8_t)~64;}
      memset(body+0x57,0,5);body[0x82]=0;body[0x2b]&=(uint8_t)~4;
      putword(body+0x1a,0);putword(body+0x1c,0);putword(body+0x1e,0);armor_sound(r,2);
    }
  }
  if(state.giga_tick && state.giga_tick<146) {
    /* $88:9E..A1: the first 31 ticks lift X by a pixel. While the world is
     * held, edit only X's owned body and use the universal ceiling probe.
     * A stored co-op partner moves without projecting the other seat. */
    if(state.giga_tick<=31) {
      int x=(int)word(body+5),y=(int)word(body+8);bool ceiling=y<=20;
      if(armor_terrain)for(int dx=-6;dx<=6;dx+=6)ceiling|=armor_terrain(r,x+dx,y-19);
      if(!ceiling) {putword(body+8,(unsigned)(y-1));putword(body+0x24,(unsigned)(y-1));body[7]=0;}
    }
    r[0xb9d]=r[0xba0]=0;return true;
  }
  if(!(r[0x1f99]&1)) {state.tracer=state.radar_closing=0;return false;}
  if(state.radar_closing) {
    if(!(pad&0xf1d))state.radar_closing=0;
    r[0xab]=r[0xac]=r[0xbe2]=r[0xbe3]=0;r[0xb9d]=r[0xba0]=0;return true;
  }
  if(game==3 && r[0xd3]==4 && body[1]==2 && !body[2] && body[3]==2 && (body[0x2b]&4) &&
      !r[0x1f10] && !r[0x1f0c] && !r[0x1f23] && !r[0x1f48] && state.radar_stage!=r[0x1f7a]+1) {
    state.radar_stage=(uint8_t)(r[0x1f7a]+1);state.tracer=1;++state.radar_epoch;
  } else if(combo==1 && pressed) {
    state.tracer^=1;state.utility_ticks=180;++state.radar_epoch;armor_sound(r,0x2c);
    if(game==3 && !state.tracer)state.radar_closing=1;
  } else if(game==3 && state.tracer && (pad&0x30d)) {
    state.tracer=0;state.radar_closing=1;r[0xab]=r[0xac]=0;r[0xb9d]=r[0xba0]=0;return true;
  }
  if(game==2 && state.tracer && state.utility_ticks && !--state.utility_ticks)state.tracer=0;
  if(game==3 && (state.tracer || state.radar_closing)) {r[0xb9d]=r[0xba0]=0;return true;}
  return false;
}
