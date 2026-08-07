//
// Created by raph on 14/05/2026.
//

#include "PlanetClientModule.h"

#include <cmath>

#include "planet_client_components.h"
#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_transform.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/world/planet/planet_rendering_components.h"

using namespace vp;

namespace {
    /// Flattens the leaves under a node into draw instances
    void collect_node(const PlanetQuadtrees &quadtree, uint32_t index,
                      const PlanetLodParams &params, const glm::dvec3 &cameraPosPlanet,
                      std::vector<PlanetTileDrawInstance> &out) {
        const PlanetQuadtreeNode &node = quadtree.node(index);

        if (!node.is_leaf()) {
            for (uint32_t i = 0; i < 4; ++i) {
                collect_node(quadtree, node.firstChild + i, params, cameraPosPlanet, out);
            }
            return;
        }

        const double extent = 2.0 / double(1u << node.level);
        const double u0 = -1.0 + double(node.x) * extent;
        const double v0 = -1.0 + double(node.y) * extent;

        // Camera relative: subtracting in double before the cast is what keeps the
        // precision. The absolute position is around 450 km, where a float only
        // resolves 3 cm, while the result here is small enough to stay exact
        const glm::dvec3 originPlanet =
                face_uv_to_direction(node.face, u0, v0) * params.planetRadius;

        PlanetTileDrawInstance instance;
        instance.originSpacePos = glm::vec3(originPlanet - cameraPosPlanet);
        instance.extent = float(extent);
        instance.nodeFaceOrigin = {float(u0), float(v0)};
        instance.packed = planet_tile_pack(node.face, node.level);
        instance.morph = 0.f;

        out.push_back(instance);
    }
}

void PlanetClientModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetTileLodComp>();
    ecs.component<PlanetTileDrawList>();
}

void PlanetClientModule::register_systems(flecs::world &ecs) {

    ecs.system<PlanetTileLodComp, PlanetTileDrawList, const PlanetComp, const GlobalTransform>(
                "PlanetClient-UpdateLod")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, PlanetTileLodComp &lod, PlanetTileDrawList &drawList,
                 const PlanetComp &planet, const GlobalTransform &transform) {
            if (!lod.quadtree) {
                lod.quadtree = std::make_unique<PlanetQuadtrees>();
            }

            lod.params.planetRadius = planet.radius;

            const glm::dvec3 cameraPosPlanet = -glm::dvec3(transform.pos);

            lod.quadtree->update(cameraPosPlanet, lod.params);

            // Flatten for the renderer, which never sees the quadtree itself.
            // clear keeps the capacity, so this does not allocate after the first frames
            drawList.drawInstances.clear();
            for (uint8_t face = 0; face < 6; ++face) {
                collect_node(*lod.quadtree, lod.quadtree->root(CubemapFace(face)),
                             lod.params, cameraPosPlanet, drawList.drawInstances);
            }

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
