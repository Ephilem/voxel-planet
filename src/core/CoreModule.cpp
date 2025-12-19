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

    auto assetRegistry = std::make_unique<AssetRegistry>();

    ecs.set<GameState>({
        .resourceSystem = std::make_unique<ResourceSystem>(assetRegistry.get()),
        .assetRegistry = std::move(assetRegistry),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });

    for (int x = 0; x < 30; ++x) {
        for (int y = 0; y < 1; ++y) {
            for (int z = 0; z < 30; ++z) {
                // test chunk
                VoxelChunk chunk = {
                    .data = {0},
                    .textureIDs = {
                        {"voxelplanet:textures/grass"_asset, 1},
                        {"voxelplanet:textures/cobblestone"_asset, 2}
                    }
                };

                for (int x1 = 0; x1 < CHUNK_SIZE; ++x1) {
                    for (int y1 = 0; y1 < CHUNK_SIZE; ++y1) {
                        for (int z1 = 0; z1 < CHUNK_SIZE; ++z1) {
                            // if (y1 == y) {
                            //     chunk.data[x1][y1][z1] = 1;
                            // } else {
                            //     chunk.data[x1][y1][z1] = 0;
                            // }
                            chunk.data[x1][y1][z1] = (y1 == 0) ? std::rand()%2+1 : 0;
                            // chunk.data[x1][y1][z1] = (y1 == 0) ? 1 : 0;
                        }
                    }
                }

                std::string entityName = "TestChunk_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
                ecs.entity(entityName.c_str())
                    .set<Position>({
                        static_cast<float>(x * CHUNK_SIZE), static_cast<float>(y * CHUNK_SIZE), static_cast<float>(z * CHUNK_SIZE)
                    })
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
