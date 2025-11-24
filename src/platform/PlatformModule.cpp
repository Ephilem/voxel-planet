#include "PlatformModule.h"
#include "PlatformState.h"
#include "events.h"
#include "core/GameState.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "core/log/Logger.h"

PlatformModule::PlatformModule(flecs::world& ecs) {
    auto* gameState = ecs.get<GameState>();
    if (!gameState) {
        throw std::runtime_error("PlatformModule: CoreModule must be imported before PlatformModule");
    }

    ecs.component<WindowResizeEvent>();

    ecs.set<PlatformState>({
        .window = std::make_unique<Window>(1280, 720, "VoxelPlanet")
    });

    ecs.system("PlatformUpdateSystem")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            auto* gameState = it.world().get_mut<GameState>();
            auto* platform = it.world().get_mut<PlatformState>();
            if (!gameState || !platform || !platform->window) return;

            platform->window->pollEvents();

            double currentTime = glfwGetTime();
            gameState->deltaTime = currentTime - gameState->lastTime;
            gameState->lastTime = currentTime;

            if (platform->window->shouldClose()) {
                gameState->isRunning = false;
            }

            if (glfwGetKey(platform->window->window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                std::cout << "ESC pressed, initiating shutdown..." << std::endl;
                gameState->isRunning = false;
            }
        });

    ecs.get_mut<PlatformState>()->window->setupCallbacks(ecs);
}

void shutdown_platform(flecs::world& ecs) {
    LOG_INFO("PlatformModule", "Shutting down...");
    auto* platform = ecs.get_mut<PlatformState>();
    if (platform && platform->window) {
        platform->window.reset();
    }
}
