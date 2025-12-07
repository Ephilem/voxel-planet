#pragma once
#include <memory>

#include "inputs/InputStateManager.h"
#include "window/Window.h"

struct PlatformState {
    std::unique_ptr<InputStateManager> inputManager;
    std::unique_ptr<Window> window;
};
