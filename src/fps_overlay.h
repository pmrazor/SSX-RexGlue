// ssx - minimal always-on FPS overlay (external overlays cannot hook the
// presenter's swap chain).

#pragma once

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>
#if __has_include(<rex/ui/frame_generation_stats.h>)
#include <rex/ui/frame_generation_stats.h>
#endif
#if __has_include(<rex/ui/d3d12/ssx_hdr.h>)
#include <rex/ui/d3d12/ssx_hdr.h>
#include <rex/cvar.h>
REXCVAR_DECLARE(bool, d3d12_ssx_hdr_preview);
REXCVAR_DECLARE(bool, d3d12_ssx_hdr_layers);
#endif

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
      ImGui::Text("Render: %.0f FPS", ssx::g_guest_fps.load());
      ImGui::Text("%.2f ms avg / %.2f ms max", ssx::g_guest_frame_ms_avg.load(),
                  ssx::g_guest_frame_ms_max.load());
#if __has_include(<rex/ui/d3d12/ssx_hdr.h>)
      if (REXCVAR_GET(d3d12_ssx_hdr_preview)) {
        ImGui::Separator();
#if __has_include(<rex/ui/d3d12/ssx_hdr_status.h>)
        using rex::ui::d3d12::SsxHDRPath;
        const auto hdr = rex::ui::d3d12::SsxHDRStatus::Get().Read();
        if (hdr.path == SsxHDRPath::kNative)
          ImGui::TextColored(ImVec4(.4f, 1.f, .5f, 1.f), "Native HDR | %.0f nit target",
                            rex::ui::d3d12::GetSsxHDRSettings().peak_nits);
        else if (hdr.path == SsxHDRPath::kFrontend)
          ImGui::TextUnformatted("SDR mapped to HDR | menu/pause/loading");
        else if (hdr.path == SsxHDRPath::kCalibration)
          ImGui::TextUnformatted("HDR calibration pattern");
        else if (hdr.path == SsxHDRPath::kSDRDisplay)
          ImGui::TextColored(ImVec4(1.f, .7f, .3f, 1.f), "SDR output | HDR display unavailable");
        else
          ImGui::TextColored(ImVec4(1.f, .7f, .3f, 1.f), "SDR fallback | %s",
                            rex::ui::d3d12::SsxHDRPathName(hdr.path));
        const auto total = hdr.native_frames + hdr.fallback_frames;
        if (total)
          ImGui::Text("Race fallback: %llu / %llu frames (%.2f%%)",
                      static_cast<unsigned long long>(hdr.fallback_frames),
                      static_cast<unsigned long long>(total),
                      100.0 * double(hdr.fallback_frames) / double(total));
        if (hdr.recent_fallback && hdr.path == SsxHDRPath::kNative)
          ImGui::TextColored(ImVec4(1.f, .7f, .3f, 1.f), "Recent fallback: %s",
                            rex::ui::d3d12::SsxHDRPathName(hdr.last_fallback));
#else
        ImGui::Text("HDR requested: %.0f nit target (status unavailable)",
                    rex::ui::d3d12::GetSsxHDRSettings().peak_nits);
#endif
        if (!REXCVAR_GET(d3d12_ssx_hdr_layers))
          ImGui::TextUnformatted("Scene-only diagnostic");
      }
#endif
#if __has_include(<rex/ui/frame_generation_stats.h>)
      const auto fg = rex::ui::FrameGenerationStats::Get().Read();
      ImGui::Separator();
      if (!fg.enabled)
        ImGui::TextUnformatted("FG output: off");
      else if (!fg.active)
        ImGui::Text("FG output (%ux): suspended", fg.multiplier);
      else if (!fg.sampled)
        ImGui::Text("FG output (%ux): measuring...", fg.multiplier);
      else
        ImGui::Text("FG output (%ux): %.0f FPS", fg.multiplier, fg.output_fps);
#endif
    }
    ImGui::End();
  }
};
