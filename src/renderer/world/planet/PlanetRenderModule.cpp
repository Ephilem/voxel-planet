//
// Created by raph on 04/04/2026.
//

#include "PlanetRenderModule.h"

#include "core/debug/DebugDraw.h"
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
}

void PlanetRenderModule::register_systems(flecs::world &ecs) {

    ecs.system<const PlanetComp, PlanetRenderComp, const GlobalTransform>("PlanetDebugRenderer-DrawOctree")
        .kind(flecs::OnStore)
        .each([](flecs::entity e, const PlanetComp& planet, PlanetRenderComp& renderComp, const GlobalTransform& transform) {
            auto& position = transform.pos;

            // We know that the camera is always in 0 0 0, it is space. Because we know the transform from  0 0 0 to the center of the planet, we can use that as the camera position in the planet's local space
            glm::vec3 camPos = -position;

            renderComp.renderOctree.build(planet.radius, camPos, 5, 1000.0f);

            auto nodes = renderComp.renderOctree.get_nodes();
            for (const auto& node : nodes) {
                if (!node.isLeaf) continue;

                AABB aabb = node.get_debug_aabb() + position;
                DebugDraw::Point(node.center + position, OCTREE_NODE_COLORS[node.level]);
                DebugDraw::Aabb(aabb, OCTREE_NODE_COLORS[node.level]);
            }
        });
}

void PlanetRenderModule::register_pipelines(flecs::world &ecs) {
}

void PlanetRenderModule::register_submodules(flecs::world &ecs) {
}

void PlanetRenderModule::register_entities(flecs::world &ecs) {
}
