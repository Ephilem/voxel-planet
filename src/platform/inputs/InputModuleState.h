#pragma once
#include <memory>

#include "InputStateManager.h"

struct InputModuleState {
    std::unique_ptr<InputStateManager> inputManager;
};
