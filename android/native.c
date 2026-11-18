#include <jni.h>
#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "desktop/android_host.h"
#include "mmx_renderer.h"
static int wide=1,boss_rush=1,aspect=MMX_ASPECT_ADAPTIVE;
void MmxAndroidPrepare(void) {
 const char *home=SDL_AndroidGetInternalStoragePath();
 if(home) { setenv("SNESRECOMP_DATA_DIR",home,1); chdir(home); }
 SDL_SetHint(SDL_HINT_RENDER_DRIVER,"opengles2");
 SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
 setenv("SNESRECOMP_NO_LAUNCHER","1",1);
 FILE *file=fopen("android-settings.ini","r"); char line[256];
 while(file && fgets(line,sizeof(line),file)) {
   char *eq=strchr(line,'='); if(!eq) continue; *eq++=0;
   eq[strcspn(eq,"\r\n")]=0;
   if(!strcmp(line,"CrashReplay")) setenv("MMX_REPLAY_RECORD",strcmp(eq,"0")?"1":"0",1);
   if(!strcmp(line,"BossRush")) boss_rush=strcmp(eq,"0")!=0;
   if(!strcmp(line,"Widescreen")) wide=strcmp(eq,"0")!=0;
   if(!strcmp(line,"Aspect")) {
     if(!strcmp(eq,"4:3")) wide=0;
     else if(!strcmp(eq,"16:9")) aspect=MMX_ASPECT_16_9;
     else if(!strcmp(eq,"21:9")) aspect=MMX_ASPECT_21_9;
     else if(!strcmp(eq,"32:9")) aspect=MMX_ASPECT_32_9;
   }
 }
 if(file) fclose(file);
}
int MmxAndroidBossRush(void) { return boss_rush; }
int MmxAndroidWide(void) { return wide; }
int MmxAndroidAspect(void) { return aspect; }
JNIEXPORT void JNICALL Java_org_retroporting_megamanx_GameActivity_nativeInput(JNIEnv *e,jclass c,jint bits) {
 (void)e;(void)c;snesrecomp_android_input((uint32_t)bits);
}
JNIEXPORT void JNICALL Java_org_retroporting_megamanx_GameActivity_nativePause(JNIEnv *e,jclass c,jboolean paused) {
 (void)e;(void)c;snesrecomp_android_pause(paused);
}
JNIEXPORT void JNICALL Java_org_retroporting_megamanx_GameActivity_nativeGameQuit(JNIEnv *e,jclass c) {
 (void)e;(void)c;snesrecomp_android_quit();
}
JNIEXPORT void JNICALL Java_org_retroporting_megamanx_GameActivity_nativeSlot(JNIEnv *e,jclass c,jboolean save) {
 (void)e;(void)c;snesrecomp_android_slot(save);
}
