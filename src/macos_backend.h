#pragma once

#include <stdbool.h>
#include <stdint.h>

struct RendererFuncs;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

void MacMetalRenderer_Create(struct RendererFuncs *funcs);
void MacMetal_SetRetroEnabled(bool enabled);
bool MacMetal_GetRetroEnabled(void);
void MacMetal_SetRetroOptions(int scanlines, int blend, int noise, int hue);
void MacMetal_GetRetroOptions(int *scanlines, int *blend, int *noise, int *hue);

void MacUi_Init(struct SDL_Window *window, void *metal_device);
void MacUi_Shutdown(void);
void MacUi_ProcessEvent(const void *event);
void MacUi_Toggle(void);
bool MacUi_IsOpen(void);
bool MacUi_CaptureKeyboard(void);
void MacUi_Render(void *pass_descriptor, void *command_buffer, void *command_encoder);

bool MacAudio_Init(int requested_frequency, int requested_samples,
                   int *actual_frequency, int *actual_channels,
                   int *frames_per_block);
void MacAudio_SetPaused(bool paused);
void MacAudio_SetVolume(float volume);
void MacAudio_Shutdown(void);

void MacGamepad_Poll(bool enable_player1, bool enable_player2, int deadzone,
                     uint32_t *player1, uint32_t *player2,
                     uint32_t *active_players);

#ifdef __cplusplus
}
#endif
