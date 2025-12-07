#include "core/CoreModule.h"
#include "core/GameState.h"
#include "platform/PlatformModule.h"
#include "renderer/RendererModule.h"
#include <flecs.h>
#include <iostream>

int main() {
    try {
        auto ecs = std::make_unique<flecs::world>();

        ecs->import<CoreModule>();
        ecs->import<PlatformModule>();
        ecs->import<RendererModule>();
        ecs->import<flecs::stats>();
        ecs->set<flecs::Rest>({});

        ecs->system("ShutdownSystem")
            .kind(flecs::PostFrame)
            .run([](flecs::iter& it) {
                flecs::world world = it.world();
                auto* gameState = world.get<GameState>();
                if (gameState && !gameState->isRunning) {
                    shutdown_renderer(world);
                    shutdown_platform(world);
                    shutdown_core(world);
                    world.quit();
                }
            });

        ecs->app()
          .enable_stats()
          .enable_rest()
          .run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
