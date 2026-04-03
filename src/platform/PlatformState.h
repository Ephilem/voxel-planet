#pragma once
#include <memory>

#include "window/Window.h"

namespace vp::platform {
struct PlatformState {
    std::unique_ptr<Window> window;
    bool closeRequested = false;
};
} // namespace vp::platform
