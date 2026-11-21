/* Existing ROM harness: use generated PCM so no pack or speaker is required. */
#include "snes/msu1.h"
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
extern int mmx_msu1_music_command(uint8_t command);

static void msu_state_select(const char *rom, bool enabled) {
  char root[4096];
#ifdef _WIN32
  check(_getcwd(root,sizeof(root))!=NULL,"MSU test profile resolves");
#else
  check(getcwd(root,sizeof(root))!=NULL,"MSU test profile resolves");
#endif
  for(char *p=root;*p;++p) if(*p==92) *p='/';
  FILE *f=fopen(SNES_MOD_CATALOG_ROOT "/state.toml","wb");
  check(f!=NULL,"MSU test state opens");
  fprintf(f,"format_version = 1\n[[package]]\nid = \"megaman-x.enhancement.msu1\"\nversion = \"1.0.0\"\n"
      "[[feature]]\npackage_id = \"megaman-x.enhancement.msu1\"\nid = \"msu1\"\nenabled = %s\n"
      "[[resource]]\npackage_id = \"megaman-x.enhancement.msu1\"\nfeature_id = \"msu1\"\nid = \"pcm_pack\"\npath = \"%s/audio%s\"\n",
      enabled?"true":"false",root,getenv("MMX_MSU_FILE_SELECTION_TEST")?"/test.msu":"");
  check(!fclose(f),"MSU test state writes");
  check(snes_mod_runtime_initialize_c(SNES_MOD_CATALOG_ROOT,"megaman-x-us",
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),
      "MSU test catalog initializes");
  check(snes_mod_runtime_commit_c(rom),snes_mod_runtime_last_error_c());
}
static void msu_state_setup(const char *rom) {
  MkDir("audio");
  for(unsigned track=7;track<=8;++track) {
    char path[64];snprintf(path,sizeof(path),"audio/test-%u.pcm",track);
    FILE *f=fopen(path,"wb");check(f!=NULL,"synthetic PCM opens");
    const uint8_t header[]={'M','S','U','1',0,0,0,0};
    check(fwrite(header,1,sizeof(header),f)==sizeof(header),"synthetic PCM header");
    for(unsigned i=0;i<4096;++i) {
      int16_t pair[2]={(int16_t)(1000+(track-7)*4000+i),
                      (int16_t)(-1000-(int)(track-7)*4000-(int)i)};
      if(fwrite(pair,sizeof(pair),1,f)!=1) check(0,"synthetic PCM frame");
    }
    check(!fclose(f),"synthetic PCM closes");
  }
  if(getenv("MMX_MSU_FILE_SELECTION_TEST")) {
    FILE *f=fopen("audio/test.msu","wb");check(f!=NULL,"selected MSU file opens");
    check(fputc(0x58,f)!=EOF && !fclose(f),"selected MSU data writes");
    for(unsigned track=7;track<=9;++track) {
      char path[64];snprintf(path,sizeof(path),"audio/other-game-%u.pcm",track);
      f=fopen(path,"wb");check(f!=NULL,"unrelated PCM opens");
      const uint8_t header[]={'M','S','U','1',0,0,0,0};
      check(fwrite(header,1,sizeof(header),f)==sizeof(header),"unrelated PCM header");
      for(unsigned i=0;i<64;++i) {
        const int16_t pair[2]={9000,-9000};
        check(fwrite(pair,sizeof(pair),1,f)==1,"unrelated PCM sample");
      }
      check(!fclose(f),"unrelated PCM closes");
    }
  }
  msu_state_select(rom,true);
}
static void msu_state_mix(int16_t *samples,unsigned frames) {
  memset(samples,0,(size_t)frames*2*sizeof(*samples));
  RtlApuLock();msu1_mix(samples,(int)frames);RtlApuUnlock();
}
static void msu_state_checks(const char *rom, uint8 *start,uint8 *expected,
                             uint8 *actual,size_t cap) {
  msu1_init();snes_mod_runtime_activate_plugins_c();
  hitbox_boot();zero_replay(30);
  check(g_mmx_custom_renderer && g_mmx_custom_view.width>256,"MSU scenario uses real Fit field");
  check(MmxRendererSaveCapture("msu-field.mmxc"),"MSU field capture saves");
  check(mmx_msu1_music_command(0x17),"select saved scene music through title plugin");
  int16_t first[64],second[64];msu_state_mix(first,23);
  if(getenv("MMX_MSU_FILE_SELECTION_TEST")) {
    /* The title intentionally mixes at its native 0x50 music gain. */
    check(first[0]==1000*0x50/255 && first[1]==-1000*0x50/255,
          "selected file owns music despite larger unrelated pack");
    check(msu1_read(0x2001)==0x58,"selected file owns MSU data");
  }
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n!=0,"current-build MSU state saves");
  check(RtlSaveSnapshot("msu-current.sav"),"current-build MSU file saves");
  msu_state_mix(first,32);
  check(mmx_msu1_music_command(0x18),"other scene selects different music");
  check(RtlLoadSnapshotFromMemory(start,n),"current-build MSU memory state loads");
  msu_state_mix(second,32);
  same(first,sizeof(first),second,sizeof(second),"native state restores exact MSU track and cursor");
  check(mmx_msu1_music_command(0x18),"other music before file restore");
  check(RtlLoadSnapshot("msu-current.sav"),"current-build MSU file loads");
  msu_state_mix(second,32);
  same(first,sizeof(first),second,sizeof(second),"file restore uses the same MSU timeline");

  /* A local off preference remains off during load; activation is deferred. */
  msu_state_select(rom,false);msu1_init();snes_mod_runtime_activate_plugins_c();
  check(RtlLoadSnapshotFromMemory(start,n),"load preserves disabled local MSU choice");
  check(!mmx_msu1_music_command(0x00),"disabled title plugin remains inert");
  msu_state_select(rom,true);msu1_init();snes_mod_runtime_activate_plugins_c();
  msu_state_mix(second,32);
  same(first,sizeof(first),second,sizeof(second),"enabling MSU uses the restored timeline");

  /* PCM delivery is local: canonical bytes and rollback loads ignore it. */
  size_t cn=RtlRollbackSaveToMemory(expected,cap);check(cn!=0,"canonical state saves");
  check(mmx_msu1_music_command(0x18),"local music changes independently");
  size_t an=RtlRollbackSaveToMemory(actual,cap);
  same(expected,cn,actual,an,"local PCM excluded from canonical state");
  uint8_t before[MSU1_PLAYBACK_STATE_SIZE],after[MSU1_PLAYBACK_STATE_SIZE];
  msu1_capture_playback(before);
  check(RtlRollbackLoadFromMemory(expected,cn),"canonical rollback restores guest");
  msu1_capture_playback(after);
  same(before,sizeof(before),after,sizeof(after),"rollback retains current local PCM delivery");
  n=RtlSaveSnapshotToMemory(start,cap);zero_replay(20);
  cn=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"current-build field continuation restores");
  zero_replay(20);an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,cn,actual,an,"20-frame current-build continuation remains exact");
  puts("MMX MSU STATE CHECKS PASSED");
}
