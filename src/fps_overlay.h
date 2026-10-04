// ssx - minimal always-on FPS overlay (external overlays cannot hook the
// presenter's swap chain).

#pragma once

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "frame_stats.h"

class FpsOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    ImGui::SetNextWindowPos(ImVec2(8, 8));
    ImGui::SetNextWindowBgAlpha(0.45f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##ssx_fps", nullptr, kFlags)) {
      ImGui::Text("%.0f FPS", ssx::g_guest_fps.load());
      ImGui::Text("%.2f ms avg / %.2f ms max", ssx::g_guest_frame_ms_avg.load(),
                  ssx::g_guest_frame_ms_max.load());
    }
    ImGui::End();
  }
};
