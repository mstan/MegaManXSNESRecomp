#import <Metal/Metal.h>

#include "macos_backend.h"
#include "mmx_display.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "mmx_rtl.h"
#ifdef __cplusplus
}
#endif

#include "imgui.h"
#include "backends/imgui_impl_metal.h"
#include "backends/imgui_impl_sdl2.h"

#include <SDL.h>
#include <stdio.h>

static bool g_ui_open;
static char g_state_message[64];

extern "C" void MacUi_Init(SDL_Window *window, void *metal_device) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForMetal(window);
  ImGui_ImplMetal_Init((__bridge id<MTLDevice>)metal_device);
  g_ui_open = false;
}

extern "C" void MacUi_Shutdown(void) {
  if (!ImGui::GetCurrentContext()) return;
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  g_ui_open = false;
}

extern "C" void MacUi_ProcessEvent(const void *event) {
  if (ImGui::GetCurrentContext())
    ImGui_ImplSDL2_ProcessEvent((const SDL_Event *)event);
}

extern "C" void MacUi_Toggle(void) { g_ui_open = !g_ui_open; }
extern "C" bool MacUi_IsOpen(void) { return g_ui_open; }
extern "C" bool MacUi_CaptureKeyboard(void) { return g_ui_open; }

extern "C" void MacUi_Render(void *pass_descriptor, void *command_buffer, void *command_encoder) {
  if (!g_ui_open || !ImGui::GetCurrentContext()) return;

  MTLRenderPassDescriptor *pass = (__bridge MTLRenderPassDescriptor *)pass_descriptor;
  id<MTLCommandBuffer> command = (__bridge id<MTLCommandBuffer>)command_buffer;
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)command_encoder;
  ImGui_ImplMetal_NewFrame(pass);
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowSize(ImVec2(430.0f, 600.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Mega Man X", &g_ui_open);
  ImGui::TextUnformatted("Display settings");
  ImGui::Separator();

  bool retro = MacMetal_GetRetroEnabled();
  if (ImGui::Checkbox("Retro NTSC display", &retro))
    MacMetal_SetRetroEnabled(retro);

  int scanlines, blend, noise, hue;
  MacMetal_GetRetroOptions(&scanlines, &blend, &noise, &hue);
  bool scanlines_value = scanlines != 0;
  bool blend_value = blend != 0;
  bool changed = false;
  changed |= ImGui::Checkbox("Scanlines", &scanlines_value);
  changed |= ImGui::Checkbox("Phosphor blend", &blend_value);
  changed |= ImGui::SliderInt("Signal noise", &noise, 0, 24);
  changed |= ImGui::SliderInt("Hue", &hue, -30, 30);
  if (changed)
    MacMetal_SetRetroOptions(scanlines_value, blend_value, noise, hue);

  bool widescreen = MmxDisplay_IsWidescreenEnabled();
  if (ImGui::Checkbox("True widescreen renderer", &widescreen))
    MmxDisplay_SetWidescreenEnabled(widescreen);
  ImGui::Text("Status: %s, %s (%dx224)", widescreen ? "enabled" : "disabled",
              MmxDisplay_IsWidescreenActive() ? "active" : "inactive",
              MmxDisplay_GetCurrentFrameWidth());
  ImGui::TextUnformatted("Adds host-only PPU scene columns; gameplay stays unchanged.");

  ImGui::Separator();
  ImGui::TextUnformatted("Save states");
  for (int slot = 0; slot < 10; ++slot) {
    ImGui::PushID(slot);
    char save_label[16];
    char load_label[16];
    snprintf(save_label, sizeof(save_label), "Save %d", slot + 1);
    snprintf(load_label, sizeof(load_label), "Load %d", slot + 1);
    if (ImGui::Button(save_label)) {
      RtlSaveLoad(kSaveLoad_Save, slot);
      snprintf(g_state_message, sizeof(g_state_message), "Saved state %d", slot + 1);
    }
    ImGui::SameLine();
    if (ImGui::Button(load_label)) {
      RtlSaveLoad(kSaveLoad_Load, slot);
      snprintf(g_state_message, sizeof(g_state_message), "Loaded state %d", slot + 1);
    }
    ImGui::PopID();
  }
  if (g_state_message[0]) ImGui::TextUnformatted(g_state_message);

  ImGui::Separator();
  ImGui::Text("F1 toggles this menu");
  ImGui::Text("Controller: Apple GameController.framework");
  ImGui::Text("Audio: Core Audio output unit");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), command, encoder);
}
