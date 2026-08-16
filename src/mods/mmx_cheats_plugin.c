#include "mod_runtime.h"

#include <stdint.h>
#include <string.h>

extern uint8_t g_ram[0x20000];

#define MMX_CHEAT_START_10_LIVES "megaman-x.cheat.start-10-lives"
#define MMX_CHEAT_START_7_LIVES "megaman-x.cheat.start-7-lives"
#define MMX_CHEAT_START_5_LIVES "megaman-x.cheat.start-5-lives"
#define MMX_CHEAT_START_1_LIFE "megaman-x.cheat.start-1-life"
#define MMX_CHEAT_INFINITE_LIVES "megaman-x.cheat.infinite-lives"
#define MMX_CHEAT_LESS_ENERGY "megaman-x.cheat.less-energy"
#define MMX_CHEAT_MORE_ENERGY "megaman-x.cheat.more-energy"
#define MMX_CHEAT_ALL_WEAPONS_NO_SIGMA "megaman-x.cheat.all-weapons-no-sigma"
#define MMX_CHEAT_INFINITE_WEAPONS "megaman-x.cheat.infinite-weapons"
#define MMX_CHEAT_JUMP_HEIGHT "megaman-x.cheat.jump-height"
#define MMX_CHEAT_DISABLE_CHARGING "megaman-x.cheat.disable-charging"
#define MMX_CHEAT_PACKAGE "megaman-x.enhancement.cheats"
#define MMX_CHEAT_JUMP_FEATURE "cheat-jump-height"

enum {
  MMX_RAM_X_STATE = 0x0BAA,
  MMX_RAM_Y_VELOCITY = 0x0BC4,
  MMX_RAM_HEALTH = 0x0BCF,
  MMX_RAM_CHARGE_TIMER = 0x0BFF,
  MMX_RAM_CHARGE_LEVEL = 0x0C03,
  MMX_RAM_WEAPON_ENERGY = 0x1F87,
  MMX_RAM_STAGE = 0x1F7A,
  MMX_RAM_LIVES = 0x1F80,
  MMX_RAM_STAGE_FLAGS0 = 0x1F83,
  MMX_RAM_MAX_HEALTH = 0x1F9A,
};

enum {
  MMX_FLAG_INFINITE_LIVES = 1u << 0,
  MMX_FLAG_ALL_WEAPONS_NO_SIGMA = 1u << 3,
  MMX_FLAG_INFINITE_WEAPONS = 1u << 4,
  MMX_FLAG_DISABLE_CHARGING = 1u << 5,
};

static uint32_t g_mmx_cheat_flags;
static int g_mmx_cheat_start_lives;
static int g_mmx_cheat_start_energy;
static int g_mmx_cheat_start_lives_applied;
static int g_mmx_cheat_start_energy_applied;
static uint8_t g_mmx_cheat_jump_velocity_hi;
static uint8_t g_mmx_cheat_prev_x_state;

static void mmx_cheats_frame(void);

static void mmx_cheats_reset(void) {
  g_mmx_cheat_flags = 0;
  g_mmx_cheat_start_lives = -1;
  g_mmx_cheat_start_energy = -1;
  g_mmx_cheat_start_lives_applied = 0;
  g_mmx_cheat_start_energy_applied = 0;
  g_mmx_cheat_jump_velocity_hi = 0;
  g_mmx_cheat_prev_x_state = 0;
}

static void mmx_cheats_register_frame(void) {
  (void)snes_mod_register_frame_callback(mmx_cheats_frame);
}

static void mmx_cheat_start_10_lives(void) {
  g_mmx_cheat_start_lives = 9;
  mmx_cheats_register_frame();
}

static void mmx_cheat_start_7_lives(void) {
  g_mmx_cheat_start_lives = 6;
  mmx_cheats_register_frame();
}

static void mmx_cheat_start_5_lives(void) {
  g_mmx_cheat_start_lives = 4;
  mmx_cheats_register_frame();
}

static void mmx_cheat_start_1_life(void) {
  g_mmx_cheat_start_lives = 0;
  mmx_cheats_register_frame();
}

static void mmx_cheat_infinite_lives(void) {
  g_mmx_cheat_flags |= MMX_FLAG_INFINITE_LIVES;
  mmx_cheats_register_frame();
}

static void mmx_cheat_less_energy(void) {
  g_mmx_cheat_start_energy = 8;
  mmx_cheats_register_frame();
}

static void mmx_cheat_more_energy(void) {
  g_mmx_cheat_start_energy = 0x20;
  mmx_cheats_register_frame();
}

static void mmx_cheat_all_weapons_no_sigma(void) {
  g_mmx_cheat_flags |= MMX_FLAG_ALL_WEAPONS_NO_SIGMA;
  mmx_cheats_register_frame();
}

static void mmx_cheat_infinite_weapons(void) {
  g_mmx_cheat_flags |= MMX_FLAG_INFINITE_WEAPONS;
  mmx_cheats_register_frame();
}

static void mmx_cheat_jump_height(void) {
  char mode[16] = {0};
  if (!snes_mod_feature_option_value_c(MMX_CHEAT_PACKAGE,
                                       MMX_CHEAT_JUMP_FEATURE,
                                       "mode", mode, sizeof(mode))) {
    strcpy(mode, "super");
  }
  if (strcmp(mode, "bogus") == 0)
    g_mmx_cheat_jump_velocity_hi = 0x04;
  else if (strcmp(mode, "mega") == 0)
    g_mmx_cheat_jump_velocity_hi = 0x09;
  else
    g_mmx_cheat_jump_velocity_hi = 0x07;
  mmx_cheats_register_frame();
}

static void mmx_cheat_disable_charging(void) {
  g_mmx_cheat_flags |= MMX_FLAG_DISABLE_CHARGING;
  mmx_cheats_register_frame();
}

static int mmx_cheats_in_stage(void) {
  return g_ram[0x00D1] == 0x02 && g_ram[0x00D2] == 0x04 &&
         g_ram[MMX_RAM_STAGE] <= 0x0C;
}

static void mmx_cheats_fill_all_weapons(void) {
  for (uint32_t off = 0; off <= 0x10; off += 2) {
    g_ram[MMX_RAM_WEAPON_ENERGY + off] = 0x00;
    g_ram[MMX_RAM_WEAPON_ENERGY + off + 1] = 0xDC;
  }
}

static void mmx_cheats_refill_owned_weapons(void) {
  for (uint32_t off = 0; off <= 0x10; off += 2) {
    uint8_t *sub = &g_ram[MMX_RAM_WEAPON_ENERGY + off];
    uint8_t *units = &g_ram[MMX_RAM_WEAPON_ENERGY + off + 1];
    if ((*units & 0x40) != 0) {
      *sub = 0x00;
      *units = (uint8_t)((*units & 0xC0) | 0x1C);
    }
  }
}

static void mmx_cheats_apply_all_weapons_no_sigma(void) {
  mmx_cheats_fill_all_weapons();
  for (uint32_t i = 0; i < 4; i++)
    g_ram[MMX_RAM_STAGE_FLAGS0 + i] = 0xFF;
}

static void mmx_cheats_apply_jump(uint8_t x_state) {
  if (!g_mmx_cheat_jump_velocity_hi)
    return;
  if (x_state == 0x06 && g_mmx_cheat_prev_x_state != 0x06) {
    g_ram[MMX_RAM_Y_VELOCITY] = 0x00;
    g_ram[MMX_RAM_Y_VELOCITY + 1] = g_mmx_cheat_jump_velocity_hi;
  }
}

static void mmx_cheats_frame(void) {
  int in_stage = mmx_cheats_in_stage();
  uint8_t x_state = g_ram[MMX_RAM_X_STATE];

  if (!in_stage) {
    g_mmx_cheat_start_lives_applied = 0;
    g_mmx_cheat_start_energy_applied = 0;
    g_mmx_cheat_prev_x_state = x_state;
    return;
  }

  if (g_mmx_cheat_start_lives >= 0 && !g_mmx_cheat_start_lives_applied) {
    g_ram[MMX_RAM_LIVES] = (uint8_t)g_mmx_cheat_start_lives;
    g_mmx_cheat_start_lives_applied = 1;
  }

  if (g_mmx_cheat_start_energy >= 0 && !g_mmx_cheat_start_energy_applied) {
    if (g_mmx_cheat_start_energy > g_ram[MMX_RAM_MAX_HEALTH])
      g_ram[MMX_RAM_MAX_HEALTH] = (uint8_t)g_mmx_cheat_start_energy;
    g_ram[MMX_RAM_HEALTH] = (uint8_t)g_mmx_cheat_start_energy;
    g_mmx_cheat_start_energy_applied = 1;
  }

  if (g_mmx_cheat_flags & MMX_FLAG_INFINITE_LIVES)
    g_ram[MMX_RAM_LIVES] = 9;

  if (g_mmx_cheat_flags & MMX_FLAG_ALL_WEAPONS_NO_SIGMA)
    mmx_cheats_apply_all_weapons_no_sigma();
  else if (g_mmx_cheat_flags & MMX_FLAG_INFINITE_WEAPONS)
    mmx_cheats_refill_owned_weapons();

  if (g_mmx_cheat_flags & MMX_FLAG_DISABLE_CHARGING) {
    g_ram[MMX_RAM_CHARGE_TIMER] = 0x95;
    g_ram[MMX_RAM_CHARGE_LEVEL] = 0;
  }

  mmx_cheats_apply_jump(x_state);
  g_mmx_cheat_prev_x_state = x_state;
}

SNES_MOD_CONSTRUCTOR(mmx_register_cheats_plugin) {
  (void)snes_mod_register_reset_callback(mmx_cheats_reset);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_START_10_LIVES,
                                            mmx_cheat_start_10_lives);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_START_7_LIVES,
                                            mmx_cheat_start_7_lives);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_START_5_LIVES,
                                            mmx_cheat_start_5_lives);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_START_1_LIFE,
                                            mmx_cheat_start_1_life);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_INFINITE_LIVES,
                                            mmx_cheat_infinite_lives);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_LESS_ENERGY,
                                            mmx_cheat_less_energy);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_MORE_ENERGY,
                                            mmx_cheat_more_energy);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_ALL_WEAPONS_NO_SIGMA,
                                            mmx_cheat_all_weapons_no_sigma);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_INFINITE_WEAPONS,
                                            mmx_cheat_infinite_weapons);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_JUMP_HEIGHT,
                                            mmx_cheat_jump_height);
  (void)snes_mod_register_activation_plugin(MMX_CHEAT_DISABLE_CHARGING,
                                            mmx_cheat_disable_charging);
}
