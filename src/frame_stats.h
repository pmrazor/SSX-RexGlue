// ssx - guest frame statistics shared between the present hook and the overlay.

#pragma once

#include <atomic>

namespace ssx {

// Updated from the game's present wrapper about twice a second.
inline std::atomic<float> g_guest_fps{0.0f};
inline std::atomic<float> g_guest_frame_ms_avg{0.0f};
inline std::atomic<float> g_guest_frame_ms_max{0.0f};

}  // namespace ssx
