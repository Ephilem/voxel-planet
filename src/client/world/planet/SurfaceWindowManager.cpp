//
// Created by raph on 14/05/2026.
//

#include "SurfaceWindowManager.h"

#include <imgui.h>

#include "planet_client_components.h"
#include "client/player/player_components.h"
#include "core/main_components.h"
#include "core/debug/DebugDraw.h"
#include "core/log/Logger.h"
#include "core/world/planet/PlanetChunkGenerator.h"
#include "core/world/planet/planet_utils.h"
#include "core/world/spatial/spatial_utils.h"
#include "renderer/rendering_components.h"

using namespace vp;

void SurfaceWindowManager::Register(flecs::world &ecs) {
    ecs.set<SurfaceWindowManager>({});
    auto* swm = ecs.get_mut<SurfaceWindowManager>();
    swm->init(ecs);
}

void SurfaceWindowManager::init(flecs::world &ecs) {
    m_generator = std::make_unique<PlanetChunkGenerator>();

    // Iter on planet and compare with PlayerClient
    ecs.system<const Transform, const CellCoord>("SWM-DetectPlanetaryEntries")
        .kind(flecs::OnUpdate)
        .with<PlayerClient>()
        .each([this](flecs::entity playerE, const Transform& playerTrs, const CellCoord& playerCell)
        {
            auto query = playerE.world().query_builder<const GlobalTransform>()
                            .with<PlanetComp>()
                            .build();

            query.each([&](flecs::iter& it, size_t i, const GlobalTransform& globalTrs)
            {
                flecs::entity planetE = it.entity(i);
                // We can use the globalTrs for the distance considering the player is always at 0,0,0 camera space
                double dist = glm::length(globalTrs.pos);
                double surfaceThreshold = planetE.get<PlanetComp>()->radius * 1.02;

                bool inSurface = dist < surfaceThreshold;
                bool hasTag = playerE.has<InPlanetSurface>(planetE);

                if (inSurface && !hasTag) {
                    playerE.add<InPlanetSurface>(planetE);
                    it.end();
                } else if (!inSurface && hasTag) {
                    playerE.remove<InPlanetSurface>(planetE);
                    it.end();
                }
            });
        });

    ecs.system("SWM-PollChunkGeneration")
        .kind(flecs::OnUpdate)
        .run([this](flecs::iter& it) {
            system_poll_chunk_generation();
        });

    ecs.observer("SWM-PlayerEnteredSurface")
        .event(flecs::OnAdd)
        .with<InPlanetSurface>(flecs::Wildcard)
        .each([this](flecs::iter& it, size_t i) {
            auto player= it.entity(i);
            auto planet = it.pair(0).second();
            on_player_entered_surface(player, planet);
        });

    ecs.observer("SWM-PlayerExitedSurface")
        .event(flecs::OnRemove)
        .with<InPlanetSurface>(flecs::Wildcard)
        .each([this](flecs::iter& it, size_t i) {
            auto player= it.entity(i);
            auto planet = it.pair(0).second();
            on_player_exited_surface(player, planet);
        });

    ecs.system<const SurfaceAnchorComp, const GlobalTransform>("SWM-DebugVizAnchor")
        .kind(flecs::OnUpdate)
        .each([this](flecs::entity anchor, const SurfaceAnchorComp& a, const GlobalTransform& gTrs) {
            // Debug draw the anchor frame
            glm::dvec3 center = gTrs.pos;
            glm::vec3 right = a.anchorRight;
            glm::vec3 up = a.anchorUp;
            glm::vec3 forward = a.anchorForward;

            const GlobalTransform* planetCameraPos = anchor.parent().get<GlobalTransform>();
            center = center + glm::dvec3(planetCameraPos->pos);
            DebugDraw::Arrow(center, right, 10.0, {1, 0, 0, 1});
            DebugDraw::Arrow(center, up, 10.0, {1, 0, 0, 1});
            DebugDraw::Arrow(center, forward, 10.0, {1, 0, 0, 1});


            ImGui::Begin("Surface Anchor Debug");
            // pos window at center
            ImGui::SetWindowPos(ImVec2(100, 400));
            ImGui::Text("Face: %d", static_cast<int>(a.face));
            ImGui::Text("Grid Center: (%d, %d)", a.gridCenter.x, a.gridCenter.y);
            ImGui::Text("Grid Origin: (%d, %d)", a.gridOrigin.x, a.gridOrigin.y);
            ImGui::End();

        });
}

void SurfaceWindowManager::on_player_entered_surface(flecs::entity player, flecs::entity planet) {
    LOG_TRACE("SurfaceWindowManager", "Player {} entered surface of planet {}", player.id(), planet.name().c_str());

    // init the surface window anchor
    // TODO Here we conisdering that the player is already children of the planet, and so in the correct grid
    const Transform* playerTrs = player.get<Transform>();
    const CellCoord* playerCellCrd = player.get<CellCoord>();

    initialize_anchor(planet, {
        .cell = *playerCellCrd,
        .transform = *playerTrs,
    });
}

void SurfaceWindowManager::on_player_exited_surface(flecs::entity player, flecs::entity planet) {
    LOG_TRACE("SurfaceWindowManager", "Player {} exited surface of planet {}", player.id(), planet.name().c_str());

    // no need of the anchor, we remove this
    // get the surface anchor in planet childrens
    const auto query = planet.world().query_builder<const SurfaceAnchorComp>()
                    .with(flecs::ChildOf, planet)
                    .build();

    query.each([&](flecs::iter& it, size_t i, const SurfaceAnchorComp&) {
        auto anchor = it.entity(i);
        anchor.destruct();
    });
}

void SurfaceWindowManager::system_update_surface_window(flecs::entity anchor, SurfaceAnchorComp a, const Transform &playerTrs, const CellCoord &playerCell) {

}

void SurfaceWindowManager::system_poll_chunk_generation() {
    std::vector<ChunkGenOutput> results = m_generator->poll_results(999);

    for (const auto & result : results) {
        if (result.empty) continue;
        flecs::entity chunkEntity = result.chunkEntity;
        if (!chunkEntity.is_alive() || !chunkEntity.is_valid()) {
            LOG_WARN("SurfaceWindowManager", "Received chunk generation result for invalid chunk entity {}", chunkEntity.id());
            continue;
        }

        const SurfaceChunkCoord* chunkCoord = chunkEntity.get<SurfaceChunkCoord>();
        if (*chunkCoord != result.coord) continue;

        chunkEntity
            .set<VoxelChunk>(std::move(result.chunk))
            .set<VoxelChunkMesh>({})
            .add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
    }
}

void SurfaceWindowManager::initialize_anchor(flecs::entity planet, SpatialCoordinate playerCoord) {
    const Grid* planetGrid = planet.get<Grid>();
    if (!planetGrid) {
        LOG_ERROR("SurfaceWindowManager", "Cannot initialize surface anchor for planet {}: no Grid component found", planet.name().c_str());
        return;
    }
    const glm::dvec3 playerPos = planetGrid->get_hp_grid_pos(playerCoord);
    const PlanetComp* planetComp = planet.get<PlanetComp>();
    const glm::dvec3 anchorPlanetPos = glm::normalize(playerPos) * glm::dvec3(planetComp->radius);
    const SpatialCoordinate anchorCoord = planetGrid->get_grid_spatial_coord(anchorPlanetPos);

    SurfaceAnchorComp a {
        .gridCenter = {0, 0},
        .gridOrigin = {0, 0},
    };
    build_anchor_frame(anchorPlanetPos,a.anchorRight, a.anchorUp, a.anchorForward, a.face);

    flecs::entity anchor = planet.world().entity("SurfaceAnchor")
        .child_of(planet)
        .set<SurfaceAnchorComp>(a)
        .set<CellCoord>(anchorCoord.cell)
        .set<Transform>(anchorCoord.transform)
        .add<GlobalTransform>();
    const auto* planetInfo = planet.get<PlanetComp>();
    constexpr int windowRadius  = 16;

    // get anchor coordinate on the face surface
    glm::dvec2 normalizedUv = dir_to_face_uv(a.face, anchorPlanetPos) * glm::dvec2(planetComp->radius);
    glm::ivec2 uvPos = glm::floor(normalizedUv / double(CHUNK_SIZE));

    a.gridCenter = uvPos;

    std::vector<ChunkGenInput> batchEnqueues;
    batchEnqueues.reserve((windowRadius * 2 + 1) * (windowRadius * 2 + 1) * 1);
    for (int alt = 0; alt <= 0; alt++) {
        for (int u = -windowRadius; u <= windowRadius; u++) {
            for (int v = -windowRadius; v <= windowRadius; v++) {
                glm::dvec3 sphereDir = tangent_cell_to_sphere_dir(anchorPlanetPos, a, u, v);

                flecs::entity chunk = anchor.world().entity()
                    .child_of(anchor)
                    .set_doc_name(std::format("SurfaceChunk({},{},{})", u, v, alt).c_str())
                    .set<SurfaceChunkCoord>({u, v, alt});

                batchEnqueues.push_back({
                    .chunkEntity = chunk,
                    .coord = {u, v, alt},
                    .sphereDir = sphereDir,
                    .config = PlanetGenerationConfig{
                        .radius = planetInfo->radius,
                    }
                });
            }
        }
    }

    m_generator->enqueues(batchEnqueues.data(), batchEnqueues.size());
}

void SurfaceWindowManager::create_chunk(flecs::entity anchor, int chunkU, int chunkV, int alt) {
    auto* a = anchor.get_mut<SurfaceAnchorComp>();
    auto planet = anchor.parent().get<PlanetComp>();

    glm::dvec3 sphereDir = tangent_cell_to_sphere_dir(get_hp_position(anchor), *a, chunkU, chunkV);

    // create empty chunk
    flecs::entity chunk = anchor.world().entity()
        .child_of(anchor)
        .set<SurfaceChunkCoord>({chunkU, chunkV, alt})
        .set<Transform>({.pos = glm::vec3(
            float(chunkU) * CHUNK_SIZE,
            float(alt) * CHUNK_SIZE,
            float(chunkV) * CHUNK_SIZE
        )})
        .add<GlobalTransform>();

    ChunkGenInput input{
        .chunkEntity = chunk,
        .coord = {chunkU, chunkV, alt},
        .sphereDir = sphereDir,
        .config = PlanetGenerationConfig{
            .radius = planet->radius,
        }
    };
    m_generator->enqueue(input);
}

void SurfaceWindowManager::destroy_chunk(flecs::entity anchor, int chunkU, int chunkV, int alt) {
}

void SurfaceWindowManager::reset_window(flecs::entity anchor, glm::dvec3 playerPlanetPos) {
}

void SurfaceWindowManager::check_grid_shift(flecs::entity anchor, SurfaceAnchorComp &a, glm::dvec3 newCenter) {
}

void SurfaceWindowManager::set_anchor(flecs::entity anchor, SurfaceAnchorComp &a, glm::dvec3 playerPlanetPos) {
}
