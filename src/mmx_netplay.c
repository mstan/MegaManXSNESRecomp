/* MMX's session rules. Transport, lobby discovery and rollback stay shared. */
#include "mmx_netplay.h"
#include "mod_runtime.h"
#include "mmx_coop.h"
#include "mmx_weapons.h"
#include "mmx_armor.h"
#include <stdio.h>
#include <string.h>

#if defined(RECOMP_LAUNCHER) && !MMX_VARIANT_JP
#include "recomp_launcher.h"

static const char *const kCoop = "megaman-x.coop";
static const char *const kZero = "megaman-x.character.zero";
static const char *const kWide = "megaman-x.enhancement.widescreen";
static const RecompLauncherCModProvider *s_mods;
static const RecompLauncherCNetplayCallbacks *s_net;
static RecompLauncherCModProvider s_provider;
static RecompLauncherCNetplayCallbacks s_callbacks;
static int s_active;
static char s_error[512];

int MmxNetplayActive(void) { return s_active; }

int MmxNetplayReady(char *reason, size_t cap) {
  if(snes_mod_runtime_feature_enabled_c("megaman-x.armor","armor") && !MmxArmorEnabled()) {
    snprintf(reason,cap,"The selected X armor could not load. Check its source ROM and cache directory.");
    return 0;
  }
  if (!MmxCoopEnabled()) {
    snprintf(reason, cap, "X + Zero co-op could not load. Check the selected Zero behavior's source file.");
    return 0;
  }
  for (unsigned page = 1; page <= 2; ++page) {
    const char *id = page == 1 ? "megaman-x.weapons.x2" : "megaman-x.weapons.x3";
    if (snes_mod_runtime_feature_enabled_c(id, "weapons") && !MmxWeaponsPageEnabled(page)) {
      snprintf(reason, cap, "The selected X%u weapon pack could not load.", page + 1);
      return 0;
    }
  }
  return 1;
}

static int fail(const char *message) {
  snprintf(s_error, sizeof(s_error), "%s", message);
  return 0;
}
static const char *mod_error(void *ctx) {
  return s_error[0] ? s_error : s_mods->last_error(ctx);
}
static const char *net_error(void *ctx) {
  return s_error[0] ? s_error : s_net->last_error ? s_net->last_error(ctx) : "";
}
static void clear_error(void *ctx) {
  s_error[0] = 0;
  if (s_net->clear_last_error) s_net->clear_last_error(ctx);
}

static void mode_changed(int enabled) {
  s_error[0] = 0;
  if (!enabled) {
    snes_mod_runtime_end_temporary_c();
    s_active = 0;
    return;
  }
  if (s_active) return;
  if (!s_mods) s_mods = snes_mod_runtime_launcher_provider_c();
  if (!s_mods || !snes_mod_runtime_begin_temporary_c()) {
    fail("Netplay requires the bundled X + Zero co-op mod.");
    return;
  }
  s_active = 1;
  s_mods->feature_enable(s_mods->ctx, kZero, "zero", 0);
  if (!s_mods->feature_enable(s_mods->ctx, kCoop, "coop", 1))
    fail("The X + Zero co-op mod is missing. Restore the bundled mods.");
  /* The offline layout is not part of an online room. This temporary plan
   * restores the player's saved local preference when they leave Netplay. */
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "local_cameras", "unified");
  /* Preserve an existing fixed choice; Adaptive becomes 16:9 in this
   * temporary plan only. Disabled widescreen remains the native view. */
  char aspect[32] = {0};
  snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
  if (!strcmp(aspect, "adaptive"))
    s_mods->feature_set_option(s_mods->ctx, kWide, "widescreen", "aspect", "16:9");
}

static int validate_plan(void) {
  if (!s_active || !snes_mod_runtime_feature_enabled_c(kCoop, "coop"))
    return fail("Netplay requires X + Zero co-op. Reopen Netplay to prepare the room.");
  if (snes_mod_runtime_feature_enabled_c(kZero, "zero"))
    return fail("Character switching must be off during co-op netplay.");
  char cameras[32] = {0};
  snes_mod_runtime_feature_option_value_c(kCoop, "coop", "cameras", cameras, sizeof(cameras));
  if (strcmp(cameras, "independent") && strcmp(cameras, "unified"))
    return fail("Choose Independent or Unified netplay cameras. Restore the bundled co-op mod if this option is missing.");
  if (snes_mod_runtime_feature_enabled_c(kWide, "widescreen")) {
    char aspect[32] = {0};
    snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
    if (strcmp(aspect, "16:9") && strcmp(aspect, "21:9") && strcmp(aspect, "32:9"))
      return fail("Choose a fixed netplay view: 16:9, 21:9, or 32:9.");
  }
  s_error[0] = 0;
  return 1;
}

static int feature_enable(void *ctx, const char *pkg, const char *feature, int enabled) {
  s_error[0] = 0;
  if (s_active && ((!strcmp(pkg, kCoop) && !enabled) || (!strcmp(pkg, kZero) && enabled)))
    return fail("Netplay requires X / Zero Co-op; Add Zero is unavailable.");
  return s_mods->feature_enable(ctx, pkg, feature, enabled);
}
static int set_enabled(void *ctx, const char *pkg, int enabled) {
  if (s_active && ((!strcmp(pkg, kCoop) && !enabled) || (!strcmp(pkg, kZero) && enabled)))
    return fail("Netplay requires X + Zero co-op.");
  s_error[0] = 0;
  return s_mods->set_enabled(ctx, pkg, enabled);
}
static int hidden_camera_option(const char *id) {
  return !strcmp(id, s_active ? "local_cameras" : "cameras");
}
static int feature_get(void *ctx, int index, RecompLauncherCModFeature *out) {
  if (!s_mods->feature_get(ctx, index, out)) return 0;
  if (!strcmp(out->package_id, kCoop)) {
    RecompLauncherCModOption option;
    for (int i=0; s_mods->feature_option_get(ctx, kCoop, out->id, i, &option); ++i)
      if (hidden_camera_option(option.id)) --out->option_count;
  }
  if (s_active && !strcmp(out->package_id, kZero)) out->hidden = 1;
  if (s_active && !strcmp(out->package_id, kCoop) && !out->has_error)
    snprintf(out->status, sizeof(out->status), "Required for netplay");
  return 1;
}
static int option_get(void *ctx, const char *pkg, const char *fid, int index,
                      RecompLauncherCModOption *out) {
  if (!strcmp(pkg, kCoop)) {
    int visible=0, found=0;
    for (int i=0; s_mods->feature_option_get(ctx, pkg, fid, i, out); ++i) {
      if (hidden_camera_option(out->id)) continue;
      if (visible++==index) { found=1; break; }
    }
    if (!found) return 0;
  } else if (!s_mods->feature_option_get(ctx, pkg, fid, index, out)) return 0;
  if (s_active && !strcmp(pkg, kWide) && !strcmp(out->id, "aspect")) {
    out->choice_count = 3;
    snprintf(out->default_value, sizeof(out->default_value), "16:9");
    snprintf(out->description, sizeof(out->description),
        "The host chooses the same fixed view for both players. Resizing scales the picture. Netplay uses original CRT pixel proportions.");
  }
  return 1;
}
static int choice_get(void *ctx, const char *pkg, const char *fid, const char *option,
                      int index, RecompLauncherCModChoice *out) {
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect")) {
    static const char *const ratios[] = {"16:9", "21:9", "32:9"};
    if (index < 0 || index >= 3) return 0;
    snprintf(out->value, sizeof(out->value), "%s", ratios[index]);
    snprintf(out->label, sizeof(out->label), "%s", ratios[index]);
    return 1;
  }
  return s_mods->feature_choice_get(ctx, pkg, fid, option, index, out);
}
static int set_option(void *ctx, const char *pkg, const char *fid, const char *option, const char *value) {
  s_error[0] = 0;
  if (!s_active && !strcmp(pkg, kCoop) && !strcmp(option, "cameras"))
    return fail("Choose Local co-op display for offline play. Open Netplay to choose the room's cameras.");
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(option, "local_cameras"))
    return fail("Local co-op display is available offline. Choose Netplay cameras for the room.");
  if (!strcmp(option, "behavior") &&
      ((!strcmp(pkg, kZero) && !strcmp(fid, "zero")) ||
       (!strcmp(pkg, kCoop) && !strcmp(fid, "coop")))) {
    /* One user preference exposed in both mutually exclusive character
     * features. Keep both catalog values identical so whichever feature is
     * active also carries this rule in saves and the netplay agreement.
     * Netplay's temporary plan provides its existing restore-on-exit scope. */
    const char *other = !strcmp(pkg, kZero) ? kCoop : kZero;
    const char *other_fid = !strcmp(pkg, kZero) ? "coop" : "zero";
    char previous[32] = {0};
    snes_mod_runtime_feature_option_value_c(pkg, fid, option, previous, sizeof(previous));
    if (!s_mods->feature_set_option(ctx, pkg, fid, option, value)) return 0;
    if (!s_mods->feature_set_option(ctx, other, other_fid, option, value)) {
      s_mods->feature_set_option(ctx, pkg, fid, option, previous);
      return fail("Cannot update the shared Zero behavior. Restore both bundled character mods.");
    }
    return 1;
  }
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect") &&
      strcmp(value, "16:9") && strcmp(value, "21:9") && strcmp(value, "32:9"))
    return fail("Adaptive view is available offline. Choose a fixed ratio for netplay.");
  return s_mods->feature_set_option(ctx, pkg, fid, option, value);
}
static bool zero_feature(const char *package, const char *feature) {
  return (!strcmp(package, kZero) && !strcmp(feature, "zero")) ||
         (!strcmp(package, kCoop) && !strcmp(feature, "coop"));
}
static const char *zero_source(const char *package, const char *feature) {
  char behavior[16] = {0};
  snes_mod_runtime_feature_option_value_c(package, feature, "behavior", behavior, sizeof(behavior));
  return !strcmp(behavior, "x1") ? "project-zero-ips" : "x3-rom";
}
static int resource_get(void *ctx, const char *package, const char *feature,
                        int index, RecompLauncherCModResource *out) {
  if (!s_mods->feature_resource_get ||
      !s_mods->feature_resource_get(ctx, package, feature, index, out)) return 0;
  if (zero_feature(package, feature) && !strcmp(out->id, "project-zero-ips")) {
    out->required = !strcmp(out->id, zero_source(package, feature));
    if (out->required && !out->path[0]) {
      out->verified = 0;
      snprintf(out->status, sizeof(out->status), "Required: select a file");
    }
    if (out->path[0])
      snprintf(out->status, sizeof(out->status), "%s",
          out->verified ? "Project Zero patch verified" : "Select the Project Zero v1.0 IPS");
  }
  return 1;
}
static bool zero_selection(const char **package, const char **feature) {
  *package = kCoop;
  *feature = "coop";
  if (snes_mod_runtime_feature_enabled_c(*package, *feature)) return true;
  *package = kZero;
  *feature = "zero";
  return snes_mod_runtime_feature_enabled_c(*package, *feature);
}
static int validate_zero_source(void *ctx) {
  const char *package, *feature;
  if (!zero_selection(&package, &feature)) return 1;
  RecompLauncherCModResource resource = {0};
  for (int i = 0; resource_get(ctx, package, feature, i, &resource); ++i) {
    if (!resource.required || (resource.verified && resource.path[0])) continue;
    return fail(!strcmp(resource.id, "project-zero-ips") ?
        "Select the Mega Man X: Project Zero v1.0 IPS patch for X1 Zero." :
        "Select your original Mega Man X3 USA ROM for this Zero behavior.");
  }
  return 1;
}
static int commit(void *ctx, const char *rom) {
  s_error[0] = 0;
  return validate_zero_source(ctx) && s_mods->commit(ctx, rom);
}
static int try_commit(void *ctx, const char *rom) {
  const char *package, *feature;
  if (zero_selection(&package, &feature)) return 0;
  s_error[0] = 0;
  return s_mods->try_commit ? s_mods->try_commit(ctx, rom) : 0;
}
static int commit_netplay(void *ctx, const char *rom) {
  return validate_plan() && commit(ctx, rom);
}
static int no(void *ctx) { (void)ctx; return 0; }
static int create(void *ctx, const char *name, char *endpoint, const char *password,
                   const RecompLauncherCSettings *settings, int lan, int slots) {
  (void)slots;
  mode_changed(1);
  if (!validate_plan() || !validate_zero_source(s_mods->ctx)) return -1;
  if (s_net->allow_spectators_set) s_net->allow_spectators_set(ctx, 0);
  return s_net->create(ctx, name, endpoint, password, settings, lan, 2);
}
static int fill_launch(void *ctx, RecompLauncherCNetplayLaunch *out) {
  if (!s_net->fill_launch(ctx, out)) return 0;
  if (out->is_spectator || out->host_spectates || out->max_slots != 2 || out->player_count != 2)
    return fail("Mega Man X netplay requires exactly two players.");
  return 1;
}

void MmxNetplayConfigureLauncher(RecompLauncherCGameInfo *info) {
  if (!info->mods || info->mods == &s_provider) return;
  s_mods = info->mods;
  s_provider = *s_mods;
  s_provider.feature_enable = feature_enable;
  s_provider.set_enabled = set_enabled;
  s_provider.feature_get = feature_get;
  s_provider.feature_option_get = option_get;
  s_provider.feature_choice_get = choice_get;
  s_provider.feature_set_option = set_option;
  s_provider.commit = commit;
  s_provider.try_commit = try_commit;
  s_provider.feature_resource_get = resource_get;
  s_provider.last_error = mod_error;
  info->mods = &s_provider;
  if (!info->netplay_supported || !info->netplay) return;
  s_provider.commit_netplay = commit_netplay;
  info->netplay_mode_changed = mode_changed;
  s_net = info->netplay;
  s_callbacks = *s_net;
  s_callbacks.create = create;
  s_callbacks.fill_launch = fill_launch;
  s_callbacks.last_error = net_error;
  s_callbacks.clear_last_error = clear_error;
  s_callbacks.allow_spectators_get = NULL;
  s_callbacks.allow_spectators_set = NULL;
  s_callbacks.host_can_spectate = no;
  s_callbacks.automatch_available = no; /* Co-op has no competitive queue. */
  info->netplay = &s_callbacks;
}

int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  if (!from_lobby) mode_changed(1);
  if (validate_plan()) return 1;
  snprintf(reason, cap, "%s", s_error);
  return 0;
}
#else
int MmxNetplayActive(void) { return 0; }
int MmxNetplayReady(char *reason, size_t cap) { return MmxNetplayPrepare(0, reason, cap); }
void MmxNetplayConfigureLauncher(struct RecompLauncherCGameInfo *info) { (void)info; }
int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  (void)from_lobby;
  snprintf(reason, cap, "Netplay requires the USA co-op build.");
  return 0;
}
#endif
