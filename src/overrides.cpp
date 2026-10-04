// ssx - hand-written overrides for recompiled guest functions.

#include <algorithm>
#include <chrono>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "frame_stats.h"

REXCVAR_DEFINE_BOOL(ssx_show_fps, true, "SSX",
                    "Show the guest frame rate overlay in the top-left corner.");
REXCVAR_DEFINE_INT32(ssx_present_interval, -1, "SSX",
                     "Guest vblanks between flips (-1 keeps the game's 2). 1 allows one flip "
                     "per vblank, so frames are capped at video_mode_refresh_rate.");
REXCVAR_DEFINE_INT32(ssx_render_fps, 60, "SSX",
                     "Render loop rate in Hz (the game ships with 60). 0 removes the cap. "
                     "Flips still follow guest vblanks, so also raise video_mode_refresh_rate "
                     "(the game flips every 2nd vblank unless ssx_present_interval is set).");

// sub_83046110 reads VSCR via mfvscr (unimplemented in codegen) and returns
// the NJ (non-Java / denormal flush) bit. Xenon runs with NJ set.
REX_HOOK_RAW(sub_83046110) {
  (void)base;
  ctx.r3.u64 = 1;
}

// sub_8233D770 is the game's present wrapper (sole caller of VdSwap). Feed the
// FPS overlay twice a second and log the guest frame rate every 5 seconds.
REX_EXTERN(__imp__sub_8233D770);
REX_HOOK_RAW(sub_8233D770) {
  using clock = std::chrono::steady_clock;
  static clock::time_point last_present = clock::now();
  static clock::time_point overlay_window = last_present;
  static clock::time_point log_window = last_present;
  static uint32_t overlay_frames = 0, log_frames = 0;
  static double max_frame_ms = 0;

  __imp__sub_8233D770(ctx, base);

  auto now = clock::now();
  max_frame_ms = std::max(max_frame_ms,
                          std::chrono::duration<double, std::milli>(now - last_present).count());
  last_present = now;
  ++overlay_frames;
  ++log_frames;

  double overlay_s = std::chrono::duration<double>(now - overlay_window).count();
  if (overlay_s >= 0.5) {
    ssx::g_guest_fps = float(overlay_frames / overlay_s);
    ssx::g_guest_frame_ms_avg = float(overlay_s * 1000.0 / overlay_frames);
    ssx::g_guest_frame_ms_max = float(max_frame_ms);
    overlay_frames = 0;
    max_frame_ms = 0;
    overlay_window = now;
  }
  double log_s = std::chrono::duration<double>(now - log_window).count();
  if (log_s >= 5.0) {
    REXLOG_INFO("Guest FPS: {:.1f}", log_frames / log_s);
    log_frames = 0;
    log_window = now;
  }
}

// The game's fixed-rate thread loops (sub_83047120) tick their object's update
// once the elapsed time accumulator reaches the interval in nanoseconds at +72;
// sub_83046C48 is that check, run every loop iteration. The render loop
// (vtable 82007738) is set to 1/60 s and the game rewrites the field later, so
// enforce the configured interval on every check.
REX_EXTERN(__imp__sub_83046C48);
REX_HOOK_RAW(sub_83046C48) {
  constexpr uint32_t kRenderLoopVtable = 0x82007738;
  constexpr uint32_t kIntervalOffset = 72;
  int32_t fps = REXCVAR_GET(ssx_render_fps);
  if (fps != 60) {
    uint32_t object = ctx.r3.u32;
    uint32_t vtable = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + object));
    if (vtable == kRenderLoopVtable) {
      uint64_t interval_ns = fps > 0 ? 1000000000ull / uint64_t(fps) : 0;
      *reinterpret_cast<uint64_t*>(base + object + kIntervalOffset) =
          __builtin_bswap64(interval_ns);
    }
  }
  __imp__sub_83046C48(ctx, base);
}

// sub_8233C870 is D3D's swap scheduler, called from the CP interrupt for each
// swap. Its argument packs the frontbuffer address (bits 12-31), the present
// interval in vblanks (bits 8-11) and a lateness threshold (bits 0-7). The game
// encodes an interval that keeps flips at least 1/60 s apart (2 at 120 Hz, 4 at
// 240 Hz); rewriting it lets flips follow the guest vblank rate.
REX_EXTERN(__imp__sub_8233C870);
REX_HOOK_RAW(sub_8233C870) {
  int32_t interval = REXCVAR_GET(ssx_present_interval);
  if (interval >= 0) {
    uint32_t flags = ctx.r3.u32;
    flags = (flags & ~0xF00u) | (uint32_t(std::min(interval, 15)) << 8);
    ctx.r3.u64 = flags;
  }
  __imp__sub_8233C870(ctx, base);
}
