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
#include "core/world/planet/planet_transform.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/rendering_components.h"
#include "renderer/world/planet/planet_rendering_components.h"

int main() {
    tracy_integration::InstallFlecsHooks();
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
            auto* gameState = world.try_get<GameState>();
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
            .set<vp::SurfaceAligned>({})
            .set<vp::LookAngles>({})

            .set<vp::PlanetChunkLoader>({.loadingDistance = 10, .unloadingDistance = 12, .altitudeDistance = 5})

            .child_of(earth);

        ecs->system<const vp::Grid>("GridOrigin").kind(flecs::PreStore).each([](flecs::entity e, const vp::Grid& grid) {
            glm::vec3 pos;

            if (const auto* gt = e.try_get<vp::GlobalTransform>()) {
                pos = glm::vec3(gt->pos);
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

        ecs->system<const vp::CellCoord, const vp::Transform, const vp::GlobalTransform, const vp::Grid,
                    const vp::Planet, const Player>("DrawCurrentChunkOutline")
            .term_at(2)
            .parent()
            .term_at(3)
            .parent()
            .term_at(4)
            .parent()
            .kind(flecs::PreStore)
            .each([](flecs::entity, const vp::CellCoord& cell, const vp::Transform& tr,
                     const vp::GlobalTransform& planetGt, const vp::Grid& grid, const vp::Planet& planet,
                     const Player&) {
                constexpr uint8_t level = 0;

                // Position du joueur dans l'espace planète (même calcul que RequestChunks)
                const glm::dvec3 posPlanet = grid.get_hp_grid_pos(cell, tr.pos);
                const vp::PlanetVoxelCoord vc = vp::planet_pos_to_voxel(posPlanet, planet.radius, level);

                // Origin of the chunk in voxel space (floor division, works for negative z too)
                const int64_t cs = CHUNK_SIZE; // adapte au nom de ta constante
                auto floorDiv = [](int64_t a, int64_t b) {
                    int64_t q = a / b;
                    if ((a % b != 0) && ((a < 0) != (b < 0)))
                        --q;
                    return q;
                };
                const double x0 = double(floorDiv(vc.voxel.x, cs) * cs), x1 = x0 + double(cs);
                const double y0 = double(floorDiv(vc.voxel.y, cs) * cs), y1 = y0 + double(cs);
                const double z0 = double(floorDiv(vc.voxel.z, cs) * cs), z1 = z0 + double(cs);

                auto toRender = [&](const glm::dvec3& v) {
                    const glm::dvec3 p = vp::planet_voxel_to_pos(vc.face, v.x, v.y, v.z, planet.radius, level);
                    return glm::vec3(planetGt.pos + p); // la soustraction se fait en double, puis cast
                };

                const glm::vec4 color{1.0f, 1.0f, 0.0f, 1.0f};
                constexpr int SEGMENTS = 16;

                // Draws one edge between two voxel-space points, subdivided because
                // the "box" is curved on the sphere
                auto edge = [&](const glm::dvec3& a, const glm::dvec3& b) {
                    glm::vec3 prev = toRender(a);
                    for (int i = 1; i <= SEGMENTS; ++i) {
                        const glm::vec3 cur = toRender(glm::mix(a, b, double(i) / SEGMENTS));
                        vp::DebugDraw::Line(prev, cur, color);
                        prev = cur;
                    }
                };

                const glm::dvec3 c[8] = {
                    {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, // bottom
                    {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, // top
                };
                for (int i = 0; i < 4; ++i) {
                    edge(c[i], c[(i + 1) % 4]);         // bottom ring
                    edge(c[i + 4], c[(i + 1) % 4 + 4]); // top ring
                    edge(c[i], c[i + 4]);               // verticals
                }
            });

        ecs->app().target_fps(99999).enable_stats().enable_rest().run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
