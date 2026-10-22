static void vs_movement_checks(unsigned boss,uint8_t *snapshot,size_t size) {
  check(RtlLoadSnapshotFromMemory(snapshot,size),"movement fixture restores");
  MmxBossRushState rush=MmxBossRushGetState();unsigned d=rush.bosses[0].object;
  int x0=eagle_word(g_ram+d+5),y0=eagle_word(g_ram+d+8),min_y=y0,max_dx=0;
  if(boss==4) {
    frame(SNES_PAD_A|SNES_PAD_RIGHT);
    for(unsigned t=0;t<60;++t)frame(0);
    int expected=x0+96;if(expected>rush.camera_x+336)expected=rush.camera_x+336;
    fprintf(stderr,"VS teleport x=%d expected=%d\n",eagle_word(g_ram+d+5),expected);
    check(abs((int)eagle_word(g_ram+d+5)-expected)<=1,"Kuwanger teleports one fixed step and remains there");
  } else if(boss==6) {
    for(unsigned t=0;t<20;++t)frame(SNES_PAD_B*(t==0)|SNES_PAD_UP|SNES_PAD_RIGHT);
    check((int)eagle_word(g_ram+d+5)>x0+8 && (int)eagle_word(g_ram+d+8)<y0-8,"Eagle steers horizontally and vertically in flight");
    frame(0);frame(SNES_PAD_B);zero_replay(80);
    check(!MmxVsGetState().fighters[0].flying && eagle_word(g_ram+d+8)==MmxVsGetState().fighters[0].ground_y,
        "Eagle lands after flight is released");
  } else {
    frame(SNES_PAD_B|SNES_PAD_RIGHT);
    for(unsigned t=0;t<150;++t) {
      frame(0);int y=eagle_word(g_ram+d+8),dx=abs((int)eagle_word(g_ram+d+5)-x0);
      if(y<min_y)min_y=y;if(dx>max_dx)max_dx=dx;
    }
    fprintf(stderr,"VS jump boss=%u height=%d horizontal=%d\n",boss,y0-min_y,max_dx);
    check(min_y<y0-8 && max_dx>=8,"native directional jump moves the boss without walking");
  }
  if(boss==1 || boss==5) {
    check(RtlLoadSnapshotFromMemory(snapshot,size),"ceiling fixture restores");
    frame(boss==1?SNES_PAD_A:SNES_PAD_UP|SNES_PAD_B);
    bool attached=false;
    for(unsigned t=0;t<180;++t) {frame(0);attached|=MmxVsGetState().fighters[0].attached!=0;if(attached)break;}
    fprintf(stderr,"VS ceiling boss=%u attached=%u y=%u\n",boss,attached,eagle_word(g_ram+d+8));
    check(attached,"ceiling action reaches and attaches to the neutral arena ceiling");
    x0=eagle_word(g_ram+d+5);for(unsigned t=0;t<16;++t)frame(SNES_PAD_RIGHT);
    check((int)eagle_word(g_ram+d+5)>x0+8,"attached boss traverses the ceiling under directional control");
    frame(0);frame(boss==1?SNES_PAD_A:SNES_PAD_B);frame(0);
    check(!MmxVsGetState().fighters[0].attached,"ceiling attachment releases on its action button");
  }
  if(boss==7) {
    int heights[2]={0,0};bool quake[2]={false,false};
    for(unsigned held=0;held<2;++held) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Mammoth variable-jump fixture restores");
      y0=eagle_word(g_ram+d+8);min_y=y0;
      for(unsigned t=0;t<180;++t) {
        frame(t<(held?16:1)?SNES_PAD_B:0);int y=eagle_word(g_ram+d+8);if(y<min_y)min_y=y;
        quake[held]|=MmxVsGetState().shake_ticks!=0;
      }
      heights[held]=y0-min_y;
    }
    fprintf(stderr,"VS Mammoth small=%d large=%d quake=%u/%u\n",heights[0],heights[1],quake[0],quake[1]);
    check(heights[1]>heights[0]+8 && quake[1],"holding Mammoth jump increases height and creates landing quake");
  }
}
