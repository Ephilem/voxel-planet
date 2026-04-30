//
// Created by raph on 16/04/2026.
//

#include "PlanetChunkManager.h"

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
    ecs.system<const ChunkLoader, const CellCoord, const Transform>("PCM-UpdateChunks")
        .kind(flecs::PreFrame)
        .with<const PlanetComp, const GlobalTransform>().up()
        .each([this](flecs::entity e, const ChunkLoader& loader, const CellCoord& cell, const Transform& trs) {
            auto planet = e.parent();
            if (planet.is_valid()) {
                system_update_chunks(e, planet, loader, cell, trs);
            }
        });
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

void PlanetChunkManager::system_update_chunks(flecs::entity e, flecs::entity planet, const ChunkLoader& loader, const CellCoord& cell, const Transform& trs) {
    // 1. get chunkloader position with planetary-relative coordinates. We assume because the ChunkLoader is child of the planet, that the coordinate is
    //    relative to the planet origin
    // TODO update the code for the case where the loader isn't in the direct grid of the planet, but in a child grid (like a ship grid). In that case we need to do the grid transformation thing to get the loader position in the planet grid
    const Grid* planetGrid = planet.get<Grid>();
    glm::dvec3 loaderPos = cell * planetGrid->cellSize + glm::dvec3(trs.pos);

    // 2. 
}

void PlanetChunkManager::system_poll_results(flecs::entity ePlanet) {
}

void PlanetChunkManager::system_process_unload(flecs::iter &it) {
}

void PlanetChunkManager::system_drain_candidates() {
}
