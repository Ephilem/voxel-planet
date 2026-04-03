#pragma once
#include <cstdint>

namespace vp::platform {
struct WindowResizeEvent {
    uint32_t width;
    uint32_t height;
};
} // namespace vp::platform
