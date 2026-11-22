#include "GUI.hpp"

#include <OPPCH.h>

GUI::GUI(SDL_Window* window, SDL_GLContext context, App*& app) : app(app) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForOpenGL(window, context);
  ImGui_ImplOpenGL3_Init("#version 150");
}

void GUI::draw() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("Info");
  ImGui::SetWindowPos(ImVec2(0, 0));
  ImGui::Text("FPS: %.1f (%.3f ms/f)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
  ImVec2 infoWindowPos = ImGui::GetWindowPos();
  ImVec2 infoWindowSize = ImGui::GetWindowSize();
  ImGui::End();

  ImGui::Begin("Settings");
  app->OnImGuiRender();
  ImGui::SetWindowPos(ImVec2(0, infoWindowPos.y + infoWindowSize.y));
  ImGui::SetWindowSize(ImVec2(infoWindowSize.x, 0));
  ImGui::End();

  // SplatDiff panel (top-right)
  ImGuiIO& io = ImGui::GetIO();
  const float panelWidth = 400.0f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelWidth, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y), ImGuiCond_Always);
  ImGui::Begin("SplatDiff", nullptr, ImGuiWindowFlags_NoMove);
  app->OnSplatDiffImGui();
  ImGui::End();

  // Big "SCAN A / SCAN B" label so the audience always knows which scan is on screen
  const char* label = app->isShowingB() ? "SCAN B (after)" : "SCAN A (before)";
  ImU32 colour = app->isShowingB() ? IM_COL32(255, 200, 90, 255) : IM_COL32(120, 200, 255, 255);
  ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), 30.0f, ImVec2(20, io.DisplaySize.y - 50), colour, label);

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}

bool GUI::isMouseOverGUI() {
  ImGuiIO& io = ImGui::GetIO();
  return io.WantCaptureMouse;
}
