// Exercise the real generated selector through the handwritten hook. No game
// assets are read at runtime; codegen/build still requires the user's own XEX.
#include <array>
#include <bit>
#include <cstring>
#include <iostream>

#include <rex/cvar.h>
#include <rex/ppc/function.h>

REXCVAR_DECLARE(bool, ssx_scene_720p);
REX_EXTERN(sub_82FB6120);

int main() {
  struct Case {
    bool enabled;
    uint32_t caller, preset, output_width, output_height, expected_width, expected_height;
  };
  constexpr Case cases[] = {
      {false, 0x82FB69C4, 4, 1280, 720, 1120, 584},  // Default fallback.
      {true,  0x82FB69C4, 4, 1280, 720, 1280, 720},  // Captured scene selection.
      {true,  0x12345678, 4, 1280, 720, 1120, 584},  // Unrecognized caller.
      {true,  0x82FB69C4, 4, 1920, 1080, 1120, 584}, // Different output mode.
      {true,  0x82FB69C4, 2, 1280, 720, 1280, 646},  // Another built-in preset.
      {true,  0x82FB69C4, 0, 640, 480, 640, 480},    // Caller-supplied dimensions.
      {false, 0x82FB69C4, 4, 1280, 720, 1120, 584},  // Disabling restores stock.
  };
  for (size_t i = 0; i < std::size(cases); ++i) {
    const auto& test = cases[i];
    alignas(32) std::array<uint8_t, 64> memory;
    memory.fill(0xA5);
    PPCContext ctx{};
    ctx.lr = test.caller;
    ctx.r4.u64 = test.preset;
    ctx.r5.u64 = test.output_width;
    ctx.r6.u64 = test.output_height;
    ctx.r7.u64 = 16;
    ctx.r8.u64 = 24;
    REXCVAR_SET(ssx_scene_720p, test.enabled);
    sub_82FB6120(ctx, memory.data());
    uint32_t width, height;
    std::memcpy(&width, memory.data() + 16, sizeof(width));
    std::memcpy(&height, memory.data() + 24, sizeof(height));
    if (std::byteswap(width) != test.expected_width ||
        std::byteswap(height) != test.expected_height) {
      std::cerr << "Scene-size case " << i << " failed\n";
      return 1;
    }
    for (size_t byte = 0; byte < memory.size(); ++byte) {
      if ((byte >= 16 && byte < 20) || (byte >= 24 && byte < 28)) continue;
      if (memory[byte] != 0xA5) {
        std::cerr << "Scene-size case " << i << " wrote outside output fields\n";
        return 1;
      }
    }
  }
  std::cout << "7 scene-size cases passed against generated guest code\n";
  return 0;
}
