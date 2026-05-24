//
// Created by raph on 04/04/2026.
//

#include "PlanetRenderModule.h"

#include <imgui.h>

#include "PlanetSurfaceTerrainRenderer.h"
#include "core/debug/DebugDraw.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"

using namespace vp;

static constexpr glm::vec4 OCTREE_NODE_COLORS[] = {
    {1.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 0.5f, 0.5f, 1.0f}
};

void PlanetRenderModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetRenderComp>();
    ecs.component<VoxelChunkMesh>();
    ecs.component<VoxelChunkMeshState>()
            .add(flecs::Exclusive);
}

void PlanetRenderModule::register_systems(flecs::world &ecs) {
    ecs.system<const PlanetComp, PlanetRenderComp, const GlobalTransform>("PlanetDebugRenderer-DrawOctree")
            .kind(flecs::OnStore)
            .each([](flecs::entity e, const PlanetComp &planet, PlanetRenderComp &renderComp,
                     const GlobalTransform &transform) {
                auto &position = transform.pos;

                // We know that the camera is always in 0 0 0, it is his space. Because we know the transform from  0 0 0 to the center of the planet, we can use that as the camera position in the planet's local space
                glm::vec3 camPos = -position;

                for (int i = 0; i < 6; i++) {
                    renderComp.quadtrees[i].build(planet.radius, camPos, static_cast<CubeFace>(i), 6, 2.0f);
                    renderComp.quadtrees[i].debug_viz(position);
                }


                // auto nodes = renderComp.renderOctree.get_nodes();
                // for (const auto &node: nodes) {
                //     if (!node.isLeaf) continue;
                //
                //     glm::vec3 spherePos = glm::normalize(node.center) * (planet.radius);
                //     // adjust spherePos vectically (because chunks are 3d)
                //     AABB aabb = node.get_debug_aabb() + position;
                //     DebugDraw::Point(spherePos + position, OCTREE_NODE_COLORS[node.level % 8]);
                //     // DebugDraw::Aabb(aabb, OCTREE_NODE_COLORS[node.level]);
                // }

                DebugDraw::Sphere(position, planet.radius, {0.5f, 0.5f, 1.0f, 1.0f}, 32);
            });
}

void PlanetRenderModule::register_pipelines(flecs::world &ecs) {
}

void PlanetRenderModule::register_submodules(flecs::world &ecs) {
}

void PlanetRenderModule::register_entities(flecs::world &ecs) {
    PlanetSurfaceTerrainRenderer::Register(ecs);
}
