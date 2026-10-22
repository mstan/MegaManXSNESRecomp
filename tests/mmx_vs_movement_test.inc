#include "mmx_boss_rush_audio.h"
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
    static const unsigned directions[]={SNES_PAD_RIGHT,SNES_PAD_DOWN,SNES_PAD_LEFT,SNES_PAD_UP};
    for(unsigned seat=0;seat<2;++seat)for(unsigned attacking=0;attacking<2;++attacking) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Eagle four-direction fixture restores");
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      check(!MmxVsGetState().fighters[seat].flying && MmxVsGetState().fighters[seat].grounded,
          "Eagle starts grounded on his own side");
      unsigned quiet=MmxBossRushAudioGetState().sequence;zero_replay(120);
      check(MmxBossRushAudioGetState().sequence==quiet && g_ram[object+0x38]==0,
          "grounded idle Eagle has neither wing-flap cues nor the flight animation");
      frame(SNES_PAD_B<<(seat*12));frame(0);
      check(MmxVsGetState().fighters[seat].flying,"Jump enables Eagle flight from the floor");
      for(unsigned t=0;t<40;++t)frame(SNES_PAD_UP<<(seat*12));
      check(MmxBossRushAudioGetState().sequence>quiet,"airborne Eagle retains his native wing-flap cues");
      for(unsigned dir=0;dir<4;++dir) {
        int bx=eagle_word(g_ram+object+5),by=eagle_word(g_ram+object+8);
        for(unsigned t=0;t<10;++t)frame((directions[dir]|(attacking && t==0?SNES_PAD_Y:0))<<(seat*12));
        int ax=eagle_word(g_ram+object+5),ay=eagle_word(g_ram+object+8);
        fprintf(stderr,"VS Eagle seat=%u attacking=%u dir=%u xy=%d,%d -> %d,%d ground=%d\n",seat,attacking,dir,bx,by,ax,ay,MmxVsGetState().fighters[seat].ground_y);
        check(dir==0?ax>=bx+16:dir==1?ay>=by+16:dir==2?ax<=bx-16:ay<=by-16,
            "both Eagles steer in all four directions, including downward during attacks");
      }
      frame(0);frame(SNES_PAD_B<<(seat*12));zero_replay(80);
      check(!MmxVsGetState().fighters[seat].flying && eagle_word(g_ram+object+8)==MmxVsGetState().fighters[seat].ground_y,
          "Eagle lands on the arena floor after flight is released");
      quiet=MmxBossRushAudioGetState().sequence;zero_replay(120);
      check(MmxBossRushAudioGetState().sequence==quiet && g_ram[object+0x38]==0,
          "landing stops further wing-flap cues on both seats");
    }
    for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Eagle full dive fixture restores");
      if(!seat)check(RtlSaveSnapshot("vs-eagle-ready.sav"),"private Eagle rollback fixture saves");
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      int min_y=32767,max_y=0,min_x=32767,max_x=0;bool dive=false,recovered=false,hit=false;
      for(unsigned t=0;t<520;++t) {
        frame((t==0?SNES_PAD_X:0)<<(seat*12));
        int x=eagle_word(g_ram+object+5),y=eagle_word(g_ram+object+8);
        if(y<min_y)min_y=y;if(y>max_y)max_y=y;if(x<min_x)min_x=x;if(x>max_x)max_x=x;
        dive|=g_ram[object+0x16]==0x8a;hit|=MmxVsGetState().fighters[seat^1].hp<32;
        if(dive && MmxVsGetState().fighters[seat].action==MMX_VS_IDLE) {recovered=true;break;}
        if(!seat && g_ram[object+0x16]==0x8a && y>rush.camera_y+96 && y<rush.camera_y+104)
          rush_visual_bmp("vs-eagle-native-dive.bmp");
      }
      fprintf(stderr,"VS Eagle dive seat=%u bounds=%d..%d,%d..%d dive=%u recovered=%u hit=%u\n",seat,min_x,max_x,min_y,max_y,dive,recovered,hit);
      check(min_y<rush.camera_y-32 && max_y>rush.camera_y+256 && max_x-min_x>160 && dive,
          "both Eagles leave through the ceiling and complete the native across-screen dive");
      check(hit && recovered && MmxVsGetState().fighters[seat].flying,
          "one Special hits the opponent and returns Eagle to controllable flight");
      int before=eagle_word(g_ram+object+5);
      unsigned direction=before>rush.camera_x+184?SNES_PAD_LEFT:SNES_PAD_RIGHT;
      for(unsigned t=0;t<8;++t)frame(direction<<(seat*12));
      check(abs((int)eagle_word(g_ram+object+5)-before)>=12,"Eagle responds to directions after the full dive");
    }
  } else {
    frame(SNES_PAD_B|SNES_PAD_RIGHT);
    for(unsigned t=0;t<150;++t) {
      frame(SNES_PAD_RIGHT);int y=eagle_word(g_ram+d+8),dx=abs((int)eagle_word(g_ram+d+5)-x0);
      if(y<min_y)min_y=y;if(dx>max_dx)max_dx=dx;
    }
    fprintf(stderr,"VS jump boss=%u height=%d horizontal=%d\n",boss,y0-min_y,max_dx);
    check(min_y<y0-8 && max_dx>=8,"native directional jump moves the boss without walking");
    for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"air steering fixture restores");
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      int ground=eagle_word(g_ram+object+8);
      frame(SNES_PAD_B<<(seat*12));
      for(unsigned t=0;t<80 && eagle_word(g_ram+object+8)>=ground-12;++t)frame(0);
      int air_y=eagle_word(g_ram+object+8),before=eagle_word(g_ram+object+5);
      for(unsigned t=0;t<4;++t)frame(SNES_PAD_RIGHT<<(seat*12));
      int right=eagle_word(g_ram+object+5);
      for(unsigned t=0;t<4;++t)frame(SNES_PAD_LEFT<<(seat*12));
      int left=eagle_word(g_ram+object+5);
      zero_replay(4);int stopped=eagle_word(g_ram+object+5);
      fprintf(stderr,"VS air steering boss=%u seat=%u y=%d x=%d/%d/%d/%d\n",boss,seat,air_y,before,right,left,stopped);
      check(air_y<ground-8 && right>before+2 && left<right-2 && abs(stopped-left)<=2,
          "both seats can start, reverse and stop horizontal motion after takeoff");
    }
  }
  if(boss==0) {
    for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Penguin slide fixture restores");
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      int before=eagle_word(g_ram+object+5),distance=0;
      for(unsigned t=0;t<60;++t) {
        frame((t<16?SNES_PAD_X:0)<<(seat*12));
        int dx=abs((int)eagle_word(g_ram+object+5)-before);if(dx>distance)distance=dx;
      }
      fprintf(stderr,"VS Penguin slide seat=%u distance=%d\n",seat,distance);
      check(distance>=24,"Special button produces Penguin's native stomach slide on both seats");
    }
  }
  if(boss==1) {
    for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"ceiling fixture restores");
      unsigned d=MmxBossRushGetState().bosses[seat].object;
      frame(SNES_PAD_A<<(seat*12));
      bool attached=false;
      for(unsigned t=0;t<180;++t) {frame(0);attached|=MmxVsGetState().fighters[seat].attached!=0;if(attached)break;}
      fprintf(stderr,"VS ceiling boss=%u attached=%u y=%u\n",boss,attached,eagle_word(g_ram+d+8));
      check(attached,"ceiling action reaches and attaches to the neutral arena ceiling");
      zero_replay(60);int hanging_x=eagle_word(g_ram+d+5),hanging_y=eagle_word(g_ram+d+8);bool still=true;
      for(unsigned t=0;t<90;++t) {frame(0);still&=(g_ram[d+0x17]&127)==0x12;}
      check(still,"stationary Mandrill holds the native hanging pose");
      check(eagle_word(g_ram+d+5)==hanging_x && eagle_word(g_ram+d+8)==hanging_y,"idle hanging does not drift");
      char image[80];snprintf(image,sizeof(image),"vs-mandrill-hang-idle-%u.bmp",seat);rush_visual_bmp(image);
      for(unsigned dir=0;dir<2;++dir) {
        int before=eagle_word(g_ram+d+5);
        for(unsigned t=0;t<12;++t)frame((dir?SNES_PAD_RIGHT:SNES_PAD_LEFT)<<(seat*12));
        int after=eagle_word(g_ram+d+5);
        check(dir?after>before+8:after<before-8,"attached boss traverses the ceiling in either direction");
        check(((g_ram[d+0x11]&64)!=0)==(dir!=0),"ceiling movement flips Mandrill's native sprite facing");
        snprintf(image,sizeof(image),"vs-mandrill-hang-%u-%u.bmp",seat,dir);rush_visual_bmp(image);
        zero_replay(30);check((g_ram[d+0x17]&127)==0x12,"releasing directions returns to the still hanging pose");
      }
      frame(SNES_PAD_A<<(seat*12));frame(0);
      check(!MmxVsGetState().fighters[seat].attached,"ceiling attachment releases on its action button");
    }
  }
  if(boss==5) {
    for(unsigned seat=0;seat<2;++seat) {
      int heights[2]={0,0};
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      for(unsigned held=0;held<2;++held) {
        check(RtlLoadSnapshotFromMemory(snapshot,size),"Chameleon jump-height fixture restores");
        int ground=eagle_word(g_ram+object+8),peak=ground;
        for(unsigned t=0;t<120;++t) {
          frame((t<(held?16:1)?SNES_PAD_B:0)<<(seat*12));
          int y=eagle_word(g_ram+object+8);if(y<peak)peak=y;
        }
        heights[held]=ground-peak;
      }
      fprintf(stderr,"VS Chameleon heights seat=%u tap=%d held=%d\n",seat,heights[0],heights[1]);
      check(heights[0]>=24 && heights[1]>heights[0]+12,"Chameleon tap/hold Jump produces distinct jump heights on both seats");
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Chameleon cling fixture restores");
      frame(SNES_PAD_B<<(seat*12));zero_replay(7);frame(SNES_PAD_A<<(seat*12));
      int cling_x=eagle_word(g_ram+object+5),cling_y=eagle_word(g_ram+object+8);
      for(unsigned t=0;t<90;++t)frame((SNES_PAD_A|SNES_PAD_RIGHT|(t==8?SNES_PAD_Y:0))<<(seat*12));
      check(MmxVsGetState().fighters[seat].attached && eagle_word(g_ram+object+5)==cling_x && eagle_word(g_ram+object+8)==cling_y,
          "holding Ability clings at the exact midair location while attacking");
      check(!g_ram[object+0x37] && !MmxBossRushVisualGetState().initialized[seat],"Chameleon remains visible while clinging");
      rush_visual_bmp(seat?"vs-chameleon-cling-p2.bmp":"vs-chameleon-cling-p1.bmp");
      frame(SNES_PAD_LEFT<<(seat*12));zero_replay(80);
      check(!MmxVsGetState().fighters[seat].attached && !MmxVsGetState().fighters[seat].flying &&
          eagle_word(g_ram+object+8)==MmxVsGetState().fighters[seat].ground_y,
          "releasing Ability drops Chameleon and returns him to the floor");
    }
  }
  if(boss==2) {
    for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Armadillo ricochet fixture restores");
      unsigned object=MmxBossRushGetState().bosses[seat].object;
      frame(SNES_PAD_X<<(seat*12));
      int previous=eagle_word(g_ram+object+5),speed=0,ricochets=0,direction=MmxVsGetState().fighters[seat].vx;
      for(unsigned t=0;t<180;++t) {
        frame(0);int x=eagle_word(g_ram+object+5),vx=MmxVsGetState().fighters[seat].vx;
        int delta=abs(x-previous);if(delta>speed)speed=delta;
        if((vx<0)!=(direction<0))++ricochets;
        check(x>=rush.camera_x+32 && x<=rush.camera_x+336,"rolling stays inside both walls");
        previous=x;direction=vx;
      }
      fprintf(stderr,"VS Armadillo roll seat=%u speed=%d ricochets=%d\n",seat,speed,ricochets);
      check(speed>=8 && ricochets>=3,"both Armadillos roll quickly and ricochet consistently");
      frame(SNES_PAD_X<<(seat*12));frame(0);
      check(MmxVsGetState().fighters[seat].action!=MMX_VS_SPECIAL && g_ram[object+0x16]==0x62,
          "Special again immediately cancels roll and restores the native standing body");
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Armadillo rolling vulnerability fixture restores");
      for(unsigned t=0;t<150 && MmxVsGetState().fighters[seat].hp==32;++t) {
        for(unsigned shot=0x1428;shot<0x1628;shot+=64)if(g_ram[shot] && MmxBossRushOwner(shot)==(int)(seat^1)) {
          memcpy(g_ram+shot+5,g_ram+object+5,2);memcpy(g_ram+shot+8,g_ram+object+8,2);
        }
        frame(((t==0?SNES_PAD_X:0)<<(seat*12))|((t<16?SNES_PAD_Y:0)<<((seat^1)*12)));
      }
      check(MmxVsGetState().fighters[seat].hp<32,"rolling armor does not block an actual opposing energy shot");
      check(RtlLoadSnapshotFromMemory(snapshot,size),"Armadillo roll contact fixture restores");
      frame(SNES_PAD_X<<(seat*12));unsigned target=MmxBossRushGetState().bosses[seat^1].object;
      for(unsigned t=0;t<60 && MmxVsGetState().fighters[seat^1].hp==32;++t) {
        memcpy(g_ram+target+5,g_ram+object+5,2);memcpy(g_ram+target+8,g_ram+object+8,2);frame(0);
      }
      check(MmxVsGetState().fighters[seat^1].hp<32,"the native ball body inflicts real roll contact damage on both seats");
    }
    check(RtlLoadSnapshotFromMemory(snapshot,size),"Armadillo guard fixture restores");
    check(RtlSaveSnapshot("vs-guard-ready.sav"),"private guard netplay fixture saves");
    for(unsigned t=0;t<360;++t)frame(SNES_PAD_A);
    check(MmxVsGetState().fighters[0].action==MMX_VS_ABILITY && (g_ram[d+0x37]&1),
        "Armadillo can sustain guard beyond the ordinary action timeout");
    for(unsigned t=0;t<150;++t) {
      for(unsigned shot=0x1428;shot<0x1628;shot+=64)if(g_ram[shot] && MmxBossRushOwner(shot)==1) {
        memcpy(g_ram+shot+5,g_ram+d+5,2);memcpy(g_ram+shot+8,g_ram+d+8,2);
      }
      frame(SNES_PAD_A|((t<16?SNES_PAD_Y:0)<<12));
    }
    check(MmxVsGetState().fighters[0].hp==32,"held guard blocks an actual opposing energy shot");
    zero_replay(24);
    check(!(g_ram[d+0x37]&1) && MmxVsGetState().fighters[0].action!=MMX_VS_ABILITY,
        "releasing a long guard clears its native immunity flag");
    for(unsigned t=0;t<150 && MmxVsGetState().fighters[0].hp==32;++t) {
      for(unsigned shot=0x1428;shot<0x1628;shot+=64)if(g_ram[shot] && MmxBossRushOwner(shot)==1) {
        memcpy(g_ram+shot+5,g_ram+d+5,2);memcpy(g_ram+shot+8,g_ram+d+8,2);
      }
      frame((t<16?SNES_PAD_Y:0)<<12);
    }
    check(MmxVsGetState().fighters[0].hp<32,"released guard takes damage from an actual opposing shot");
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
