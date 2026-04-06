//
// Created by raph on 04/04/2026.
//

#include "PlanetDebugRenderer.h"

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

void PlanetDebugRenderer::Register(flecs::world &ecs) {

    ecs.system<const PlanetComp, PlanetRenderComp, const Transform>("PlanetDebugRenderer-DrawOctree")
        .kind(flecs::OnStore)
        .each([](flecs::entity e, const PlanetComp& planet, PlanetRenderComp& renderComp, const Transform& transform) {
            auto& position = transform.pos;
            // get cameras
            glm::vec3 camPos(0.0f);
            e.world().query<const Camera3d, const Transform>()
                .each([&](const Camera3d& camera, const Transform& camTransform) {
                    camPos = camTransform.pos;
                });
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
