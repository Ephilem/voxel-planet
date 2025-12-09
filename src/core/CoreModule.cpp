#include "CoreModule.h"
#include "GameState.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "main_components.h"
#include "log/Logger.h"
#include "world/world_components.h"

CoreModule::CoreModule(flecs::world& ecs) {

    ecs.component<VoxelChunk>();
    ecs.component<Position>();

    ecs.set<GameState>({
        .resourceSystem = std::make_unique<ResourceSystem>(),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });

    // test chunk
    VoxelChunk chunk = {
        .data = {0}
    };

    // generate a flat chunk
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                chunk.data[x][y][z] = std::rand() % 2;
            }
        }
    }

    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 1; ++y) {
            for (int z = 0; z < 10; ++z) {
                std::string entityName = "TestChunk_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
                ecs.entity(entityName.c_str())
                    .set<Position>({static_cast<float>(x * CHUNK_SIZE), static_cast<float>(y * CHUNK_SIZE), static_cast<float>(z * CHUNK_SIZE)})
                    .set<VoxelChunk>(chunk);
            }
        }
    }

}

CoreModule::~CoreModule() = default;

void shutdown_core(flecs::world& ecs) {
    LOG_INFO("CoreModule", "Shutting down...");
    auto* gameState = ecs.get_mut<GameState>();
    if (gameState && gameState->resourceSystem) {
        gameState->resourceSystem.reset();
    }
}
