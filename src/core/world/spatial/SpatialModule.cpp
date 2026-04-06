//
// Created by raph on 06/04/2026.
//

#include "SpatialModule.h"

#include "spatial_components.h"
#include "spatial_prefabs.h"

void vp::SpatialModule::register_components(flecs::world &ecs) {
    ecs.component<Transform>()
        .member<float>("pos", 3, 0)
        .member<float>("rot", 3, 3 * sizeof(float))
        .member<float>("scale", 3, 6 * sizeof(float));

    ecs.component<LocalFloatingOriginTransform>()
        .member<float>("pos", 3, 0)
        .member<float>("rot", 3, 3 * sizeof(float))
        .member<float>("scale", 3, 6 * sizeof(float));

    ecs.component<GlobalTransform>()
        .member<float>("pos", 3, 0)
        .member<float>("rot", 3, 3 * sizeof(float))
        .member<float>("scale", 3, 6 * sizeof(float));

    ecs.component<GridCellCoord>()
        .member<int64_t>("x")
        .member<int64_t>("y")
        .member<int64_t>("z");

    ecs.component<Grid>()
        .member<double>("Cell Size");

    ecs.component<FloatingOrigin>();
}

void vp::SpatialModule::register_systems(flecs::world &ecs) {
}

void vp::SpatialModule::register_pipelines(flecs::world &ecs) {
}

void vp::SpatialModule::register_submodules(flecs::world &ecs) {
}

void vp::SpatialModule::register_entities(flecs::world &ecs) {
}
