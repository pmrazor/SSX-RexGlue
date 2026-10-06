// Exercise the real hook with a synthetic guest device and reservation helper.
// No game assets are required. This checks command placement and ABI isolation.
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <rex/cvar.h>
#include <rex/ppc/function.h>
#include <rex/ui/guest_frame_timing.h>

REXCVAR_DECLARE(std::string, ssx_frame_timing_capture);
REXCVAR_DECLARE(bool, ssx_frame_token_handoff);
REX_EXTERN(sub_82FB55E8);
namespace {
bool reserve_called = false, fail_reserve = false;
bool reserve_args_valid = false;
void put(uint8_t *base, uint32_t at, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(base + at, &value, 4);
}
uint32_t get(uint8_t *base, uint32_t at) {
  uint32_t value;
  std::memcpy(&value, base + at, 4);
  return std::byteswap(value);
}
} // namespace
#define STUB(address)                                                          \
  REX_EXTERN(__imp__sub_##address) {                                           \
    (void)ctx;                                                                 \
    (void)base;                                                                \
  }
STUB(82FB55E8)
STUB(82366B88)
STUB(8236EA70)
STUB(823D2DF0)
STUB(823D1B10)
STUB(823D4370)
STUB(823D5918)
STUB(823D4C80)
STUB(823D5E28)
STUB(823D62B0)
STUB(823CF578)
STUB(8236E098)
STUB(8237DCA8)
STUB(8237DA98)
STUB(8237DF28)
STUB(8237E1A0)
STUB(8236E878)
STUB(829E1688)
STUB(829E1AE8)
STUB(829E17A0)
STUB(8317DDB0)
STUB(8287D0B8)
#undef STUB
REX_EXTERN(sub_8232A580) {
  reserve_called = true;
  reserve_args_valid =
      ctx.r3.u32 == 0x400 && ctx.r4.u32 == 5 && ctx.r1.u32 == 0xEF90;
  // Clobber call-volatile state to expose accidental use of the parent's
  // context.
  ctx.r1.u32 = 123;
  ctx.r13.u32 = 456;
  ctx.lr = 789;
  ctx.f1.f64 = 4.5;
  ctx.r3.u32 = fail_reserve ? 0 : get(base, 0x400 + 48);
}
int main() {
  using Trace = rex::ui::GuestFrameTiming;
  REXCVAR_SET(ssx_frame_timing_capture, std::string("diagnostic-test"));
  constexpr uint32_t frame_callers[] = {0x8237DCCC, 0x8236E8D0, 0x8237DAC4,
                                        0x8237DF4C, 0x8237E1C4};
  for (int test = 0; test < 10; ++test) {
    REXCVAR_SET(ssx_frame_token_handoff, test >= 5);
    alignas(32) std::array<uint8_t, 65536> memory{};
    auto *base = memory.data();
    put(base, 0x100 + 92, 1);
    put(base, 0x100 + 8, 0x200);
    put(base, 0x200 + 12, 0x400);
    put(base, 0x200 + 188, test == 2 ? 1 : 0);
    put(base, 0x400 + 48, 0x1000);
    put(base, 0x400 + 52, 0x1100);
    put(base, 0x1000, 0x12345678); // last written word must survive
    PPCContext ctx{};
    ctx.r3.u32 = 0x100;
    ctx.r1.u32 = 0xF000;
    ctx.r13.u32 = 0xCAFE;
    ctx.lr = test == 3 ? 0x8237DD00 : frame_callers[test >= 5 ? test - 5 : 0];
    const auto before = ctx;
    fail_reserve = test == 1;
    reserve_called = reserve_args_valid = false;
    Trace::ExchangeRender(0x1234567889ABCDEFull);
    if (test < 4)
      Trace::Get().BeginCapture();
    sub_82FB55E8(ctx, base);
    Trace::Get().FinishCapture();
    Trace::ExchangeRender(0);
    const bool expected_reserve = test < 2 || test >= 5;
    if (reserve_called != expected_reserve ||
        (reserve_called && !reserve_args_valid) ||
        std::memcmp(&ctx, &before, sizeof(ctx)) ||
        get(base, 0x1000) != 0x12345678 ||
        get(base, 0x400 + 48) != (test == 0 || test >= 5 ? 0x1014u : 0x1000u)) {
      std::cerr << "Context/reservation case " << test << " failed\n";
      return 1;
    }
    if ((test == 0 || test >= 5) &&
        (get(base, 0x1004) != 0xC0031000 ||
         rex::ui::DecodeGuestRenderMarker(
             get(base, 0x1008), get(base, 0x100C), get(base, 0x1010),
             get(base, 0x1014)) != 0x1234567889ABCDEFull ||
         get(base, 0x1018) != 0)) {
      std::cerr << "Packet payload/cursor mismatch\n";
      return 1;
    }
  }
  std::cout << "10 native frame marker hook cases passed\n";
}
