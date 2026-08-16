//
// Created by raph on 14/05/2026.
//

#include "PlanetClientModule.h"

#include <cmath>

#include "core/log/Logger.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_transform.h"
#include "core/world/spatial/spatial_components.h"
#include "planet_client_components.h"
#include "renderer/rendering_components.h"
#include "renderer/world/planet/planet_rendering_components.h"

using namespace vp;

void PlanetClientModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetTileLodComp>();
}

void PlanetClientModule::register_systems(flecs::world& ecs) {

    // The camera is looked up once per frame rather than joined into the planet query:
    // the frustum is shared by every planet, and rebuilding it per planet would repeat
    // the same plane extraction
    ecs.system<PlanetTileLodComp, PlanetTileDrawListComp, const Planet, const GlobalTransform>("PlanetClient-UpdateLod")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, PlanetTileLodComp& lod, PlanetTileDrawListComp& drawList, const Planet& planet,
                 const GlobalTransform& transform) {
            if (!lod.quadtree) {
                lod.quadtree = std::make_unique<PlanetQuadtrees>();
            }

            lod.params.planetRadius = planet.radius;

            const glm::dvec3 cameraPosPlanet = -glm::dvec3(transform.pos);

            lod.quadtree->update(cameraPosPlanet, lod.params);

            // Rendering is camera relative, so viewProj already yields planes in the space
            // the draw items live in. No world space conversion is needed
            Frustrum frustum;
            bool hasFrustum = false;
            if (lod.frustumCulling) {
                e.world().each([&](const Camera3d& camera) {
                    if (hasFrustum)
                        return; // first camera wins
                    frustum.update(camera.projectionMatrix * camera.viewMatrix);
                    hasFrustum = true;
                });
            }

            // Flatten for the renderer, which never sees the quadtree itself.
            // clear keeps the capacity, so this does not allocate after the first frames
            {
                VOXEL_ZONE_N("PlanetClient-CollectDrawList");
                drawList.drawItems.clear();
                lod.quadtree->begin_collect();
                for (uint8_t face = 0; face < 6; ++face) {
                    lod.quadtree->collect_node(lod.quadtree->root(static_cast<CubemapFace>(face)), lod.params,
                                               cameraPosPlanet, drawList.drawItems, hasFrustum ? &frustum : nullptr);
                }
            }

            if (lod.debugDrawNodes) {
                lod.quadtree->debug_draw(transform.pos, lod.params, lod.debugMode, lod.debugSegmentsPerEdge);
            }
        });
}

void PlanetClientModule::register_pipelines(flecs::world& ecs) {}

void PlanetClientModule::register_submodules(flecs::world& ecs) {}

void PlanetClientModule::register_entities(flecs::world& ecs) {}
