#pragma once

#include <stdbool.h>

struct SDL_Renderer;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime settings UI for Linux. The renderer is NULL for OpenGL and the
 * active SDL_Renderer for accelerated/software SDL output. */
bool LinuxUi_Init(struct SDL_Window *window, struct SDL_Renderer *renderer,
                  bool use_opengl);
void LinuxUi_Shutdown(void);
void LinuxUi_ProcessEvent(const void *event);
void LinuxUi_Toggle(void);
bool LinuxUi_IsOpen(void);
bool LinuxUi_CaptureInput(void);
void LinuxUi_RenderSdl(struct SDL_Renderer *renderer);
void LinuxUi_RenderOpenGL(void);

#ifdef __cplusplus
}
#endif
