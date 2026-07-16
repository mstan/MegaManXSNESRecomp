#include "linux_ui.h"

#include "mmx_display.h"
extern "C" {
#include "mmx_rtl.h"
}

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>
#include <stdio.h>

static bool g_ui_open;
static bool g_use_opengl;
static char g_state_message[64];

static void RenderMenu(void) {
  ImGui::SetNextWindowSize(ImVec2(430.0f, 600.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Mega Man X", &g_ui_open);
  ImGui::TextUnformatted("Display settings");
  ImGui::Separator();

  bool widescreen = MmxDisplay_IsWidescreenEnabled();
  if (ImGui::Checkbox("True widescreen renderer", &widescreen))
    MmxDisplay_SetWidescreenEnabled(widescreen);
  ImGui::Text("Status: %s, %s (%dx224)", widescreen ? "enabled" : "disabled",
              MmxDisplay_IsWidescreenActive() ? "active" : "inactive",
              MmxDisplay_GetCurrentFrameWidth());
  ImGui::TextWrapped("Adds host-only PPU scene columns; gameplay stays unchanged.");

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
  ImGui::TextUnformatted("F1 toggles this menu");
  ImGui::Text("Renderer: %s", g_use_opengl ? "OpenGL" : "SDL");
  ImGui::TextUnformatted("Controller: SDL GameController");
  ImGui::TextUnformatted("Audio: SDL audio");
  ImGui::End();
}

static void BeginFrame(void) {
  if (g_use_opengl)
    ImGui_ImplOpenGL3_NewFrame();
  else
    ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  RenderMenu();
  ImGui::Render();
}

extern "C" bool LinuxUi_Init(SDL_Window *window, SDL_Renderer *renderer,
                              bool use_opengl) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();

  g_use_opengl = use_opengl;
  bool platform_ok;
  bool renderer_ok;
  if (use_opengl) {
    platform_ok = ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
    renderer_ok = ImGui_ImplOpenGL3_Init("#version 330 core");
  } else {
    platform_ok = ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    renderer_ok = ImGui_ImplSDLRenderer2_Init(renderer);
  }
  if (!platform_ok || !renderer_ok) {
    LinuxUi_Shutdown();
    return false;
  }
  g_ui_open = false;
  g_state_message[0] = '\0';
  return true;
}

extern "C" void LinuxUi_Shutdown(void) {
  if (!ImGui::GetCurrentContext()) return;
  if (g_use_opengl)
    ImGui_ImplOpenGL3_Shutdown();
  else
    ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  g_ui_open = false;
}

extern "C" void LinuxUi_ProcessEvent(const void *event) {
  if (ImGui::GetCurrentContext())
    ImGui_ImplSDL2_ProcessEvent((const SDL_Event *)event);
}

extern "C" void LinuxUi_Toggle(void) { g_ui_open = !g_ui_open; }
extern "C" bool LinuxUi_IsOpen(void) { return g_ui_open; }
extern "C" bool LinuxUi_CaptureInput(void) { return g_ui_open; }

extern "C" void LinuxUi_RenderSdl(SDL_Renderer *renderer) {
  if (!g_ui_open || !ImGui::GetCurrentContext() || g_use_opengl) return;

  /* The game uses an emulated-pixel logical size. ImGui uses window pixels,
   * so render it with logical scaling disabled and restore the game mapping. */
  int logical_width, logical_height;
  SDL_RenderGetLogicalSize(renderer, &logical_width, &logical_height);
  SDL_RenderSetLogicalSize(renderer, 0, 0);
  BeginFrame();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
  SDL_RenderSetLogicalSize(renderer, logical_width, logical_height);
}

extern "C" void LinuxUi_RenderOpenGL(void) {
  if (!g_ui_open || !ImGui::GetCurrentContext() || !g_use_opengl) return;
  BeginFrame();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
