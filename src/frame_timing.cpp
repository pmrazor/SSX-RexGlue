// Native SSX scheduling diagnostics for Reflex boundary verification.
// Optional coupled Reflex pins an actual worker's two published samples to one
// rendered frame. The default interpolated rendering path remains unchanged.
#include <atomic>
#include <bit>
#include <cstring>
#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_STRING(ssx_frame_timing_capture, "", "SSX",
                      "Diagnostic directory: create capture.request for a "
                      "five-second native timing trace.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Keep the existing release SDK usable as a fallback.
#if __has_include(<rex/ui/guest_frame_timing.h>)
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/guest_frame_timing.h>
#include <rex/ui/guest_reflex.h>
namespace {
using Trace = rex::ui::GuestFrameTiming;
using Event = Trace::Event;
std::atomic<uint64_t> last_input{0}, last_worker{0};
thread_local uint64_t current_worker = 0;
rex::ui::GuestSampleLineage sample_lineage;
struct InputSnapshot {
  uint64_t update = 0;
  std::array<rex::input::X_INPUT_STATE, 4> states{};
  std::array<uint32_t, 4> results{};
};
InputSnapshot frame_input;
std::mutex frame_input_mutex;
bool SampleFrameInput(uint64_t update) {
  auto *kernel = REX_KERNEL_STATE();
  if (!kernel || !kernel->emulator()->input_system())
    return false;
  auto *input = static_cast<rex::input::InputSystem *>(
      kernel->emulator()->input_system());
  InputSnapshot next;
  next.update = update;
  for (uint32_t user = 0; user < 4; ++user)
    next.results[user] = input->GetState(user, &next.states[user]);
  std::lock_guard lock(frame_input_mutex);
  frame_input = next;
  return true;
}
bool Enabled() {
  return !REXCVAR_GET(ssx_frame_timing_capture).empty() ||
         Trace::ContinuousMarkersEnabled();
}
template <typename T> T Load(const uint8_t *base, uint32_t address) {
  T value;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}
void Store(uint8_t *base, uint32_t address, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(base + address, &value, sizeof(value));
}
// Only read source-verified objects and bounds. No scan of guest memory.
uint32_t Sample(const uint8_t *base, uint32_t channel, uint32_t index) {
  const auto count = Load<uint32_t>(base, channel + 76);
  const auto array = Load<uint32_t>(base, channel + 88);
  if (!array || count > 128 || index >= count)
    return 0;
  return Load<uint32_t>(base, array + index * 4);
}
uint64_t SampleKey(uint32_t channel, uint32_t sample) {
  return uint64_t(channel) << 32 | sample;
}
struct Scope {
  uint64_t id = 0, previous = 0;
  Event end;
  bool render, worker;
  Scope(Event begin, Event finish, uint32_t object, bool is_render = false)
      : end(finish), render(is_render), worker(begin == Event::kWorkerBegin) {
    if (!Enabled())
      return;
    auto &trace = Trace::Get();
    id = trace.NewId();
    if (render) {
      previous = Trace::ExchangeRender(id);
      trace.SetFrameGenerationScene(id, false);
    }
    if (worker) {
      previous = current_worker;
      current_worker = id;
    }
    // These are observations only; the analyzer must not assert that a render
    // consumed the latest completed worker merely because its ID appears here.
    const auto observed = render ? last_worker.load(std::memory_order_acquire)
                                 : last_input.load(std::memory_order_acquire);
    trace.Add(begin, id, observed, object);
  }
  ~Scope() {
    if (!id)
      return;
    Trace::Get().Add(end, id);
    if (end == Event::kInputEnd)
      last_input.store(id, std::memory_order_release);
    if (end == Event::kWorkerEnd)
      last_worker.store(id, std::memory_order_release);
    if (render)
      Trace::ExchangeRender(previous);
    if (worker)
      current_worker = previous;
  }
};
} // namespace

// Polling loop 82366420 invokes this every ~5 ms on a separate thread.
REX_EXTERN(__imp__sub_82366B88);
REX_HOOK_RAW(sub_82366B88) {
  const auto directory = REXCVAR_GET(ssx_frame_timing_capture);
  if (!directory.empty())
    Trace::Get().PollCapture(std::filesystem::u8path(directory));
  Scope scope(Event::kInputBegin, Event::kInputEnd, ctx.r3.u32);
  __imp__sub_82366B88(ctx, base);
}

#define SSX_TIMING_HOOK(address, begin, end, render)                           \
  REX_EXTERN(__imp__sub_##address);                                            \
  REX_HOOK_RAW(sub_##address) {                                                \
    Scope scope(Event::begin, Event::end, ctx.r3.u32, render);                 \
    __imp__sub_##address(ctx, base);                                           \
  }

SSX_TIMING_HOOK(8236EA70, kWorkerDispatchBegin, kWorkerDispatchEnd, false)
SSX_TIMING_HOOK(8236E098, kRenderDispatchBegin, kRenderDispatchEnd, true)
#undef SSX_TIMING_HOOK

// Loading and frontend rendering uses this synchronous path instead of the
// normal race job. Sleep/input still precede guest renderer/device locks.
REX_EXTERN(__imp__sub_8236E878);
REX_HOOK_RAW(sub_8236E878) {
  Scope scope(Event::kSynchronizedRenderBegin, Event::kSynchronizedRenderEnd,
              ctx.r3.u32, true);
  auto &reflex = rex::ui::GuestReflex::Get();
  // This exact guest state skips all rendering in 8236E878. Do not begin an
  // NVIDIA frame which cannot reach a Present.
  if (reflex.frame_driven() && Load<uint32_t>(base, 0x83817B78) != 9)
    reflex.BeginRenderUpdate(scope.id,
                             [&] { return SampleFrameInput(scope.id); });
  __imp__sub_8236E878(ctx, base);
}

#define SSX_SAMPLE_UPDATE_HOOK(address)                                        \
  REX_EXTERN(__imp__sub_##address);                                            \
  REX_HOOK_RAW(sub_##address) {                                                \
    rex::ui::GuestReflex::Get().BeginSamples(current_worker);                  \
    __imp__sub_##address(ctx, base);                                           \
  }
SSX_SAMPLE_UPDATE_HOOK(823D1B10)
SSX_SAMPLE_UPDATE_HOOK(823D4370)
SSX_SAMPLE_UPDATE_HOOK(823D5918)
#undef SSX_SAMPLE_UPDATE_HOOK

REX_EXTERN(__imp__sub_823D2DF0);
REX_HOOK_RAW(sub_823D2DF0) {
  Scope scope(Event::kWorkerBegin, Event::kWorkerEnd, ctx.r3.u32);
  const bool coupled = rex::ui::GuestReflex::Get().BeginWorker(scope.id);
  __imp__sub_823D2DF0(ctx, base);
  if (coupled)
    rex::ui::GuestReflex::Get().EndWorker(scope.id);
}

// Other scheduler jobs use the same controller wrapper and two-channel
// publication protocol. They execute outside renderer locks, including the
// frontend's direct dispatch on its separate update thread. CF578 publishes
// samples itself without calling the controller-processing wrapper.
#define SSX_FRONTEND_WORKER_HOOK(address, publishes_here)                      \
  REX_EXTERN(__imp__sub_##address);                                            \
  REX_HOOK_RAW(sub_##address) {                                                \
    Scope scope(Event::kWorkerBegin, Event::kWorkerEnd, ctx.r3.u32);           \
    auto &reflex = rex::ui::GuestReflex::Get();                                \
    const bool scheduled =                                                     \
        reflex.frame_driven() && reflex.BeginWorker(scope.id);                 \
    if (publishes_here)                                                        \
      reflex.BeginSamples(scope.id);                                           \
    __imp__sub_##address(ctx, base);                                           \
    if (scheduled)                                                             \
      reflex.EndWorker(scope.id);                                              \
  }
SSX_FRONTEND_WORKER_HOOK(823D4C80, false)
SSX_FRONTEND_WORKER_HOOK(823D5E28, false)
SSX_FRONTEND_WORKER_HOOK(823D62B0, false)
SSX_FRONTEND_WORKER_HOOK(823CF578, true)
#undef SSX_FRONTEND_WORKER_HOOK

REX_EXTERN(__imp__sub_8237DCA8);
REX_HOOK_RAW(sub_8237DCA8) {
  Scope scope(Event::kRenderBegin, Event::kRenderEnd, ctx.r3.u32, true);
  // The race worker 823D2DF0 reads this same manager's byte +30 and
  // calls 823D2590/823D2D40 on its pause transitions. Non-race render
  // scopes default to false. Bounds are restricted to guest virtual memory.
  const uint32_t pause_manager = Load<uint32_t>(base, 0x838AB8F0);
  const bool unpaused = pause_manager >= 0x10000u && pause_manager < 0xE0000000u &&
                        base[pause_manager + 30] == 0;
  Trace::Get().SetFrameGenerationScene(scope.id, unpaused);
  // No renderer/device locks have been taken at this entry point.
  auto &reflex = rex::ui::GuestReflex::Get();
  if (reflex.frame_driven())
    reflex.BeginRenderUpdate(scope.id,
                             [&] { return SampleFrameInput(scope.id); });
  else
    reflex.WaitForRender(scope.id);
  __imp__sub_8237DCA8(ctx, base);
}

// The world map and other frontend jobs select their own published channels.
// Their frame boundary is before the first renderer lock, like the race job.
#define SSX_FRONTEND_RENDER_HOOK(address)                                      \
  REX_EXTERN(__imp__sub_##address);                                            \
  REX_HOOK_RAW(sub_##address) {                                                \
    Scope scope(Event::kRenderBegin, Event::kRenderEnd, ctx.r3.u32, true);     \
    auto &reflex = rex::ui::GuestReflex::Get();                                \
    if (reflex.frame_driven())                                                 \
      reflex.BeginRenderUpdate(scope.id,                                       \
                               [&] { return SampleFrameInput(scope.id); });    \
    __imp__sub_##address(ctx, base);                                           \
  }
SSX_FRONTEND_RENDER_HOOK(8237DA98)
SSX_FRONTEND_RENDER_HOOK(8237DF28)
SSX_FRONTEND_RENDER_HOOK(8237E1A0)
#undef SSX_FRONTEND_RENDER_HOOK

// This helper is called with the channel's guest critical section held. The
// chosen slot is state 1 (writer owned) on return. Assign a fresh generation
// even if both the address and timestamp repeat.
REX_EXTERN(__imp__sub_829E1688);
REX_HOOK_RAW(sub_829E1688) {
  const auto channel = ctx.r3.u32;
  __imp__sub_829E1688(ctx, base);
  if (!Enabled())
    return;
  const auto slot = Sample(base, channel, Load<uint32_t>(base, channel + 72));
  if (!slot || Load<uint32_t>(base, slot + 12) != 1)
    return;
  const auto timestamp = Load<uint64_t>(base, channel + 56);
  const auto id = Trace::Get().NewId(), key = SampleKey(channel, slot);
  if (!sample_lineage.BeginWrite(key, timestamp, id))
    return;
  Trace::Get().Add(Event::kSampleWrite, id, current_worker, key);
  Trace::Get().Add(Event::kSampleTime, id, timestamp);
}

REX_EXTERN(__imp__sub_829E1AE8);
REX_HOOK_RAW(sub_829E1AE8) {
  uint64_t id = 0, key = 0;
  if (Enabled()) {
    const auto channel = ctx.r3.u32;
    const auto slot = Sample(base, channel, Load<uint32_t>(base, channel + 72));
    // Announce before releasing the slot to the renderer; recording only after
    // the original call races selection on another thread. The end event is
    // separate and may legitimately follow a selection event.
    if (slot && Load<uint32_t>(base, slot + 12) == 1) {
      key = SampleKey(channel, slot);
      id = sample_lineage.Publish(key, Load<uint64_t>(base, channel + 56));
      Trace::Get().Add(Event::kSamplePublishBegin, id, current_worker, key);
    }
  }
  __imp__sub_829E1AE8(ctx, base);
  if (id) {
    Trace::Get().Add(Event::kSamplePublishEnd, id, current_worker, key);
    rex::ui::GuestReflex::Get().Published(
        current_worker, {key, id, Load<uint64_t>(base, uint32_t(key))});
  }
}

REX_EXTERN(__imp__sub_829E17A0);
REX_HOOK_RAW(sub_829E17A0) {
  const auto channel = ctx.r3.u32;
  rex::ui::GuestReflex::Sample expected;
  const auto render = Trace::CurrentRender();
  const bool coupled =
      rex::ui::GuestReflex::Get().Selection(render, channel, expected);
  if (coupled)
    ctx.f1.f64 = std::bit_cast<double>(expected.timestamp);
  __imp__sub_829E17A0(ctx, base);
  if (!Enabled())
    return;
  if (coupled) {
    const auto slot = Sample(base, channel, Load<uint32_t>(base, channel + 52));
    const auto key = SampleKey(channel, slot);
    if (!slot || key != expected.key || Load<uint32_t>(base, slot + 12) != 0 ||
        sample_lineage.Selected(key, Load<uint64_t>(base, slot)) !=
            expected.generation)
      rex::ui::GuestReflex::Get().Abort(render);
  }
  Trace::Get().Add(Event::kSampleInterpolation, render,
                   Load<uint32_t>(base, channel + 36),
                   uint64_t(channel) << 32 | ctx.r3.u32);
  // Successful selections are pinned in state 0 until 829E1A60. Reading them
  // here does not compete with the simulation writer or change guest locking.
  for (const auto offset : {52u, 48u}) {
    const auto slot =
        Sample(base, channel, Load<uint32_t>(base, channel + offset));
    if (!slot)
      continue;
    const auto key = SampleKey(channel, slot);
    const auto generation =
        Load<uint32_t>(base, slot + 12) == 0
            ? sample_lineage.Selected(key, Load<uint64_t>(base, slot))
            : 0;
    Trace::Get().Add(offset == 52 ? Event::kSampleSelectPrevious
                                  : Event::kSampleSelectNext,
                     render, generation, key);
  }
}

// Worker-side controller update, distinct from the independent 5 ms polling
// loop. A zero related ID deliberately reveals calls outside the known worker.
REX_EXTERN(__imp__sub_8317DDB0);
REX_HOOK_RAW(sub_8317DDB0) {
  rex::ui::GuestReflex::Get().Input(current_worker);
  const auto id = Enabled() ? Trace::Get().NewId() : 0;
  if (id)
    Trace::Get().Add(Event::kWorkerInputBegin, id, current_worker);
  __imp__sub_8317DDB0(ctx, base);
  if (id)
    Trace::Get().Add(Event::kWorkerInputEnd, id, current_worker);
}

// Sole gamepad-state wrapper used by the worker controller update. In the
// render-driven experiment it consumes that update's immutable fresh snapshot.
// Independent 5 ms polling and all alternate callers retain their original API.
REX_EXTERN(__imp__sub_8287D0B8);
REX_HOOK_RAW(sub_8287D0B8) {
  const auto update =
      rex::ui::GuestReflex::Get().InputUpdateForWorker(current_worker);
  const auto user = ctx.r3.u32, flags = ctx.r4.u32, output = ctx.r5.u32;
  // 8317ACD8 asks for gamepads (1), then other devices (8). XAM rejects
  // the latter before reading input. Keep that original rejection path.
  if (update && user < 4 && flags == 1) {
    bool consumed = false;
    {
      std::lock_guard lock(frame_input_mutex);
      if (frame_input.update == update) {
        if (output && frame_input.results[user] == 0)
          std::memcpy(base + output, &frame_input.states[user],
                      sizeof(frame_input.states[user]));
        ctx.r3.u64 = frame_input.results[user];
        consumed = true;
      }
    }
    if (consumed) {
      rex::ui::GuestReflex::Get().ConsumedInput(current_worker, update, user);
      return;
    }
  }
  __imp__sub_8287D0B8(ctx, base);
}

REX_EXTERN(__imp__sub_82FB55E8);
REX_EXTERN(sub_8232A580);
REX_HOOK_RAW(sub_82FB55E8) {
  const auto renderer = ctx.r3.u32, caller = uint32_t(ctx.lr);
  __imp__sub_82FB55E8(ctx, base);
  const auto id = Trace::CurrentRender();
  const bool frame_boundary = caller == 0x8237DCCC || caller == 0x8236E8D0 ||
                              caller == 0x8237DAC4 || caller == 0x8237DF4C ||
                              caller == 0x8237E1C4;
  if (!frame_boundary || !id ||
      (!Trace::Get().capturing() && !Trace::ContinuousMarkersEnabled()))
    return;
  // These exact frame-boundary calls own the renderer and immediate device
  // locks. Resolve the device exactly as 82FB55E8 does, never from a last-used
  // global. Deferred command recording is deliberately excluded.
  if (Load<uint32_t>(base, renderer + 92) != 1) {
    Trace::Get().Add(Event::kGuestRenderMarkerRejected, id, 1);
    return;
  }
  const auto wrapper = Load<uint32_t>(base, renderer + 8);
  if (!wrapper || Load<uint32_t>(base, wrapper + 188) != 0) {
    Trace::Get().Add(Event::kGuestRenderMarkerRejected, id, 2);
    return;
  }
  const auto device = Load<uint32_t>(base, wrapper + 12);
  if (!device) {
    Trace::Get().Add(Event::kGuestRenderMarkerRejected, id, 3);
    return;
  }
  // The allocator can refill/flush through guest code. Isolate its registers
  // and stack scratch from the caller, retaining all original returned state.
  PPCContext reserve{};
  reserve.r1.u32 = ctx.r1.u32 - 0x70;
  reserve.r13 = ctx.r13;
  reserve.fpscr = ctx.fpscr;
  reserve.r3.u32 = device;
  reserve.r4.u32 = 5; // header + four payload words
  sub_8232A580(reserve, base);
  const auto cursor = reserve.r3.u32;
  if (!cursor || (cursor & 3) || cursor > UINT32_MAX - 20 ||
      cursor != Load<uint32_t>(base, device + 48) ||
      cursor + 20 > Load<uint32_t>(base, device + 52)) {
    Trace::Get().Add(Event::kGuestRenderMarkerRejected, id, 4, device);
    return;
  }
  // Reservation returns the LAST written word, not the first free one.
  Store(base, cursor + 4, 0xC0031000); // PM4 type 3, NOP, four payload words
  Store(base, cursor + 8, rex::ui::kGuestRenderMarkerSignature);
  Store(base, cursor + 12, rex::ui::kGuestRenderMarkerVersion);
  Store(base, cursor + 16, uint32_t(id));
  Store(base, cursor + 20, uint32_t(id >> 32));
  Store(base, device + 48, cursor + 20);
  Trace::Get().Add(Event::kGuestRenderMarker, id, device, cursor);
}
#endif
