#include "mmx_qol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t ram[0x20000],rom[32];
uint8 cpu_read8(CpuState *cpu,uint8 bank,uint16 addr) {
  (void)cpu;assert(bank==0x86);assert(addr>=0x88c0 && addr<0x88e0);
  return rom[addr-0x88c0];
}
static CpuState cpu;
static void fresh(void) {
  memset(ram,0,sizeof(ram));memset(&cpu,0,sizeof(cpu));memset(rom,0,sizeof(rom));
  cpu.D=0xba8;cpu.P=0x30;cpu_p_to_mirrors(&cpu);MmxQolReset();
}
static uint32_t at(unsigned pc) {return MmxQolOpcode(&cpu,ram,pc,true,true,ram[0xc00]==4);}
static void fire(unsigned mods,bool held,bool press,bool ready,bool expected_held,bool expected_press) {
  MmxQolReset();MmxQolEnable(mods);
  MmxQolFireIntent(ram,ready,&held,&press);
  assert(held==expected_held && press==expected_press);
}
int main(void) {
  fresh();ram[0xbfc]=2;ram[0xbf9]=9;ram[0xbfb]=1;
  assert(!at(0x81971c) && ram[0xbfc]==2);
  MmxQolEnable(MMX_QOL_NO_DOUBLE_TAP);
  assert(MmxQolUpgradeRead(ram,0x81971f,8,true)==0 && !ram[0xbfc] && !ram[0xbf9] && !ram[0xbfb]);
  assert(!at(0x819793)); /* mapped Dash still takes its native path */
  fresh();ram[0xbcf]=0x90;ram[0x1f9a]=16;ram[0x1f83]=0x8f;
  assert(!at(0x80c723));MmxQolEnable(MMX_QOL_SUB_TANK);
  assert(at(0x80c723)==0x80c767 && ram[0x1f83]==0x8f);
  ram[0xbcf]=15;assert(!at(0x80c723));
  fire(0,false,false,false,false,false);
  fire(MMX_QOL_AUTO_CHARGE,false,false,false,true,false);
  fire(MMX_QOL_AUTO_CHARGE,true,true,true,false,false);
  fire(MMX_QOL_AUTO_CHARGE,true,true,false,false,true);
  fire(MMX_QOL_RAPID_FIRE,true,false,true,false,true);
  fire(MMX_QOL_RAPID_FIRE,false,false,true,false,false);
  fire(MMX_QOL_AUTO_CHARGE|MMX_QOL_RAPID_FIRE,true,true,true,false,false);
  fire(MMX_QOL_AUTO_CHARGE|MMX_QOL_RAPID_FIRE,true,false,false,false,true);
  fresh();MmxQolEnable(MMX_QOL_AUTO_CHARGE|MMX_QOL_RAPID_FIRE);
  ram[0xbdf]=64;ram[0xbe3]=64;ram[0xc00]=4;cpu.A=0xa5c0;
  assert(!at(0x818332) && cpu.A==0xa580 && !cpu._flag_Z && cpu._flag_N);
  assert(ram[0xbdf]==64 && ram[0xbe3]==64); /* raw logical input unchanged */
  cpu.A=0xa540;assert(!at(0x81988c) && cpu.A==0xa500 && cpu._flag_Z && !cpu._flag_N);
  cpu.A=0xa540;assert(!MmxQolOpcode(&cpu,ram,0x81988c,false,true,true) && cpu.A==0xa540);
  cpu.A=0xa540;assert(!MmxQolOpcode(&cpu,ram,0x81988c,true,false,true) && cpu.A==0xa540);
  assert(!at(0x81a0d8)); /* Hadouken recognizer reads actual pressed Shot */
  fresh();MmxQolEnable(MMX_QOL_DIALOGUE);cpu.D=0x1928;
  for(unsigned target=1;target<255;++target)for(unsigned v=0;v<target;++v) {
    ram[0x192f]=(uint8_t)target;ram[0x1942]=(uint8_t)v;
    at(0x83f5d1);assert((unsigned)ram[0x1942]+1==(v+7>=target?target:v+7));
  }
  for(unsigned v=1;v<256;++v) {
    ram[0x1942]=(uint8_t)v;at(0x83f605);
    assert((unsigned)ram[0x1942]-1==(v>7?v-7:0));
  }
  ram[0x1f34]=2;at(0x83f5e9);assert(ram[0x1f34]==4);
  fresh();cpu.A=0;assert(!at(0x80c93f) && !cpu.A);
  MmxQolEnable(MMX_QOL_ESCAPE);assert(at(0x80c93f)==0x80c94e && cpu.A==64 && !cpu._flag_Z);
  assert(!at(0x80c93c)); /* Boss Rush owner precedes the result override */
  fresh();MmxQolEnable(MMX_QOL_SKIP_INTRO);
  ram[0xd1]=ram[0xd2]=2;ram[0xa8]=16;ram[0x1f7a]=1;rom[1]=1;rom[14]=1;
  ram[0x1fa0]=22;ram[0x40]=3;
  assert(at(0x8080b5)==0x8094d9 && ram[0xd3]==20 && ram[0xd7]==1);
  assert(!ram[0x40] && !ram[0x1fa0] && cpu.S==0x13d && cpu.DB==0x86 && !cpu.D);
  ram[0xd3]=0;ram[0x1f7a]=14;assert(at(0x8080b5)==0x8094d9 && ram[0x1f7a]==1);
  ram[0xd3]=0;ram[0x1f88]=64;assert(!at(0x8080b5)); /* beaten stage */
  ram[0x1f7a]=0;assert(!at(0x8080b5));ram[0x1f7a]=9;assert(!at(0x8080b5));
  ram[0x1f7a]=1;ram[0x1f88]=0;cpu.X=16;assert(!at(0x8080b5));
  fresh();MmxQolEnable(MMX_QOL_WALL_JUMP);
  cpu.m_flag=0;cpu.P&=(uint8_t)~0x20;
  assert(at(0x818777)==0x818789 && cpu.A==0x178 && !(cpu.P&0x82));
  ram[0xbde]=128;assert(at(0x818777)==0x818789 && cpu.A==0x375);
  assert(!ram[0x1f99]); /* native inventory is never granted */
  fresh();assert(!at(0x87ca98));MmxQolEnable(MMX_QOL_FIRST_HADOUKEN);
  ram[0x1f7e]=1;assert(at(0x87ca98)==0x87ca9f && ram[0x1f7e]==1);
  cpu._flag_C=1;
  assert(!at(0x87ca98));
  assert(!at(0x87ca9f)); /* remaining requirements and real capsule producer */
  fresh();assert(MmxQolDamage(ram,0xe68,0xef9f,0,true)==0);
  MmxQolEnable(MMX_QOL_HADOUKEN_IFRAMES);
  assert(MmxQolDamage(ram,0xe68,0xef9f,0,true)==32);
  assert(MmxQolDamage(ram,0xe68,0xef9f,0,false)==0);
  assert(MmxQolDamage(ram,0xe68,0xf19d,128,true)==128);
  MmxQolEnable(MMX_QOL_HADOUKEN_SIGMA);
  assert(MmxQolDamage(ram,0xe68,0xf19d,128,true)==32);
  assert(MmxQolDamage(ram,0xe68,0xf19e,128,true)==128);
  cpu.A=0xa580;cpu.Y=0xf19d-0xef37;
  MmxQolHadoukenOpcode(&cpu,ram,0x849e48,true);
  assert(cpu.A==0xa520 && !cpu._flag_N && !(cpu.P&128));
  cpu.D=0xe68;ram[0x1f0e]=0x68;ram[0x1f0f]=0x0e;
  ram[0xe9d]=8;ram[0xea0]=30;ram[0xea5]=8;ram[0xe8f]=0;
  MmxQolHadoukenOpcode(&cpu,ram,0x849e9d,false);assert(ram[0xe9d]==8);
  MmxQolHadoukenOpcode(&cpu,ram,0x849e9d,true);
  assert(!ram[0xe9d] && !ram[0xea0] && !ram[0xea5]);
  ram[0xe9d]=8;ram[0xe8f]=4;MmxQolHadoukenOpcode(&cpu,ram,0x849e9d,true);assert(ram[0xe9d]==8);
  puts("mmx_qol: eleven option contracts PASS (native execution still required)");
  return 0;
}
