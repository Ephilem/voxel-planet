//
// Created by raph on 14/05/2026.
//

#include "PlanetClientModule.h"

#include <cmath>

#include "planet_client_components.h"
#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"

using namespace vp;

void PlanetClientModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetTileLodComp>();
}

void PlanetClientModule::register_systems(flecs::world &ecs) {

    ecs.system<PlanetTileLodComp, const PlanetComp, const GlobalTransform>("PlanetClient-UpdateLod")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, PlanetTileLodComp &lod, const PlanetComp &planet, const GlobalTransform &transform) {
            if (!lod.quadtree) {
                lod.quadtree = std::make_unique<PlanetQuadtrees>();
            }

            lod.params.planetRadius = planet.radius;

            const glm::dvec3 cameraPosPlanet = -glm::dvec3(transform.pos);

            lod.quadtree->update(cameraPosPlanet, lod.params);

            if (lod.debugDrawNodes) {
                lod.quadtree->debug_draw(transform.pos, lod.params, lod.debugMode,
                                         lod.debugSegmentsPerEdge);
            }
        });
}

void PlanetClientModule::register_pipelines(flecs::world &ecs) {
}

void PlanetClientModule::register_submodules(flecs::world &ecs) {
}

void PlanetClientModule::register_entities(flecs::world &ecs) {
}
