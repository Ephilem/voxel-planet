#include "CoreModule.h"
#include "GameState.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "main_components.h"
#include "log/Logger.h"
#include "world/ChunkManager.h"
#include "world/world_components.h"
#include "world/WorldGenerator.h"

CoreModule::CoreModule(flecs::world& ecs) {

    ecs.component<VoxelChunk>();
    ecs.component<Position>();
    ecs.component<ChunkCoordinate>();

    auto assetRegistry = std::make_unique<AssetRegistry>();

    ecs.set<GameState>({
        .resourceSystem = std::make_unique<ResourceSystem>(assetRegistry.get()),
        .assetRegistry = std::move(assetRegistry),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });

    ecs.set<WorldGenerator>(WorldGenerator{12345});
    ChunkManager::Register(ecs);
}

CoreModule::~CoreModule() = default;

void shutdown_core(flecs::world& ecs) {
    LOG_INFO("CoreModule", "Shutting down...");
    auto* gameState = ecs.get_mut<GameState>();
    if (gameState && gameState->resourceSystem) {
        gameState->resourceSystem.reset();
    }
}
