// SSX's scene-size selection, identified in the 1x race capture.
#include <bit>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(ssx_scene_720p, false, "SSX",
                    "Experimental: select a 1280x720 base scene instead of 1120x584. "
                    "Restart required; combine with 3x scaling for a 4K scene test.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(ssx_log_scene_size, false, "SSX",
                    "Log the game's scene-size selection and experimental override.");

// sub_82FB6968 selects preset 4 and supplies the output dimensions in r5/r6,
// with scene-width/height destinations in r7/r8. sub_82FB6120 writes only these
// dimensions; preset 4 is 1120x584 and preset 1 is 1280x720. Adjust the returned
// dimensions before the caller creates resources, never an already-live viewport.
// Limit the experiment to this exact call site and the captured input/output pair.
REX_EXTERN(__imp__sub_82FB6120);
REX_HOOK_RAW(sub_82FB6120) {
  const uint32_t caller = ctx.lr;
  const uint32_t preset = ctx.r4.u32;
  const uint32_t output_width = ctx.r5.u32;
  const uint32_t output_height = ctx.r6.u32;
  const uint32_t width_address = ctx.r7.u32;
  const uint32_t height_address = ctx.r8.u32;
  __imp__sub_82FB6120(ctx, base);

  const bool requested = REXCVAR_GET(ssx_scene_720p);
  if (!requested && !REXCVAR_GET(ssx_log_scene_size)) return;

  uint32_t width, height;
  std::memcpy(&width, base + width_address, sizeof(width));
  std::memcpy(&height, base + height_address, sizeof(height));
  width = std::byteswap(width);
  height = std::byteswap(height);
  const bool applied = requested && caller == 0x82FB69C4 && preset == 4 &&
                       output_width == 1280 && output_height == 720 &&
                       width == 1120 && height == 584;
  if (applied) {
    const uint32_t width_be = std::byteswap(1280u);
    const uint32_t height_be = std::byteswap(720u);
    std::memcpy(base + width_address, &width_be, sizeof(width_be));
    std::memcpy(base + height_address, &height_be, sizeof(height_be));
  }
  REXLOG_INFO("SSX_SCENE_SIZE caller={:08X} preset={} output={}x{} original={}x{} "
              "selected={}x{} experimental_requested={} applied={}",
              caller, preset, output_width, output_height, width, height,
              applied ? 1280u : width, applied ? 720u : height, requested, applied);
}
