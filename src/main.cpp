#include "core/CoreModule.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "platform/PlatformModule.h"
#include "renderer/RendererModule.h"
#include "client/ClientModule.h"
#include <flecs.h>
#include <iostream>

#include "client/player/player_components.h"
#include "core/main_components.h"
#include "core/debug/DebugDraw.h"
#include "core/physics/physics_components.h"
#include "core/world/world_components.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/rendering_components.h"

int main() {
    try {
        auto ecs = std::make_unique<flecs::world>();

        ecs->import<vp::CoreModule>();
        ecs->import<vp::PlatformModule>();
        ecs->import<vp::RendererModule>();
        ecs->import<vp::ClientModule>();
        ecs->import<flecs::stats>();
        ecs->set<flecs::Rest>({});

        tracy_integration::Register(*ecs);

        ecs->system("ShutdownSystem")
            .kind(flecs::PostFrame)
            .run([](flecs::iter& it) {
                flecs::world world = it.world();
                auto* gameState = world.get<GameState>();
                if (gameState && !gameState->isRunning) {
                    // shutdown_client(world);
                    // vp::shutdown_renderer(world);
                    // shutdown_platform(world);
                    // vp::shutdown_core(world);
                    world.quit();
                }
            });

        auto worldGrid = ecs->entity("WorldGrid")
            .set<Grid>({ .cellSize = 100'000.0 });

        auto earth = ecs->entity("Earth")
            .child_of(worldGrid)
            .set<vp::PlanetComp>({ .radius = 637100.0f })
            .set<vp::PlanetGenerationConfig>({ .radius = 637100.0f})
            .set<Grid>({ .cellSize = 10'000.0 })
            .set<PlanetRenderComp>({})
            .set<GlobalTransform>({})
            .set<CellCoord>({35, 0, 0})
            .set<Transform>({});

        ecs->entity("Player")
            .set<Camera3d>({})
            .set<Camera3dParameters>({
                .fov = 80.0f
            })

            .set<PlayerController>({})
            .add<Player>()
            .add<FloatingOrigin>()

            .set<RigidBody>({})
            .set<Velocity>({})

            .set<CellCoord>({0, 0, 0})
            .set<GlobalTransform>({})
            .set<Transform>({})

            .set<ChunkLoader>({})

            .child_of(earth);

        ecs->app()
          .target_fps(99999)
          .enable_stats()
          .enable_rest()
          .run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
