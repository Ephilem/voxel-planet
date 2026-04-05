#pragma once
#include <memory>

#include "window/Window.h"

struct PlatformState {
    std::unique_ptr<Window> window;
};
