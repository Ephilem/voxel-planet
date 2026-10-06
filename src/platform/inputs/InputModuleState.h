#pragma once
#include <memory>

#include "InputStateManager.h"

namespace vp::platform {
struct InputModuleState {
    std::unique_ptr<InputStateManager> inputManager;
};
} // namespace vp::platform
