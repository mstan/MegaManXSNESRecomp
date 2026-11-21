/* The title plugin and real PCM device, with no speaker/device output. */
#define _POSIX_C_SOURCE 200809L
#include "mod_runtime.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/msu1.h"
#include "../src/mmx_msu1.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static SNESModActivationCallback activate, reset;
static SNESModFrameCallback tick;
static SNESModApuWriteCallback apu;
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; activate=cb; return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) {reset=cb;return 1;}
int snes_mod_register_frame_callback(SNESModFrameCallback cb) {tick=cb;return 1;}
int snes_mod_register_apu_write_callback(SNESModApuWriteCallback cb) {apu=cb;return 1;}
void interp_bridge_set_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook) {(void)pc;(void)hook;}
void interp_bridge_pre_opcode_redirect(uint32_t pc) {(void)pc;abort();}
uint8 cpu_read8(CpuState *cpu,uint8 bank,uint16 addr) {(void)cpu;(void)bank;(void)addr;abort();}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
static double output_rate=44100;
double RtlAudioOutputRate(void) {return output_rate;}
static bool rollback_save,rollback_load;
bool RtlIsRollbackSave(void) {return rollback_save;}
bool RtlIsRollbackLoad(void) {return rollback_load;}
extern int mmx_msu1_music_command(uint8_t command);

static void pack(unsigned track, int base) {
  char path[64];snprintf(path,sizeof(path),"msu-test-%u.pcm",track);
  FILE *f=fopen(path,"wb");assert(f);
  const uint8_t header[]={ 'M','S','U','1',0,0,0,0 };
  assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
  for(unsigned i=0;i<4096;++i) {
    int16_t pair[2]={(int16_t)(base+i),(int16_t)(-base-(int)i)};
    assert(fwrite(pair,sizeof(pair),1,f)==1);
  }
  assert(!fclose(f));
}
static int sample(void) {
  int16_t out[2]={0};msu1_mix(out,1);assert(out[0]==-out[1]);return out[0];
}
static void configure_msu(bool enabled) {
#ifdef _WIN32
  _putenv_s("SNESRECOMP_MSU1",enabled?"msu-test":"");
#else
  setenv("SNESRECOMP_MSU1",enabled?"msu-test":"",1);
#endif
  msu1_init();
}
static void playback_state_checks(void) {
  output_rate=48000;
  assert(mmx_msu1_music_command(0x17));
  for(unsigned i=0;i<23;++i) (void)sample();
  MmxMsu1State saved=MmxMsu1GetState();
  assert(saved.valid && MmxMsu1ValidState(&saved));
  int expected[32];
  for(unsigned i=0;i<32;++i) expected[i]=sample();
  assert(mmx_msu1_music_command(0x18));
  MmxMsu1State other=MmxMsu1GetState();
  assert(other.playback[8]==8);
  MmxMsu1StateLoaded(&saved);
  for(unsigned i=0;i<32;++i) assert(sample()==expected[i]);

  /* Save a live fade, then resume its volume progression and PCM cursor. */
  assert(!apu(0x2140,0xf6));
  for(unsigned i=0;i<7;++i) tick();
  saved=MmxMsu1GetState();
  for(unsigned i=0;i<9;++i) tick();
  for(unsigned i=0;i<32;++i) expected[i]=sample();
  assert(mmx_msu1_music_command(0x18));
  MmxMsu1StateLoaded(&saved);
  for(unsigned i=0;i<9;++i) tick();
  for(unsigned i=0;i<32;++i) assert(sample()==expected[i]);

  /* Canonical capture and rollback cannot rewind this host audio timeline. */
  rollback_save=true;
  MmxMsu1State canonical=MmxMsu1GetState(),empty={0};
  assert(!memcmp(&canonical,&empty,sizeof(empty)));
  rollback_save=false;
  assert(mmx_msu1_music_command(0x18));
  uint8_t before[MSU1_PLAYBACK_STATE_SIZE],after[MSU1_PLAYBACK_STATE_SIZE];
  msu1_capture_playback(before);
  rollback_load=true;MmxMsu1StateLoaded(&saved);rollback_load=false;
  msu1_capture_playback(after);assert(!memcmp(before,after,sizeof(before)));

  /* Local preference stays off while loading; activation uses the bookmark. */
  reset();configure_msu(false);
  MmxMsu1StateLoaded(&saved);
  assert(!msu1_enabled());
  MmxMsu1State pending=MmxMsu1GetState();
  assert(!memcmp(&pending,&saved,sizeof(saved)));
  configure_msu(true);activate();
  MmxMsu1State restored=MmxMsu1GetState();
  assert(!memcmp(&restored,&saved,sizeof(saved)));
  MmxMsu1State bad=saved;bad.fade_state=3;
  assert(!MmxMsu1ValidState(&bad));
  msu1_capture_playback(before);MmxMsu1StateLoaded(&bad);
  msu1_capture_playback(after);assert(!memcmp(before,after,sizeof(before)));
  bad=saved;bad.playback[4]=99;assert(!MmxMsu1ValidState(&bad));
  bad=saved;bad.reserved=1;assert(!MmxMsu1ValidState(&bad));
}

int main(void) {
  assert(activate && reset);
  pack(7,1000);pack(8,5000);
  configure_msu(true);reset();
  assert(!mmx_msu1_music_command(0x17)); /* Disabled plugin stays stock. */
  activate();assert(tick && apu);
  assert(mmx_msu1_music_command(0x17));
  int before=0;for(unsigned i=0;i<64;++i)before=sample();assert(before>0);
  assert(!apu(0x2140,0xf6));
  for(unsigned i=0;i<100;++i)tick();
  assert(!(msu1_read(0x2000)&0x10) && sample()==0);
  assert(!apu(0x2140,0xf5));
  for(unsigned i=0;i<100;++i)tick();
  assert(msu1_read(0x2000)&0x10);
  assert(sample()>=before); /* Resume the selected PCM, retaining its cursor. */
  assert(!apu(0x2140,0xff));assert(sample()>0);
  assert(!apu(0x2140,0xfe));assert(sample()>=before);
  playback_state_checks();
  assert(!mmx_msu1_music_command(0x19)); /* Missing PCM uses SPC. */
  assert(!apu(0x2140,0xf5));assert(sample()==0);
  reset();assert(!mmx_msu1_music_command(0x17));
  assert(!remove("msu-test-7.pcm"));
  assert(!remove("msu-test-8.pcm"));
  puts("MSU capsule, state/cursor/fade, deferred enable, rollback isolation and fallback passed");
  return 0;
}
