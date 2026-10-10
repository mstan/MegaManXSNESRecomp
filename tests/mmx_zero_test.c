#include "mmx_zero.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x20000], before[0x20000], rom[0x180000], clean[0x180000];
static void asset(const char *path) {
  FILE *f = fopen(path, "wb"); assert(f);
  const uint8_t header[] = {'M','M','X','Z','E','R','O','6',128,0,128,0,64,0,64,0,117,0,35,0};
  uint8_t page[16384] = {0};
  assert(fwrite(header, sizeof(header), 1, f) == 1);
  assert(fwrite(page, 256, 1, f) == 1);
  uint8_t bounds[40];
  for (unsigned i = 0; i < 40; i += 4) { bounds[i] = 0; bounds[i+1] = 248; bounds[i+2] = 12; bounds[i+3] = 10; }
  assert(fwrite(bounds, sizeof(bounds), 1, f) == 1);
  assert(fwrite(page, 160, 1, f) == 1);
  uint8_t animation[MMX_ZERO_ANIMATION_BYTES] = {0};
  for (unsigned i = 0; i < 136; ++i) { animation[i * 2] = 0x10; animation[i * 2 + 1] = 1; }
  animation[272] = 2; animation[273] = 128; animation[275] = 253; animation[276] = 255;
  /* Measured original X3 group $4A firing records, including phase aliases. */
  const uint8_t bursts[][21] = {
    {2,0,82, 5,1,83, 1,66,84, 1,3,84, 7,4,85, 7,149,85},
    {2,0,86, 2,1,87, 5,2,88, 1,67,89, 1,4,89, 16,5,90, 255,134,90},
    {2,0,91, 5,1,92, 1,66,93, 1,3,93, 12,4,94, 255,149,94},
    {2,0,95, 3,1,96, 2,2,96, 1,67,97, 1,4,97, 12,5,98, 255,134,98}
  };
  const unsigned sequences[] = {0x30,0x36,0x43,0x49};
  for (unsigned i=0;i<4;++i) {
    unsigned base=300+i*24;
    memcpy(animation+base,bursts[i],21);
    for (unsigned j=0;j<(i%2?7:6);++j) {
      animation[(sequences[i]+j)*2]=(uint8_t)(base+j*3);
      animation[(sequences[i]+j)*2+1]=(uint8_t)((base+j*3)>>8);
    }
  }
  assert(fwrite(animation,sizeof(animation),1,f) == 1);
  uint8_t muzzle[MMX_ZERO_MUZZLE_BYTES] = {0};
  muzzle[0] = 2; muzzle[122] = 255; muzzle[123] = 232;
  assert(fwrite(muzzle,sizeof(muzzle),1,f) == 1);
  for (unsigned i = 0; i < MMX_ZERO_POSES; ++i) assert(fwrite(page, sizeof(page), 1, f) == 1);
  assert(!fclose(f));
}
static void player(void) {
  memset(ram, 0, sizeof(ram));
  ram[0xd1] = 2; ram[0xd2] = 4; ram[0xba9] = 2; ram[0xbcf] = 16;
  ram[0xbd3] = 4; ram[0xc11] = 64; ram[0xbb9] = 0x62;
  MmxZeroCancel(ram);
}
static int ceiling;
static bool ceiling_query(const uint8_t *r, int x, int y) { (void)r; (void)x; return y < ceiling; }
static void tick(unsigned held, unsigned pressed) {
  ram[0xbdf] = held; ram[0xbe3] = pressed; MmxZeroPlayerTick(ram);
}
static void modern_checks(void) {
  MmxZeroSetTerrainQuery(NULL);
  MmxZeroSetModern(true); MmxZeroResetState(); player();
  tick(64,64);
  MmxZeroState s = MmxZeroGetState();
  assert(s.slash == 1 && !s.charge && !s.burst && s.modern.enabled);
  assert(!(ram[0xbdf] & 64) && !(ram[0xbe3] & 64));
  for (unsigned i=0;i<6;++i) tick(64,0);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,3)==3);
  assert(MmxZeroHitbox(ram,0xe68,s.projectile,0xffb0)==0);
  for (unsigned i=0;i<9;++i) tick(64,0);
  assert(MmxZeroGetState().modern.hit_phase==1);
  assert(MmxZeroHitbox(ram,0xe68,s.projectile,0xffb0)==0xffb0);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,3)==3);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,3)==0);
  for (unsigned i=0;i<40;++i) tick(64,0);
  assert(!MmxZeroGetState().slash && !ram[s.projectile] && !MmxZeroGetState().charge);
  /* Fresh jump edges, a shared air-dash allowance, and exact restore. */
  player(); MmxZeroResetState(); ram[0xbd3]=0; ram[0xbaa]=8; ram[0xbab]=2;
  ram[0xbe3]=128; MmxZeroMovementTick(ram);
  assert(MmxZeroGetState().modern.jump_used && ram[0xbaa]==6);
  assert(ram[0xbc4]==0x53 && ram[0xbc5]==5 && ram[0xbc6]==0x40);
  ram[0xbc4]=ram[0xbc5]=0; ram[0xbe3]=128; MmxZeroMovementTick(ram);
  assert(!ram[0xbc4] && !ram[0xbc5]); /* No third jump. */
  ram[0xbe3]=0; ram[0xbe2]=ram[0xbde]=128; MmxZeroMovementTick(ram);
  s=MmxZeroGetState(); assert(s.modern.dash_used && s.modern.dash_ticks==18);
  MmxZeroPlayerMotion(ram,0xba8); assert(ram[0xbc2]==0x75 && ram[0xbc3]==3);
  MmxZeroCancel(ram); assert(MmxZeroGetState().modern.jump_used);
  MmxZeroResetState(); MmxZeroSetState(s);
  MmxZeroState restored=MmxZeroGetState();
  assert(!memcmp(&restored.modern,&s.modern,sizeof(s.modern)));
  ram[0xbde]=ram[0xbe2]=0; MmxZeroMovementTick(ram);
  assert(!MmxZeroGetState().modern.dash_ticks && ram[0xbaa]==8);
  ram[0xbe2]=ram[0xbde]=128; MmxZeroMovementTick(ram);
  assert(!MmxZeroGetState().modern.dash_ticks); /* No second dash. */
  ram[0xbd3]=4; MmxZeroMovementTick(ram);
  assert(!MmxZeroGetState().modern.jump_used && !MmxZeroGetState().modern.dash_used);
  s=MmxZeroGetState(); s.active_x=1; MmxZeroSetState(s);
  memcpy(before,ram,sizeof(ram)); MmxZeroMovementTick(ram); tick(ram[0xbdf],ram[0xbe3]);
  assert(!memcmp(before,ram,sizeof(ram))); /* X remains native. */
  MmxZeroSetModern(false); MmxZeroResetState(); player();
}
static unsigned ext_pre, ext_resets, ext_damage;
static bool ext_gate_open = true;
static uint8_t ext_saved;
static void ext_pre_player(uint8_t *r) { (void)r; ++ext_pre; }
static void ext_state_reset(uint8_t *r) { (void)r; ++ext_resets; ext_saved = 0; }
static unsigned ext_damage_hook(uint8_t *r, unsigned e, unsigned p, unsigned v) {
  (void)r; (void)e; (void)p; ++ext_damage; return v + 1;
}
static void ext_save(uint8_t out[]) { out[0] = 'T'; out[1] = ext_saved; }
static void ext_load(const uint8_t in[]) { if (in[0] == 'T') ext_saved = in[1]; }
static bool ext_gate(void) { return ext_gate_open; }
/* Co-op runs the player routine per seat; the extension follows Zero's seat. */
static void extension_checks(void) {
  static const MmxZeroExtension ext = {
    .pre_player = ext_pre_player, .damage = ext_damage_hook,
    .state_reset = ext_state_reset, .state_save = ext_save, .state_load = ext_load};
  uint8_t blob[MMX_ZERO_EXTENSION_STATE_BYTES];
  MmxZeroSetExtension(&ext);
  MmxZeroExtPrePlayer(ram); assert(ext_pre == 1);
  MmxZeroSetExtensionGate(ext_gate); ext_gate_open = false;
  MmxZeroExtPrePlayer(ram); assert(ext_pre == 1);
  assert(MmxZeroDamage(ram, 0xe68, 0x1228, 4) == 4 && !ext_damage);
  ext_gate_open = true;
  assert(MmxZeroDamage(ram, 0xe68, 0x1228, 4) == 5 && ext_damage == 1);
  ext_saved = 7; MmxZeroState z = MmxZeroGetState(); unsigned resets = ext_resets;
  MmxZeroSelectState(z); assert(ext_resets == resets && ext_saved == 7);
  MmxZeroExtSaveState(blob); assert(blob[0] == 'T' && blob[1] == 7 && !blob[2]);
  MmxZeroSetState(z); assert(ext_resets > resets && !ext_saved);
  MmxZeroExtLoadState(blob); assert(ext_saved == 7);
  memset(blob, 0, sizeof(blob)); MmxZeroExtLoadState(blob); assert(ext_saved == 7);
  MmxZeroSetExtensionGate(NULL); MmxZeroSetExtension(NULL);
  MmxZeroExtSaveState(blob); assert(!blob[0] && !blob[1]);
}
int main(void) {
  player(); memcpy(before, ram, sizeof(ram));
  tick(0,0); assert(!memcmp(before, ram, sizeof(ram)));
  assert(MmxZeroUpgradeBits(0x81971c, 0) == 0);
  asset("zero-test.bin"); assert(MmxZeroLoad("zero-test.bin"));
  assert(MmxZeroUpgradeBits(0x81971c, 2) == 10);
  assert(MmxZeroUpgradeBits(0x8197da, 0) == 0); /* Special charge stays upgrade-gated. */
  assert(MmxZeroMuzzle(ram,0x1228,0,0,16) == 24);
  assert(MmxZeroMuzzle(ram,0x1228,0,1,253) == 247);
  assert(MmxZeroMuzzle(ram,0xe68,0,0,16) == 16); /* NPCs are outside the player pool. */
  ram[0xbbf] = 100; /* A delayed shot uses its original firing sequence. */
  assert(MmxZeroMuzzle(ram,0x1228,0,0,16) == 24);
  assert(MmxZeroMuzzle(ram,0x1228,255,0,16) == 16);
  ram[0x1232] = 0x0b; ram[0x1239] = 64;
  assert(MmxZeroWeaponOrigin(ram,0x1228,0,144) == 150);
  ram[0x1239] = 0;
  assert(MmxZeroWeaponOrigin(ram,0x1228,0,112) == 106);
  ram[0x1232] = 0x0d;
  assert(MmxZeroWeaponOrigin(ram,0x1228,1,111) == 103);
  ram[0x1232] = 0x12;
  assert(MmxZeroWeaponOrigin(ram,0x1228,1,111) == 105);
  assert(MmxZeroWeaponOrigin(ram,0xe68,1,111) == 111);
  ram[0x1232] = 0;
  const uint8_t normal[] = {0,255,6,14,0,0,255,7,17,8};
  const uint8_t dash[] = {0,5,6,8,0,0,255,9,17,8};
  memcpy(rom+0x32552,normal,10); memcpy(rom+0x33b38,dash,10);
  memset(rom+0x37fb0,255,40); memcpy(clean,rom,sizeof(rom));
  MmxZeroSetCollisionRom(rom,sizeof(rom));
  assert(rom[0x32555] == 18 && rom[0x3255a] == 21);
  assert(rom[0x33b3b] == 11 && rom[0x37fb2] == 12);
  /* Dash terrain is X1's standing height; Zero's standing height is kept. */
  assert(rom[0x33b3e] == 0xff && rom[0x33b3f] == 6 && rom[0x33b40] == 17);
  /* Like the later games' slide, Zero keeps dashing until he can stand.
   * Measured Spark Mandrill passage: ceiling 16px above Zero's origin. */
  player(); MmxZeroSetTerrainQuery(ceiling_query);
  ram[0xbad] = 0x40; ram[0xbb0] = 0x80; ram[0xbaa] = 0x14; ram[0xbab] = 2;
  ram[0xbc8] = 0x38; ram[0xbc9] = 0xbb; ram[0xbc2] = 0x80; ram[0xbc3] = 0x03;
  ceiling = 0x80 - 15;
  assert(MmxZeroSlideHold(ram,0x818965)); /* No jump. */
  ram[0xbfa] = 0xff; assert(MmxZeroSlideHold(ram,0x818999) && !ram[0xbfa]);
  assert(MmxZeroSlideHold(ram,0x81898e) && ram[0xc11] == 64); /* Release. */
  ram[0xbdf] = 2; assert(MmxZeroSlideHold(ram,0x81898e)); /* Reverse turns. */
  assert(ram[0xc11] == 0 && ram[0xbc2] == 0x80 && ram[0xbc3] == 0xfc);
  /* A slide stopped by a wall turns around with the dash speed. */
  ram[0xbc2] = ram[0xbc3] = 0; ram[0xbdf] = 1;
  assert(MmxZeroSlideHold(ram,0x81898e) && ram[0xc11] == 64 &&
      ram[0xbc2] == 0x75 && ram[0xbc3] == 0x03);
  ram[0xbc2] = ram[0xbc3] = 0; ram[0xbdf] = 2;
  assert(MmxZeroSlideHold(ram,0x81898e) && ram[0xc11] == 0 &&
      ram[0xbc2] == 0x8b && ram[0xbc3] == 0xfc);
  /* A slide stopped by releasing the direction continues when it is held again. */
  ram[0xbc2] = ram[0xbc3] = 0; ram[0xbdf] = 2;
  assert(MmxZeroSlideHold(ram,0x81898e) && ram[0xc11] == 0 &&
      ram[0xbc2] == 0x8b && ram[0xbc3] == 0xfc);
  ram[0xbc2] = ram[0xbc3] = 0; ram[0xbdf] = 0;
  assert(MmxZeroSlideHold(ram,0x81898e) && !ram[0xbc2] && !ram[0xbc3]);
  ram[0xbc2] = 0x80; ram[0xbc3] = 0xfc;
  ram[0xbdf] = 0;
  ram[0xbd3] = 0; assert(!MmxZeroSlideHold(ram,0x81898e)); ram[0xbd3] = 4;
  ram[0xba9] = 4; assert(!MmxZeroSlideHold(ram,0x81898e)); ram[0xba9] = 2;
  ram[0x1f0c] = 1; assert(!MmxZeroSlideHold(ram,0x81898e)); ram[0x1f0c] = 0;
  ceiling = 0x80 - 27; assert(!MmxZeroSlideHold(ram,0x81898e)); /* Headroom. */
  ceiling = 0x80 - 15;
  /* Standing in a low passage (hurt recovery, landing) restarts the dash. */
  ram[0xbaa] = 0; ram[0xbab] = 2; ram[0xbc8] = 0x52; ram[0xbc9] = 0xa5;
  MmxZeroSlideTick(ram); assert(ram[0xbaa] == 0x14 && ram[0xbab] == 0);
  ram[0xbaa] = 0; ceiling = 0x80 - 27; MmxZeroSlideTick(ram); assert(ram[0xbaa] == 0);
  ram[0xbaa] = 0x16; ceiling = 0x80 - 15; MmxZeroSlideTick(ram); assert(ram[0xbaa] == 0x16);
  assert(!MmxZeroLoad("missing-zero-test.bin") && MmxZeroEnabled());
  FILE *f = fopen("zero-test-bad.bin","wb"); assert(f); fputs("MMXZERO3",f); fclose(f);
  assert(!MmxZeroLoad("zero-test-bad.bin") && MmxZeroEnabled());
  player();
  const unsigned charges[]={20,21,80,81,140,141,200,201};
  const unsigned tiers[]={0,4,4,6,6,8,8,10};
  for(unsigned c=0;c<8;++c) {
    player();
    for(unsigned i=0;i<charges[c];++i) tick(64,i==0?64:0);
    MmxZeroState z=MmxZeroGetState(); assert(MmxZeroChargeTier(&z)==tiers[c]);
    tick(0,0); z=MmxZeroGetState();
    assert(z.combo==(tiers[c]>=8) && z.saber_ready==(tiers[c]==10));
    if(tiers[c] && tiers[c]<8) assert(ram[0xc01]==(tiers[c]==4?2:8));
  }
  assert(MmxZeroGetState().burst == 1 && !ram[0x1228]);
  /* Highway restraint is native action $32 with sequence $49. It must
   * override a mirrored attack pose without changing rescue group $66. */
  player(); ram[0xbaa]=0x32; ram[0xbab]=2;
  MmxZeroState stunned={.anim_valid=1,.anim_pose=0x72};
  const uint8_t *base=MmxZeroMenuPose();
  assert(MmxZeroPose(ram,&stunned)==base+0x33*128*128);
  ram[0x1f7a]=8;
  assert(MmxZeroPose(ram,&stunned)==base+0x72*128*128);
  ram[0x1f7a]=0; ram[0xbab]=6; ram[0xbbe]=0x66; ram[0xbbf]=3;
  assert(MmxZeroPose(ram,&stunned)==base+0x49*128*128);
  ram[0xbbf]=4;
  assert(MmxZeroPose(ram,&stunned)==base+0x34*128*128);
  stunned.active_x=1; assert(!MmxZeroPose(ram,&stunned));
  /* Landing straight down ($0A) keeps a held charge and a pending combo. */
  player();
  for(unsigned i=0;i<150;++i) tick(64,i==0?64:0);
  ram[0xbaa]=0x0a;
  for(unsigned i=0;i<4;++i) tick(64,0);
  assert(MmxZeroGetState().charge==154 && ram[0xbff]==0x40);
  ram[0xbaa]=0; player();
  for(unsigned i=0;i<201;++i) tick(64,i==0?64:0);
  tick(0,0); ram[0xbaa]=0x0a;
  for(unsigned i=0;i<4;++i) tick(0,0);
  assert(MmxZeroGetState().combo==1 && MmxZeroGetState().saber_ready);
  ram[0xbaa]=0;
  /* The native engine started a looping charge voice before host release. */
  player();
  for(unsigned i=0;i<201;++i) tick(64,i==0?64:0);
  ram[0xc2f] |= 64;
  tick(0,0);
  assert(!(ram[0xc2f]&64) && ram[0xba3]==2 && ram[0xb72]==0x17);
  assert(ram[0x1f99] == 0); /* Dash/charge never grant equipment. */
  for(int i=2;i<=7;++i) tick(0,0);
  assert(!ram[0x1228]); tick(0,0); assert(ram[0x1228] && ram[0x1232]==3);
  assert(ram[0xba3]==4 && ram[0xb74]==2);
  ram[0xc25]=1; ram[0x1f0d]=0;
  for(int i=9;i<=18;++i) tick(0,0);
  tick(64,64);
  assert(MmxZeroGetState().combo == 2 && MmxZeroGetState().burst==2 && !ram[0x1268]);
  for(int i=2;i<=9;++i) tick(0,0);
  assert(!ram[0x1268]); tick(0,0);
  assert(ram[0x1268] == 1 && ram[0x1272] == 3 && ram[0xbdd] == 2);
  assert(ram[0xba3]==6 && ram[0xb76]==2);
  assert(ram[0xc25] == 1); /* Native initializer, not us, increments that counter. */
  for(int i=11;i<=29;++i) tick(0,0);
  tick(64,64); assert(!MmxZeroGetState().slash); /* Live beams block saber. */
  ram[0x1229]=8; ram[0x1269]=8; /* Disappearance effects still own slots. */
  tick(0,0); tick(64,64); assert(!MmxZeroGetState().slash);
  memset(ram+0x1228,0,128); ram[0xbdd]=ram[0xc25]=ram[0x1f0d]=0;
  tick(0,0);
  tick(64,64); MmxZeroState s = MmxZeroGetState();
  assert(s.slash == 1 && s.projectile == 0x1228 && !s.combo);
  assert(!ram[s.projectile+0x20] && !ram[s.projectile+0x21]); /* Windup cannot hit. */
  assert(!MmxZeroWeaponTick(ram,s.projectile,1));
  for(int i=0;i<6;++i) tick(0,0);
  assert(ram[s.projectile+0x20] == 0xb0 && ram[s.projectile+0x21] == 0xff);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,0) == 0);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,128) == 128);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,3) == 16);
  assert(MmxZeroHitbox(ram,0xe68,s.projectile,0xffb0) == 0);
  assert(MmxZeroHitbox(ram,0xea8,s.projectile,0xffb0) == 0xffb0);
  assert(MmxZeroDamage(ram,0xe68,s.projectile,3) == 0);
  assert(MmxZeroDamage(ram,0xea8,s.projectile,3) == 16);
  assert(ram[0xba3]==6); /* No repeated stop/release sounds through recovery. */
  s = MmxZeroGetState(); MmxZeroResetState(); MmxZeroSetState(s);
  assert(MmxZeroGetState().hit_slots == s.hit_slots && MmxZeroGetState().slash == s.slash);
  MmxZeroState invalid = s; invalid.air = 2; MmxZeroSetState(invalid);
  assert(!MmxZeroGetState().slash); MmxZeroSetState(s);
  /* Changing weapon cancels only our melee slot; native shots remain alive. */
  ram[0x1268]=1; ram[0x1272]=3; ram[0xbdd]=2;
  ram[0xbdb] = 2; tick(0,0);
  assert(!MmxZeroGetState().slash && !ram[s.projectile] && ram[0x1268] && ram[0xbdd] == 1);
  player(); s = (MmxZeroState){.combo=2,.saber_ready=1}; MmxZeroSetState(s);
  for(unsigned d=0x1228;d<0x1428;d+=64) ram[d] = 1;
  tick(64,64); assert(MmxZeroGetState().combo == 2 && !MmxZeroGetState().slash);
  ram[0xbaa] = 0x0e; tick(0,0); assert(!MmxZeroGetState().combo);
  player(); tick(64,64); ram[0xc2f]|=64;
  /* The allocator can put stale charge effects anywhere in this pool. */
  ram[0xc98]=ram[0xd58]=1; ram[0xca2]=ram[0xd62]=1;
  ram[0xd78]=1; ram[0xd82]=2; /* An unrelated small effect must survive. */
  MmxZeroCancel(ram); MmxZeroCancel(ram);
  assert(ram[0xba3]==2 && ram[0xb72]==0x17 && !(ram[0xc2f]&64));
  assert(!ram[0xc98] && !ram[0xd58] && ram[0xd78]);
  ram[0xc2f]|=64; MmxZeroCancel(ram);
  assert(ram[0xba3]==2 && (ram[0xc2f]&64)); /* Native special charge is untouched. */
  /* Starting as X: menus and stage start show X until the first exchange. */
  MmxZeroSetStartCharacter(true); MmxZeroResetState();
  assert(MmxZeroGetState().active_x == 1 && !MmxZeroActive() && MmxZeroEnabled());
  MmxZeroState start = MmxZeroGetState(); start.active_x = 0; MmxZeroSetState(start);
  assert(MmxZeroActive()); /* A saved character wins over the start choice. */
  MmxZeroSetStartCharacter(false); MmxZeroResetState(); assert(MmxZeroActive());
  modern_checks();
  MmxZeroDisable(); MmxZeroSetCollisionRom(rom,sizeof(rom));
  assert(!memcmp(rom,clean,sizeof(rom)));
  extension_checks();
  remove("zero-test.bin"); remove("zero-test-bad.bin");
  puts("Zero: asset validation, collision restoration, combo, damage, cancellation, state and extension-seat tests passed");
  return 0;
}
