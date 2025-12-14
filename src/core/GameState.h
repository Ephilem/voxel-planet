#pragma once
#include <memory>

#include "resource/AssetRegistry.h"
#include "resource/ResourceSystem.h"

struct GameState {
    std::unique_ptr<ResourceSystem> resourceSystem;
    std::unique_ptr<AssetRegistry> assetRegistry;

    bool isRunning = true;
    double deltaTime = 0.0;
    double lastTime = 0.0;
};
