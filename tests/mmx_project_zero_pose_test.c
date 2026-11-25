#include "mmx_project_zero.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  uint8_t ram[0x20000] = {0}, hud[MMX_ZERO_HUD_BYTES] = {0};
  MmxProjectZeroAssets *art = calloc(1, sizeof(*art));
  CHECK(art);
  for (unsigned pose = 0; pose < MMX_PROJECT_ZERO_BODY_POSES; ++pose)
    art->body[pose][0] = (uint8_t)pose;
  MmxZeroAdoptProjectAssets(art, hud);
  MmxZeroResetState();

  /* Armor changes the sprite group, not the running, jumping or firing pose.
   * Exercise the actual character compositor with both retail body groups. */
  for (unsigned group = 0; group <= 0x18; group += 0x18) {
    ram[0xbbe] = (uint8_t)group;
    for (unsigned pose = 0; pose < MMX_PROJECT_ZERO_BODY_POSES; ++pose) {
      ram[0xbbf] = (uint8_t)pose;
      CHECK(MmxZeroPose(ram, NULL) == art->body[pose]);
      ram[0xbbf] = (uint8_t)(pose | 0x80);
      CHECK(MmxZeroPose(ram, NULL) == art->body[pose]);
    }
    ram[0xbbf] = MMX_PROJECT_ZERO_BODY_POSES;
    CHECK(MmxZeroPose(ram, NULL) == art->body[0]);
  }
  ram[0xbbe] = 0x66;
  for (unsigned pose = 0; pose < MMX_PROJECT_ZERO_CAPTURE_POSES; ++pose) {
    ram[0xbbf] = (uint8_t)pose;
    CHECK(MmxZeroPose(ram, NULL) == art->capture[pose]);
  }
  ram[0xbbe] = 0x67; ram[0xbbf] = 5;
  CHECK(MmxZeroPose(ram, NULL) == art->body[0]);
  MmxZeroState x = MmxZeroGetState(); x.active_x = 1;
  CHECK(MmxZeroPose(ram, &x) == NULL);
  MmxZeroDisable();
  puts("Project Zero: armored movement poses, capture poses and invalid-group guards passed");
  return 0;
}
