#include "CoreModule.h"
#include "GameState.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "log/Logger.h"

CoreModule::CoreModule(flecs::world& ecs) {
    ecs.set<GameState>({
        .resourceSystem = std::make_unique<ResourceSystem>(),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });

    ecs.system<GameState>("CoreShutdownSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::iter& it, size_t, GameState& gameState) {
            if (!gameState.isRunning) {
                LOG_INFO("CoreModule", "Shutdown resquest has been received, quitting...");
                it.world().quit();
            }
        });
}

CoreModule::~CoreModule() = default;

void shutdown_core(flecs::world& ecs) {
    LOG_INFO("CoreModule", "Shutting down...");
    auto* gameState = ecs.get_mut<GameState>();
    if (gameState && gameState->resourceSystem) {
        gameState->resourceSystem.reset();
    }
}
