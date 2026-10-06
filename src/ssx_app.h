// ssx - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>

#include "fps_overlay.h"

REXCVAR_DECLARE(bool, ssx_show_fps);

class SsxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SsxApp>(new SsxApp(ctx, "ssx",
        PPCImageConfig));
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_ssx_capture", "F8", "Capture a complete SSX GPU frame", [] {
      if (!rex::cvar::InvokeCommand("d3d12_capture_guest_frame", "")) {
        REXLOG_WARN("SSX capture requires the optional SDK guest-frame capture patch");
      }
    });
    if (REXCVAR_GET(ssx_show_fps)) {
      fps_overlay_ = std::make_unique<FpsOverlay>(drawer);
    }
  }
  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_ssx_capture");
    fps_overlay_.reset();
  }

 private:
  std::unique_ptr<FpsOverlay> fps_overlay_;

  bool SetupEnvironment() override {
    // The pinned SDK resolves content paths before loading ssx.toml. Preload
    // it so a direct desktop launch honors game_data_root and user_data_root.
    // The cvar registry preserves command-line precedence over the TOML file.
    rex::cvar::LoadConfig(rex::filesystem::GetExecutableFolder() / "ssx.toml");
    return rex::ReXApp::SetupEnvironment();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
