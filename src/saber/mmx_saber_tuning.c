#include "mmx_saber_tuning.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct MmxSaberTuningOption {
  const char *id;
  int default_value;
  int min_value;
  int max_value;
  void *value;
  bool boolean;
} MmxSaberTuningOption;

static MmxSaberTuning g_tuning = {
  {3, 3, 8, 16, 3, 3, 3, 6},
  {1, 1, 2, 6, 1, 1, 1, 4},
  {2, 3, 4, 1, 1, 5, 1, 1, 2, 3, 4, 5},
  70,
  27,
  50,
  false,
};

static MmxSaberTuningOption k_options[] = {
  {"slash1_damage", 3, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_SLASH1]},
  {"slash2_damage", 3, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_SLASH2]},
  {"slash3_damage", 8, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_SLASH3]},
  {"x3_finisher_damage", 16, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_X3_FINISHER]},
  {"air_damage", 3, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_AIR]},
  {"wall_damage", 3, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_WALL]},
  {"dash_damage", 3, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_DASH]},
  {"wave_damage", 6, 1, 32,
   &g_tuning.normal_damage[MMX_SABER_TUNING_DAMAGE_WAVE]},

  {"boss_slash1_damage", 1, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_SLASH1]},
  {"boss_slash2_damage", 1, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_SLASH2]},
  {"boss_slash3_damage", 2, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_SLASH3]},
  {"boss_air_damage", 1, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_AIR]},
  {"boss_wall_damage", 1, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_WALL]},
  {"boss_dash_damage", 1, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_DASH]},
  {"boss_x3_finisher_damage", 6, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_X3_FINISHER]},
  {"boss_wave_damage", 4, 0, 32,
   &g_tuning.boss_damage[MMX_SABER_TUNING_DAMAGE_WAVE]},

  {"slash1_priority", 2, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_SLASH1]},
  {"slash2_priority", 3, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_SLASH2]},
  {"slash3_priority", 4, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_SLASH3]},
  {"air_priority", 1, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_AIR]},
  {"wall_priority", 1, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_WALL]},
  {"dash_priority", 5, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_DASH]},
  {"charge_small_priority", 1, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_CHARGE_SMALL]},
  {"charge_full_priority", 1, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_CHARGE_FULL]},
  {"max_shot1_priority", 2, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_MAX_SHOT1]},
  {"max_shot2_priority", 3, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_MAX_SHOT2]},
  {"x3_finisher_priority", 4, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_X3_FINISHER]},
  {"wave_priority", 5, 1, 9,
   &g_tuning.priority[MMX_SABER_TUNING_PRIORITY_WAVE]},

  {"priority_window_frames", 70, 30, 120,
   &g_tuning.priority_window_frames},
  {"finisher_window_frames", 27, 12, 60,
   &g_tuning.finisher_window_frames},
  {"saber_swing_volume", 50, 0, 200, &g_tuning.saber_swing_volume},
  {"show_hitboxes", 0, 0, 1, &g_tuning.show_hitboxes, true},
};

static bool parse_integer(const char *text, int *out) {
  char *end = NULL;
  long parsed;

  if (!text || !text[0] || !out) return false;
  errno = 0;
  parsed = strtol(text, &end, 10);
  if (end == text || *end != '\0' || errno == ERANGE ||
      parsed < INT_MIN || parsed > INT_MAX)
    return false;
  *out = (int)parsed;
  return true;
}

static bool parse_boolean(const char *text, bool *out) {
  if (!text || !out) return false;
  if (!strcmp(text, "1") || !strcmp(text, "true") ||
      !strcmp(text, "on")) {
    *out = true;
    return true;
  }
  if (!strcmp(text, "0") || !strcmp(text, "false") ||
      !strcmp(text, "off")) {
    *out = false;
    return true;
  }
  return false;
}

void MmxSaberTuningLoad(MmxSaberTuningOptionReader reader, void *context) {
  for (unsigned i = 0; i < sizeof(k_options) / sizeof(k_options[0]); ++i) {
    char text[64] = {0};
    int parsed;
    bool parsed_boolean;
    MmxSaberTuningOption *option = k_options + i;

    if (option->boolean)
      *(bool *)option->value = option->default_value != 0;
    else
      *(int *)option->value = option->default_value;
    if (!reader || !reader(option->id, text, sizeof(text), context)) continue;
    text[sizeof(text) - 1] = '\0';
    if (option->boolean) {
      if (!parse_boolean(text, &parsed_boolean)) continue;
      *(bool *)option->value = parsed_boolean;
      continue;
    }
    if (!parse_integer(text, &parsed)) continue;
    if (parsed < option->min_value) parsed = option->min_value;
    if (parsed > option->max_value) parsed = option->max_value;
    *(int *)option->value = parsed;
  }
}

const MmxSaberTuning *MmxSaberTuningGet(void) {
  return &g_tuning;
}

int MmxSaberTuningNormalDamage(MmxSaberTuningDamageKind kind) {
  if (kind < 0 || kind >= MMX_SABER_TUNING_DAMAGE_COUNT) return 0;
  return g_tuning.normal_damage[kind];
}

int MmxSaberTuningBossDamage(MmxSaberTuningDamageKind kind) {
  int boss;
  if (kind < 0 || kind >= MMX_SABER_TUNING_DAMAGE_COUNT) return 0;
  boss = g_tuning.boss_damage[kind];
  return boss == 0 ? g_tuning.normal_damage[kind] : boss;
}

int MmxSaberTuningPriority(MmxSaberTuningPriorityKind kind) {
  if (kind < 0 || kind >= MMX_SABER_TUNING_PRIORITY_COUNT) return 0;
  return g_tuning.priority[kind];
}

int MmxSaberTuningPriorityWindowFrames(void) {
  return g_tuning.priority_window_frames;
}

int MmxSaberTuningFinisherWindowFrames(void) {
  return g_tuning.finisher_window_frames;
}

int MmxSaberTuningSaberSwingVolume(void) {
  return g_tuning.saber_swing_volume;
}

bool MmxSaberTuningShowHitboxes(void) {
  return g_tuning.show_hitboxes;
}

int MmxSaberTuningSwingVolume(void) {
  return MmxSaberTuningSaberSwingVolume();
}
