//
// Created by raph on 04/04/2026.
//

#include "PlanetDebugRenderer.h"

#include "core/DebugDrawManager.h"
#include "core/main_components.h"

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

void PlanetDebugRenderer::Register(flecs::world &ecs) {

    ecs.system<const PlanetComp, PlanetRenderComp, const Position>("PlanetDebugRenderer-DrawOctree")
        .kind(flecs::OnStore)
        .each([](flecs::entity e, const PlanetComp& planet, PlanetRenderComp& renderComp, const Position& position) {
            // get cameras
            glm::vec3 camPos(0.0f);
            e.world().query<const Camera3d, const Position>()
                .each([&](const Camera3d& camera, const Position& camPos_) {
                    camPos = camPos_;
                });
            renderComp.renderOctree.build(planet.radius, camPos, 5, 100.0f);

            auto nodes = renderComp.renderOctree.get_nodes();
            for (const auto& node : nodes) {
                if (!node.isLeaf) continue;

                AABB aabb = node.get_debug_aabb() + position;
                DebugDrawManager::Point(node.center + position, OCTREE_NODE_COLORS[node.level]);
                DebugDrawManager::Aabb(aabb, OCTREE_NODE_COLORS[node.level]);
            }
        });
}
