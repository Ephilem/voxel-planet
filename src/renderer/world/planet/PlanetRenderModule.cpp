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
            .each([](flecs::entity e, const PlanetComp &planet, PlanetRenderComp &renderComp, const GlobalTransform &transform) {
                auto &position = transform.pos;

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
