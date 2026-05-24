#include "CoreModule.h"

#include <GLFW/glfw3.h>

#include "GameState.h"
#include "main_components.h"
#include "debug/DebugDrawModule.h"
#include "log/Logger.h"
#include "world/planet/PlanetModule.h"
#include "world/spatial/SpatialModule.h"
#include "world/spatial/spatial_components.h"

using namespace vp;

void CoreModule::register_components(flecs::world &ecs) {
    ecs.component<Player>();


    auto assetRegistry = std::make_unique<AssetRegistry>();
    ecs.component<GameState>().set<GameState>({
        .resourceSystem = std::make_unique<ResourceSystem>(assetRegistry.get()),
        .assetRegistry = std::move(assetRegistry),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });
}

void CoreModule::register_systems(flecs::world &ecs) {
}

void CoreModule::register_pipelines(flecs::world &ecs) {
}

void CoreModule::register_submodules(flecs::world &ecs) {
    ecs.import<SpatialModule>();
    ecs.import<DebugDrawModule>();
    ecs.import<PlanetModule>();
}

void CoreModule::register_entities(flecs::world &ecs) {
    ecs.entity("Planet")
        .set<Transform>({});
}

// void vp::shutdown_core(flecs::world& ecs) {
//     LOG_INFO("Core", "Shutting down...");
//     auto* gameState = ecs.get_mut<GameState>();
//     if (gameState && gameState->resourceSystem) {
//         gameState->resourceSystem.reset();
//     }
// }
