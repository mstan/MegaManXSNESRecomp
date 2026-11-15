/* Private owner-review checkpoints. No progress/save files are imported. */
static void complete_review_save(const char *name) {
  char path[4096];
  snprintf(path,sizeof(path),"%s/%s.sav",getenv("MMX_COMPLETE_REVIEW_OUT"),name);
  check(RtlSaveSnapshot(path),name);
}
static void complete_review_text(unsigned id) {
  /* The dialogue task owns $01C0..$01FF. Keep its newly created stack. */
  uint8_t stack[256];memcpy(stack,g_ram+0x300,sizeof(stack));
  CpuState cpu=g_cpu;cpu.S=0x3ff;cpu.D=0;cpu.DB=0x80;cpu.PB=0x80;
  cpu.A=id;cpu.P=0x30;cpu.emulation=0;cpu_p_to_mirrors(&cpu);
  cpu_push_jsl_return_frame(&cpu);interp_bridge_set_master_deadline(0);
  check(interp_bridge_run(&cpu,0x80e9d8) && cpu.S==0x3ff,"native dialogue task starts");
  memcpy(g_ram+0x300,stack,sizeof(stack));
  for(unsigned i=0;i<180;++i)complete_key(0);
}
static void complete_review_stage(unsigned stage,unsigned checkpoint) {
  g_ram[0x1f7a]=stage;g_ram[0x1f81]=checkpoint;g_ram[0x1f82]=0;
  g_ram[0xd1]=2;g_ram[0xd2]=2;g_ram[0xd3]=g_ram[0xd4]=0;
  unsigned wait;
  for(wait=0;wait<1800;++wait) {
    complete_key(0);
    if(g_ram[0xd2]==4 && g_ram[0xd3]==4 && g_ram[0xba9]==2 && (g_ram[0xbd3]&4))break;
  }
  check(wait<1800,"review stage finishes its native load");
  if(checkpoint) {
    g_ram[0x1f81]=checkpoint;g_ram[0x1f82]=0;
    g_ram[0xd2]=4;g_ram[0xd3]=g_ram[0xd4]=0;
    for(unsigned i=0;i<300;++i)complete_key(0);
  }
  fprintf(stderr,"review stage=%u cp=%u game=%u/%u/%u xy=%u,%u camera=%u,%u\n",
      stage,checkpoint,g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],
      g_ram[0xbad]|g_ram[0xbae]<<8,g_ram[0xbb0]|g_ram[0xbb1]<<8,
      g_ram[0x1e4d]|g_ram[0x1e4e]<<8,g_ram[0x1e50]|g_ram[0x1e51]<<8);
}
static void complete_review_prepare(const char *rom_path) {
  check(snes_mod_runtime_initialize_c("catalog","megaman-x-us",
    "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32"),"review catalog");
  const RecompLauncherCModProvider *p=snes_mod_runtime_launcher_provider_c();
  check(p->feature_enable(p->ctx,"megaman-x.accessibility.hadouken","hadouken",1) &&
      p->commit(p->ctx,rom_path),"review enables Easy Hadouken");
  snes_mod_runtime_activate_plugins_c();RtlReset(0);hitbox_boot();
  for(unsigned i=0;i<180;++i)complete_key(0);
  size_t cap=2*1024*1024;uint8_t *start=malloc(cap);
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"review baseline");
  complete_review_save("highway");
  complete_review_text(0xc0);complete_review_save("dialogue");
  check(RtlLoadSnapshotFromMemory(start,n),"restore baseline");
  complete_review_text(0xcc);complete_review_save("light");
  check(RtlLoadSnapshotFromMemory(start,n),"restore baseline");
  complete_review_text(0xc3);complete_review_save("torpedo");
  check(RtlLoadSnapshotFromMemory(start,n),"restore baseline");
  complete_key(SNES_PAD_L|SNES_PAD_X|SNES_PAD_A);complete_review_save("practice");
  complete_select(0);complete_key(SNES_PAD_Y);
  for(unsigned i=0;i<20;++i)complete_key(0);
  complete_review_save("editor");
  check(RtlLoadSnapshotFromMemory(start,n),"restore baseline");
  complete_review_stage(7,0);complete_review_save("kuwanger");
  g_ram[0x1f99]=15;g_ram[0x1f8c]=0xdc;g_ram[0xbdb]=6;g_ram[0xbd8]|=8;
  for(unsigned i=0;i<180;++i)frame(SNES_PAD_Y);
  frame(0);frame(0);
  memset(g_ram+0xe68,0,0x3c0);
  unsigned hx=(g_ram[0xbad]|g_ram[0xbae]<<8)+90,hy=g_ram[0xbb0]|g_ram[0xbb1]<<8;
  g_ram[0xe68]=1;g_ram[0xe72]=1;
  g_ram[0xe6d]=hx;g_ram[0xe6e]=hx>>8;g_ram[0xe70]=hy;g_ram[0xe71]=hy>>8;
  for(unsigned i=0;i<3;++i)frame(0);
  complete_review_save("hoganmer");
  check(RtlLoadSnapshotFromMemory(start,n),"restore baseline");
  g_ram[0x1f99]=15;g_ram[0x1f9a]=32;g_ram[0xbcf]=32;g_ram[0x1f7e]|=128;
  for(unsigned i=0;i<8;++i)g_ram[0x1f88+2*i]=0xdc;
  complete_review_stage(3,2);complete_review_save("armadillo");
  complete_review_stage(12,0);complete_review_save("sigma");
  free(start);puts("COMPLETE REVIEW FIXTURES READY");
}
