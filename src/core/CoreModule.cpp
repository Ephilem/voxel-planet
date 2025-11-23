#include "CoreModule.h"
#include "GameState.h"
#include <GLFW/glfw3.h>
#include <iostream>

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
                std::cout << "CoreModule: Shutdown requested, quitting ECS world..." << std::endl;
                it.world().quit();
            }
        });

    ecs.system<GameState>("FPSLogSystem")
        .kind(flecs::PostUpdate)
        .interval(1.0f)
        .each([](flecs::iter& it, size_t, GameState& gameState) {
            double fps = 1.0 / gameState.deltaTime;
            std::cout << "FPS: " << static_cast<int>(fps) << ", "
                      << "Delta: " << static_cast<int>(gameState.deltaTime * 1000) << "ms" << std::endl;
        });
}

CoreModule::~CoreModule() = default;

void shutdown_core(flecs::world& ecs) {
    std::cout << "CoreModule: Shutting down..." << std::endl;
    auto* gameState = ecs.get_mut<GameState>();
    if (gameState && gameState->resourceSystem) {
        std::cout << "CoreModule: Shutting down resource system..." << std::endl;
        gameState->resourceSystem.reset();
    }
}
