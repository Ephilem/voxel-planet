#include "PlatformModule.h"

#include "events.h"
#include "inputs/InputModule.h"
#include "PlatformState.h"
#include <GLFW/glfw3.h>

#include "core/log/Logger.h"
#include "core/TracyIntegration.h"

namespace vp::platform {
    void PlatformModule::init(flecs::world& ecs) {
        auto* platformState = ecs.try_get_mut<PlatformState>();
        platformState->window->setupCallbacks(ecs);
    }

    void PlatformModule::register_components(flecs::world& ecs) {
        ecs.component<WindowResizeEvent>();
        ecs.component<PlatformState>().add(flecs::Singleton);

        ecs.set<PlatformState>({.window = std::make_unique<Window>(1280, 720, "VoxelPlanet")});
    }

    void PlatformModule::register_systems(flecs::world& ecs) {
        ecs.system("PlatformModule-Update").kind(flecs::PreUpdate).run([](flecs::iter& it) {
            auto* platform = it.world().try_get_mut<PlatformState>();

            if (!platform || !platform->window) {
                return;
            }

            platform->window->pollEvents();

            if (platform->window->shouldClose()) {
                platform->closeRequested = true;
            }
        });
    }

    void PlatformModule::register_pipelines(flecs::world& ecs) {
    }

    void PlatformModule::register_submodules(flecs::world& ecs) {
        ecs.import<InputModule>();
    }

    void PlatformModule::register_entities(flecs::world& ecs) {
    }
}