#pragma once
#include <memory>

#include "resource/ResourceSystem.h"

struct GameState {
    std::unique_ptr<ResourceSystem> resourceSystem;

    bool isRunning = true;
    double deltaTime = 0.0;
    double lastTime = 0.0;
};
