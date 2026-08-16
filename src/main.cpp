#include "client/ClientModule.h"
#include "core/CoreModule.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "platform/PlatformModule.h"
#include "renderer/RendererModule.h"
#include <flecs.h>
#include <iostream>

#include "client/player/player_components.h"
#include "client/world/planet/planet_client_components.h"
#include "core/debug/DebugDraw.h"
#include "core/main_components.h"
#include "core/physics/physics_components.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/rendering_components.h"
#include "renderer/world/planet/planet_rendering_components.h"

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

        ecs->system("ShutdownSystem").kind(flecs::PostFrame).run([](flecs::iter& it) {
            flecs::world world = it.world();
            auto* gameState = world.get<GameState>();
            if (gameState && !gameState->isRunning) {
                world.quit();
            }
        });

        auto worldGrid = ecs->entity("WorldGrid").set<vp::Grid>({.cellSize = 100'000.0});

        auto earth = ecs->entity("Earth")
                         .child_of(worldGrid)
                         .set<vp::Planet>({.radius = 667'544.0f})
                         .set<vp::PlanetTerrainParams>({})
                         .set<vp::Grid>({.cellSize = 10'000.0})
                         .emplace<vp::PlanetTileLodComp>()
                         .emplace<vp::PlanetTileDrawListComp>()

                         .set<vp::GlobalTransform>({})
                         .set<vp::CellCoord>({7, 0, 0})
                         .set<vp::Transform>({});

        ecs->entity("Player")
            .set<Camera3d>({})
            .set<Camera3dParameters>({.fov = 80.0f})

            .set<PlayerController>({})
            .add<Player>()
            .add<PlayerClient>()
            .add<vp::FloatingOrigin>()

            .set<RigidBody>({})
            .set<Velocity>({})

            .set<vp::CellCoord>({0, 0, 0})
            .set<vp::GlobalTransform>({})
            .set<vp::Transform>({.pos = {0.f, 667544.f + 1000.f, 0.f}})

            .child_of(earth);

        ecs->system<const vp::Grid>("GridOrigin").kind(flecs::PreStore).each([](flecs::entity e, const vp::Grid& grid) {
            glm::vec3 pos;

            if (const auto* gt = e.get<vp::GlobalTransform>()) {
                pos = gt->pos;
            } else {
                const glm::dvec3 originRender =
                    -(glm::dvec3(grid.localOrigin.cell) * grid.cellSize + glm::dvec3(grid.localOrigin.translation));
                pos = glm::vec3(glm::normalize(originRender) * 100.0);
            }
            static constexpr glm::vec4 COLORS[] = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},
                                                   {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
                                                   {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}};

            vp::DebugDraw::Point(pos, COLORS[e.id() % 6]);
        });

        ecs->system<const vp::Grid>("GridOrigin").kind(flecs::PreStore).each([](flecs::entity e, const vp::Grid& grid) {
            glm::vec3 pos;

            if (const auto* gt = e.get<vp::GlobalTransform>()) {
                pos = gt->pos;
            } else {
                const glm::dvec3 originRender =
                    -(glm::dvec3(grid.localOrigin.cell) * grid.cellSize + glm::dvec3(grid.localOrigin.translation));
                pos = glm::vec3(glm::normalize(originRender) * 100.0);
            }
            static constexpr glm::vec4 COLORS[] = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},
                                                   {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
                                                   {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}};

            vp::DebugDraw::Point(pos, COLORS[e.id() % 6]);
        });

        ecs->app().target_fps(99999).enable_stats().enable_rest().run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
