#include "mmx_armor.h"
#include "recomp_launcher.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int enabled,game=2,verified[2],commits,tries;
static int count(void *ctx) {(void)ctx;return 1;}
static int feature(void *ctx,int i,RecompLauncherCModFeature *out) {
  (void)ctx;if(i)return 0;memset(out,0,sizeof(*out));
  strcpy(out->package_id,"megaman-x.armor");strcpy(out->id,"armor");out->enabled=enabled;return 1;
}
static int option(void *ctx,const char *p,const char *f,int i,RecompLauncherCModOption *out) {
  (void)ctx;(void)p;(void)f;if(i)return 0;memset(out,0,sizeof(*out));
  strcpy(out->id,"game");strcpy(out->value,game==2?"x2":"x3");return 1;
}
static int resources(void *ctx,const char *p,const char *f) {(void)ctx;(void)p;(void)f;return 2;}
static int resource(void *ctx,const char *p,const char *f,int i,RecompLauncherCModResource *out) {
  (void)ctx;(void)p;(void)f;if(i<0 || i>1)return 0;memset(out,0,sizeof(*out));
  strcpy(out->id,i?"x3-rom":"x2-rom");out->verified=verified[i];
  if(verified[i])strcpy(out->path,i?"owner-x3.sfc":"owner-x2.sfc");return 1;
}
static int commit(void *ctx,const char *path) {(void)ctx;(void)path;++commits;return 1;}
static int try_commit(void *ctx,const char *path) {(void)ctx;(void)path;++tries;return 1;}
static const char *error(void *ctx) {(void)ctx;return "original error";}
int main(void) {
  RecompLauncherCModProvider original={0};original.feature_count=count;original.feature_get=feature;
  original.feature_option_get=option;original.feature_resource_count=resources;original.feature_resource_get=resource;
  original.commit=commit;original.try_commit=try_commit;original.last_error=error;original.commit_worker_safe=1;
  RecompLauncherCGameInfo info={0};info.mods=&original;MmxArmorConfigureLauncher(&info);
  const RecompLauncherCModProvider *p=info.mods;assert(p && p!=&original && p->commit_worker_safe);
  assert(p->try_commit(NULL,"mmx.sfc")==1 && tries==1);assert(p->commit(NULL,"mmx.sfc") && commits==1);
  enabled=1;assert(!p->commit(NULL,"mmx.sfc") && commits==1);
  assert(strstr(p->last_error(NULL),"X2 USA ROM"));assert(!p->try_commit(NULL,"mmx.sfc") && tries==1);
  verified[0]=1;assert(p->commit(NULL,"mmx.sfc") && commits==2);
  RecompLauncherCModResource out={0};assert(p->feature_resource_get(NULL,"megaman-x.armor","armor",0,&out) && out.required);
  assert(p->feature_resource_get(NULL,"megaman-x.armor","armor",1,&out) && !out.required);
  game=3;assert(!p->commit(NULL,"mmx.sfc") && commits==2);assert(strstr(p->last_error(NULL),"X3 USA ROM"));
  verified[1]=1;verified[0]=0;assert(p->commit(NULL,"mmx.sfc") && commits==3);
  assert(p->feature_resource_get(NULL,"megaman-x.armor","armor",0,&out) && !out.required);
  assert(p->feature_resource_get(NULL,"megaman-x.armor","armor",1,&out) && out.required);
  MmxArmorConfigureLauncher(&info);assert(p->commit(NULL,"mmx.sfc") && commits==4);
  enabled=0;assert(p->try_commit(NULL,"mmx.sfc")==1 && tries==2);
  puts("Selected donor gates Play; unselected donor is optional; disabled path retains prepared launch");return 0;
}
