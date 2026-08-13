#include "PlatformModule.h"

#include "core/GameState.h"
#include "events.h"
#include "inputs/InputModule.h"
#include "PlatformState.h"
#include <GLFW/glfw3.h>

#include "core/log/Logger.h"
#include "core/TracyIntegration.h"

using namespace vp;

void PlatformModule::init(flecs::world& ecs) {
    ecs.get_mut<PlatformState>()->window->setupCallbacks(ecs);
}

void PlatformModule::register_components(flecs::world& ecs) {
    auto* gameState = ecs.get<GameState>();
    if (!gameState) {
        throw std::runtime_error("PlatformModule: CoreModule must be imported before PlatformModule");
    }

    ecs.component<WindowResizeEvent>();
    ecs.component<PlatformState>();

    ecs.set<PlatformState>({.window = std::make_unique<Window>(1280, 720, "VoxelPlanet")});
}

void PlatformModule::register_systems(flecs::world& ecs) {
    ecs.system("PlatformModule-Update").kind(flecs::PreUpdate).run([](flecs::iter& it) {
        VOXEL_ZONE_N("PlatformModule-Update");
        auto* gameState = it.world().get_mut<GameState>();
        auto* platform = it.world().get_mut<PlatformState>();
        if (!gameState || !platform || !platform->window)
            return;

        platform->window->pollEvents();

        double currentTime = glfwGetTime();
        gameState->deltaTime = currentTime - gameState->lastTime;
        gameState->lastTime = currentTime;

        if (platform->window->shouldClose()) {
            gameState->isRunning = false;
        }
    });
}

void PlatformModule::register_pipelines(flecs::world& ecs) {}

void PlatformModule::register_submodules(flecs::world& ecs) {
    ecs.import<InputModule>();
}

void PlatformModule::register_entities(flecs::world& ecs) {}
