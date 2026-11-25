#include "mmx_qol.h"

static unsigned features;
void MmxQolReset(void) {features=0;}
void MmxQolEnable(unsigned feature) {features|=feature;}
bool MmxQolEnabled(void) {return features!=0;}
bool MmxQolFeatureEnabled(unsigned mask) {return (features&mask)!=0;}
bool MmxQolDamageEnabled(void) {
  return (features&(MMX_QOL_HADOUKEN_IFRAMES|MMX_QOL_HADOUKEN_SIGMA))!=0;
}

static void accumulator8(CpuState *cpu,unsigned value) {
  cpu->A=(cpu->A&0xff00)|(value&255);
  cpu->_flag_Z=!(value&255);cpu->_flag_N=(value&128)!=0;
  cpu->P=(cpu->P&~0x82)|(cpu->_flag_Z?2:0)|(cpu->_flag_N?128:0);
}
void MmxQolFireIntent(const uint8_t *r,bool ready,bool *held,bool *pressed) {
  bool physical_held=*held,physical_pressed=*pressed;
  if(features&MMX_QOL_AUTO_CHARGE) *held=!physical_held;
  if((features&MMX_QOL_RAPID_FIRE) && physical_held) {
    *held=false;*pressed=true;
  }
  /* The native release owns charge tier, shot allocation and eligibility.
   * Suppress the ordinary pellet on the same press as a charged release. */
  if((features&MMX_QOL_AUTO_CHARGE) && physical_pressed && ready) *pressed=false;
  (void)r;
}
unsigned MmxQolShotValue(const uint8_t *r,unsigned original,bool held_read,bool ready) {
  bool held=(r[0xbdf]&64)!=0,pressed=(r[0xbe3]&64)!=0;
  if(!held_read || !r[0xbdb])MmxQolFireIntent(r,ready,&held,&pressed);
  else if(features&MMX_QOL_RAPID_FIRE)held=false;
  return (original&~64u)|((held_read?held:pressed)?64:0);
}
unsigned MmxQolUpgradeRead(uint8_t *r,unsigned pc,unsigned original,bool is_x) {
  pc&=0x7fffff;
  if(!is_x || (pc!=0x01971f && pc!=0x019796 && pc!=0x0198ff))return original;
  if(pc==0x01971f && (features&MMX_QOL_NO_DOUBLE_TAP)) {
    r[0xbfc]=r[0xbf9]=r[0xbfb]=0;original&=~8u;
  }
  return original;
}
static bool normal_shot_read(uint32_t pc) {
  switch(pc) {
    case 0x018332:case 0x0183c0:case 0x018430:case 0x0184b8:
    case 0x018562:case 0x01862f:case 0x018759:case 0x0187a0:
    case 0x01887a:case 0x018956:case 0x0189d1:case 0x018e14:return true;
    default:return false;
  }
}
static bool reentry(CpuState *cpu,const uint8_t *r) {
  unsigned stage=r[0x1f7a];
  if(stage>=13)return false; /* setup temporarily biases a real intro by 13 */
  if(!stage || stage>=9 || !cpu_read8(cpu,0x86,0x88c0+stage))return true;
  unsigned defeated=0;
  for(unsigned i=16;i;) {i-=2;defeated=(defeated<<1)|((r[0x1f88+i]&64)!=0);}
  return (defeated&cpu_read8(cpu,0x86,0x88cd+stage))!=0;
}
unsigned MmxQolDamage(const uint8_t *r,unsigned enemy,unsigned table,
                      unsigned original,bool native_hadouken) {
  (void)r;(void)enemy;
  if(!native_hadouken)return original;
  if((table==0xef9f && (features&MMX_QOL_HADOUKEN_IFRAMES)) ||
      (table==0xf19d && (features&MMX_QOL_HADOUKEN_SIGMA)))return 32;
  return original;
}
void MmxQolHadoukenOpcode(CpuState *cpu,uint8_t *r,uint32_t pc,bool native_hadouken) {
  pc&=0x7fffff;
  if(pc==0x049e48) {
    unsigned original=cpu->A&255;
    unsigned value=MmxQolDamage(r,cpu->D,(0xef37+cpu->Y)&65535,original,native_hadouken);
    if(value!=original)accumulator8(cpu,value);
  } else if(pc==0x049e9d && native_hadouken && (features&MMX_QOL_HADOUKEN_IFRAMES) &&
      cpu->D==(unsigned)(r[0x1f0e]|r[0x1f0f]<<8) && !(r[cpu->D+0x27]&127)) {
    /* The native lethal store has already run. Release only the actual boss
     * owner's hurt/HP-restore/afterimage waits; its own AI completes defeat. */
    r[cpu->D+0x35]=r[cpu->D+0x38]=r[cpu->D+0x3d]=0;
  }
}
uint32_t MmxQolOpcode(CpuState *cpu,uint8_t *r,uint32_t pc,
                      bool is_x,bool buster,bool charge_ready) {
  uint32_t bank=pc&0xff0000;
  pc&=0x7fffff;
  if(!features)return 0;
  if(is_x && cpu->D==0xba8) {
    if(pc==0x018777 && (features&MMX_QOL_WALL_JUMP)) {
      cpu->A=(r[0xbde]&128)?0x375:0x178;
      cpu->_flag_Z=0;cpu->_flag_N=0;cpu->P&=(uint8_t)~0x82;
      return bank|0x8789; /* native store, wall side and signed velocity */
    }
    if(buster && normal_shot_read(pc)) {
      accumulator8(cpu,MmxQolShotValue(r,cpu->A&255,false,charge_ready));
    }
    if(buster && (pc==0x01984c || pc==0x01985b || pc==0x01988c)) {
      accumulator8(cpu,MmxQolShotValue(r,cpu->A&255,true,charge_ready));
    }
  }
  if(pc==0x07ca98 && (features&MMX_QOL_FIRST_HADOUKEN) && !cpu->_flag_C) {
    /* Test the original prerequisites without replacing the actual visit count. */
    return bank|0xca9f;
  }
  if(pc==0x00c723 && (features&MMX_QOL_SUB_TANK) && r[0x1f9a] &&
      (r[0xbcf]&127)>=r[0x1f9a])return bank|0xc767;
  if((features&MMX_QOL_ESCAPE) && pc==0x00c93f) {
    /* Enter after the original eligibility entry/Boss Rush veto. Only the
     * availability result changes; confirmation and stage cleanup stay native. */
    accumulator8(cpu,64);
    cpu->X=(uint16_t)((r[0x1f7a]*2)&255); /* native positive result clobbers X */
    cpu->_flag_C=0;cpu->P&=(uint8_t)~1; /* original positive CMP stage,#9 */
    return bank|0xc94e;
  }
  if(features&MMX_QOL_DIALOGUE) {
    unsigned offset=0,step=0;bool close=false;
    switch(pc) {
      case 0x03f5d1:offset=26;step=7;break;
      case 0x03f5dc:offset=27;step=4;break;
      case 0x03f605:offset=26;step=7;close=true;break;
      case 0x03f60e:offset=27;step=4;close=true;break;
      case 0x03f5e9:r[0x1f34]=4;break;
    }
    if(offset) {
      unsigned at=(cpu->D+offset)&65535,value=r[at];
      /* The original INC/DEC still executes and owns its flags. Clamp the
       * extra step so exact-equality completion tests cannot be skipped. */
      if(close)r[at]=(uint8_t)(value>=step?value-step+1:1);
      else {
        unsigned target=r[(cpu->D+offset-19)&65535];
        r[at]=(uint8_t)(value+step>=target?target-1:value+step-1);
      }
    }
  }
  if((features&MMX_QOL_SKIP_INTRO) && pc==0x0080b5 && !cpu->X &&
      r[0xd1]==2 && r[0xd2]==2 && r[0xd3]<20 && (r[0xa8]&16) && !reentry(cpu,r)) {
    /* Finish through the real final hold/fade and stage-load cleanup, even
     * when the setup routine is suspended inside its own frame wait. */
    r[0x1fa0]=0;r[0xd3]=20;r[0xd7]=1;r[0x40]=0;
    if(r[0x1f7a]>=13)r[0x1f7a]-=13;
    r[0x30]=3;r[0xa0]=0;
    cpu->S=0x13d;cpu->D=0;cpu->DB=0x86;cpu->P|=0x30;
    cpu_p_to_mirrors(cpu);cpu->X&=255;cpu->Y&=255;
    return bank|0x94d9;
  }
  return 0;
}
