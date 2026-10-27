#include "mmx_armor.h"
#if defined(RECOMP_LAUNCHER) && !MMX_VARIANT_JP
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
static const char *package="megaman-x.armor",*feature="armor";
static RecompLauncherCModProvider base,provider;
static char error[512];
static int armor_enabled(void *ctx) {
  if(!base.feature_count || !base.feature_get)return 0;
  for(int i=0;i<base.feature_count(ctx);++i) {
    RecompLauncherCModFeature f={0};
    if(base.feature_get(ctx,i,&f) && !strcmp(f.package_id,package) && !strcmp(f.id,feature))return f.enabled;
  }
  return 0;
}
static unsigned selected_game(void *ctx) {
  RecompLauncherCModOption option={0};
  if(base.feature_option_get && base.feature_option_get(ctx,package,feature,0,&option) &&
      !strcmp(option.id,"game") && !strcmp(option.value,"x3"))return 3;
  return 2;
}
static int resource_get(void *ctx,const char *p,const char *f,int index,RecompLauncherCModResource *out) {
  if(!base.feature_resource_get || !base.feature_resource_get(ctx,p,f,index,out))return 0;
  if(!strcmp(p,package) && !strcmp(f,feature)) {
    const char *id=selected_game(ctx)==2?"x2-rom":"x3-rom";
    out->required=!strcmp(out->id,id);
  }
  return 1;
}
static int validate(void *ctx) {
  error[0]=0;if(!armor_enabled(ctx))return 1;
  unsigned game=selected_game(ctx);const char *id=game==2?"x2-rom":"x3-rom";
  int count=base.feature_resource_count?base.feature_resource_count(ctx,package,feature):0;
  for(int i=0;i<count;++i) {
    RecompLauncherCModResource resource={0};
    if(resource_get(ctx,package,feature,i,&resource) && !strcmp(resource.id,id)) {
      if(resource.path[0] && resource.verified)return 1;
      if(resource.path[0] && resource.status[0])
        snprintf(error,sizeof(error),"Mega Man X%u armor: %s",game,resource.status);
      break;
    }
  }
  if(!error[0])snprintf(error,sizeof(error),"Select your Mega Man X%u USA ROM for the selected armor.",game);
  return 0;
}
static int commit(void *ctx,const char *image) {
  return validate(ctx) && base.commit && base.commit(ctx,image);
}
static int try_commit(void *ctx,const char *image) {
  /* The selected donor must be checked on the commit worker. Resource
   * verification may read its identity; try_commit promises no heavy reads.
   * Mod-off launches retain the provider's prepared-commit fast path. */
  if(armor_enabled(ctx))return 0;
  error[0]=0;return base.try_commit?base.try_commit(ctx,image):0;
}
static const char *last_error(void *ctx) {
  return error[0]?error:base.last_error?base.last_error(ctx):"";
}
void MmxArmorConfigureLauncher(struct RecompLauncherCGameInfo *info) {
  if(!info || !info->mods || info->mods->commit==commit)return;
  base=*info->mods;provider=base;
  provider.commit=commit;provider.last_error=last_error;provider.try_commit=try_commit;
  provider.feature_resource_get=resource_get;info->mods=&provider;
}
#else
void MmxArmorConfigureLauncher(struct RecompLauncherCGameInfo *info) {(void)info;}
#endif
