//
// Created by raph on 16/04/2026.
//

#include "PlanetChunkManager.h"

#include <imgui.h>

#include "planet_utils.h"
#include "core/debug/DebugDraw.h"
#include "core/log/Logger.h"
#include "core/world/spatial/spatial_components.h"

using namespace vp;

PlanetChunkManager::PlanetChunkManager() {}


void PlanetChunkManager::Register(flecs::world &ecs) {
    ecs.set<PlanetChunkManager>({});
    auto* cm = ecs.get_mut<PlanetChunkManager>();
    cm->init(ecs);
}

void PlanetChunkManager::init(flecs::world &ecs) {
    // init generator
    m_generator = std::make_unique<PlanetChunkGenerator>();

    // init observers
    ecs.observer<const PlanetComp>("PCM-PlanetCreated")
        .event(flecs::OnAdd)
        .each([this](flecs::entity e, const PlanetComp& p) {
            on_planet_created(e);
        });

    ecs.observer<const PlanetComp>("PCM-PlanetRemoved")
        .event(flecs::OnRemove)
        .each([this](flecs::entity e, const PlanetComp& p) {
            on_planet_removed(e);
        });

    // init systems
    const auto test = ecs.system<ChunkLoader, const CellCoord, const Transform>("PCM-UpdateChunks")
        .kind(flecs::OnUpdate)
        .with<const PlanetComp>().up()
        .each([this](flecs::entity e, ChunkLoader& loader, const CellCoord& cell, const Transform& trs) {
            auto planet = e.parent();
            if (planet.is_valid()) {
                system_update_chunks(e, planet, loader, cell, trs);
            }
        });

    ecs.system<const ChunkLoader>("PCM-DrainCandidateBuffer")
        .kind(flecs::OnUpdate)
        .with<const PlanetComp>().up()
        .each([this](flecs::entity e, const ChunkLoader& _) {
            auto planet = e.parent();
            if (planet.is_valid()) {
                system_drain_candidates(planet);
            }
        });

    ecs.system("PCM-PollResults")
        .kind(flecs::OnStore)
        .run([this](flecs::iter&) {
            system_poll_results();
        });

    ecs.system<const ChunkLoader, const CellCoord, const Transform>("PCM-ProcessUnload")
        .kind(flecs::OnStore)
        .with<const PlanetComp>().up()
        .each([this](flecs::entity e, const ChunkLoader& loader, const CellCoord& cell, const Transform& trs) {
            auto planet = e.parent();
            if (planet.is_valid()) {
                system_process_unload(planet, loader, cell, trs);
            }
        });

    LOG_INFO("PlanetChunkManager", "Registered system: {}", test.query().str().c_str());
}

void PlanetChunkManager::on_planet_created(flecs::entity e) {
    if (m_planets.contains(e)) {
        LOG_WARN("PlanetChunkManager", "Planet entity {} already exists in chunk manager", e.id());
    } else {
        m_planets.emplace(e, PlanetRuntime{});
    }
}

void PlanetChunkManager::on_planet_removed(flecs::entity e) {
    if (!m_planets.contains(e)) {
        LOG_WARN("PlanetChunkManager", "Planet entity {} does not exist in chunk manager", e.id());
    } else {
        m_planets.erase(e);
    }
}

void PlanetChunkManager::system_update_chunks(flecs::entity e, flecs::entity planet, ChunkLoader &loader, const CellCoord& cell, const Transform& trs) {

    ImGui::Begin("PlanetChunkManager Debug");

    // 1. get chunkloader position with planetary-relative coordinates. We assume because the ChunkLoader is child of the planet, that the coordinate is
    //    relative to the planet origin
    // TODO update the code for the case where the loader isn't in the direct grid of the planet, but in a child grid (like a ship grid). In that case we need to do the grid transformation thing to get the loader position in the planet grid
    const Grid* planetGrid = planet.get<Grid>();
    glm::dvec3 loaderPos = cell * planetGrid->cellSize + glm::dvec3(trs.pos);

    // 2. Get planet position
    CubeFace currentFace = dominant_face(loaderPos);
    const PlanetComp* planetInfo = planet.get<PlanetComp>();
    glm::vec2 posOnFace = dir_to_face_uv(currentFace, loaderPos);
    PlanetChunkCoord chunkCoord = {
        .face     = currentFace,
        .x        = static_cast<int>(std::floor(posOnFace.x * planetInfo->radius / CHUNK_SIZE)),
        .y        = static_cast<int>(std::floor(posOnFace.y * planetInfo->radius / CHUNK_SIZE)),
        .altitude = static_cast<int>(std::floor((glm::length(loaderPos) - planetInfo->radius) / CHUNK_SIZE)),
    };


    ImGui::Text("Planet: %s", planet.name().c_str());
    ImGui::Text("Loader Pos: (%.2f, %.2f, %.2f)", loaderPos.x, loaderPos.y, loaderPos.z);
    ImGui::Separator();
    ImGui::Text("Current Face: %d", static_cast<int>(currentFace));
    ImGui::Text("Pos on Face: (%.9f, %.9f)", posOnFace.x, posOnFace.y);
    ImGui::Text("Chunk Coord: (face=%d, u=%d, v=%d, alt=%d)", chunkCoord.face, chunkCoord.x, chunkCoord.y, chunkCoord.altitude);

    if (!ImGui::Button("Update")) {
        ImGui::End();
        return;
    }
    ImGui::End();

    // test if the player changed of chunks
    if (loader.has_visited() && glm::ivec3(chunkCoord.x, chunkCoord.y, chunkCoord.altitude) == loader.lastVisitedChunk) {
        return;
    }
    loader.lastVisitedChunk = glm::ivec3(chunkCoord.x, chunkCoord.y, chunkCoord.altitude);

    if (!m_planets.contains(planet)) {
        LOG_WARN("PlanetChunkManager", "Planet entity {} does not exist in chunk manager", planet.id());
        return;
    }

    PlanetRuntime& runtime = m_planets.at(planet);

    // 3. scan all chunks
    std::vector<ChunkCanditate> candidates = {};
    int loaderRadius = loader.loadRadius;
    for (int x = -loaderRadius; x <= loaderRadius; x++) {
        for (int y = -loaderRadius; y <= loaderRadius; y++) {
            for (int alt = -0; alt <= 0; alt++) {
                PlanetChunkCoord c = chunkCoord;
                c.x += x;
                c.y += y;
                c.altitude += alt;

                if (runtime.is_chunk_loading(c) || runtime.is_chunk_processed(c)) return;

                candidates.push_back({
                    .coord = c,
                    // TODO compute priority based on distance to loaderPos
                    .priority = 0.0f
                });
            }
        }
    }

    for (const auto& candidate : candidates) {
        runtime.candidateHeapChunks.push_back(candidate);
    }

    if (!runtime.candidateHeapChunks.empty()) {
        std::make_heap(runtime.candidateHeapChunks.begin(), runtime.candidateHeapChunks.end(),
                       [](const ChunkCanditate& a, const ChunkCanditate& b) {
                           return a.priority > b.priority; // min-heap
                       });
    }
    LOG_TRACE("PlanetChunkManager", "Added {} candidates to heap (total: {}) for planet {}", candidates.size(),
              runtime.candidateHeapChunks.size(), planet.name().c_str());
}

void PlanetChunkManager::system_drain_candidates(const flecs::entity planet) {
    PlanetRuntime& runtime = m_planets.at(planet);

    std::vector<ChunkGenInput> toEnqueue = {};
    while (!runtime.candidateHeapChunks.empty()) {
        const auto& candidate = runtime.candidateHeapChunks.back();
        runtime.candidateHeapChunks.pop_back();

        if (runtime.is_chunk_processed(candidate.coord) || runtime.is_chunk_loading(candidate.coord)) {
            continue;
        }

        runtime.loadingChunks.insert(candidate.coord);
        toEnqueue.push_back({
            .planet = planet,
            .coord = candidate.coord,
            .config = PlanetGenerationConfig{}, // TODO get config from planet or global settings
            .priority = 0.0f
        });
    }

    if (!toEnqueue.empty()) {
        m_generator->enqueues(toEnqueue.data(), toEnqueue.size());
        LOG_TRACE("PlanetChunkManager", "Enqueued {} chunk generation tasks for planet {}", toEnqueue.size(), planet.name().c_str());
    }
}

void PlanetChunkManager::system_poll_results() {
    auto results = m_generator->poll_results(999);

    for (auto& result : results) {
        flecs::entity planet = result.planet;
        if (!planet.is_valid() || !m_planets.contains(planet)) continue;

        PlanetRuntime& runtime = m_planets.at(planet);
        runtime.loadingChunks.erase(result.coord);

        if (!result.success) continue;

        if (result.empty) {
            runtime.emptyChunks.insert(result.coord);
            continue;
        }

        const PlanetComp* planetInfo = planet.get<PlanetComp>();
        const Grid* planetGrid = planet.get<Grid>();
        if (!planetInfo || !planetGrid) continue;

        glm::dvec3 worldPos = planet_chunk_to_world(result.coord, planetInfo->radius);

        double cellSize = planetGrid->cellSize;
        glm::i64vec3 cell = {
            static_cast<int64_t>(std::floor(worldPos.x / cellSize)),
            static_cast<int64_t>(std::floor(worldPos.y / cellSize)),
            static_cast<int64_t>(std::floor(worldPos.z / cellSize)),
        };
        glm::vec3 subCell = glm::vec3(worldPos - glm::dvec3(cell) * cellSize);

        flecs::entity chunk = planet.world().entity()
            .child_of(planet)
            .set<PlanetChunkCoord>(result.coord)
            .set<CellCoord>(CellCoord(cell))
            .set<Transform>({.pos = subCell})
            .add<GlobalTransform>()
            .set<VoxelChunk>(std::move(result.chunk));

        runtime.loadedChunks[result.coord] = chunk;

        LOG_TRACE("PlanetChunkManager", "Chunk created (face={} x={} y={} alt={})",
                  static_cast<int>(result.coord.face), result.coord.x, result.coord.y, result.coord.altitude);
    }
}

void PlanetChunkManager::system_process_unload(flecs::entity planet, const ChunkLoader& loader, const CellCoord& cell, const Transform& trs) {
    if (!m_planets.contains(planet)) return;
    PlanetRuntime& runtime = m_planets.at(planet);

    const PlanetComp* planetInfo = planet.get<PlanetComp>();
    const Grid* planetGrid = planet.get<Grid>();
    if (!planetInfo || !planetGrid) return;

    glm::dvec3 loaderPos = cell * planetGrid->cellSize + glm::dvec3(trs.pos);
    double unloadDist = static_cast<double>(loader.unloadRadius) * CHUNK_SIZE;

    std::vector<PlanetChunkCoord> toUnload;
    for (auto& [coord, entity] : runtime.loadedChunks) {
        glm::dvec3 chunkPos = planet_chunk_to_world(coord, planetInfo->radius);
        if (glm::length(chunkPos - loaderPos) > unloadDist)
            toUnload.push_back(coord);
    }

    for (const auto& coord : toUnload) {
        auto it = runtime.loadedChunks.find(coord);
        if (it != runtime.loadedChunks.end()) {
            it->second.destruct();
            runtime.loadedChunks.erase(it);
        }
    }

    std::vector<PlanetChunkCoord> emptyToRemove;
    for (const auto& coord : runtime.emptyChunks) {
        glm::dvec3 chunkPos = planet_chunk_to_world(coord, planetInfo->radius);
        if (glm::length(chunkPos - loaderPos) > unloadDist)
            emptyToRemove.push_back(coord);
    }

    for (const auto& coord : emptyToRemove)
        runtime.emptyChunks.erase(coord);
}

