#pragma once
#include <memory>

#include "resource/AssetRegistry.h"
#include "resource/ResourceSystem.h"

namespace vp::core {
struct GameState {
    std::unique_ptr<ResourceSystem> resourceSystem;
    std::unique_ptr<AssetRegistry> assetRegistry;

    bool isRunning = true;
};
} // namespace vp::core
