#include "PlanetModule.h"

#include "planet_components.h"

void vp::PlanetModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetComp>();
    ecs.component<PlanetChunkCoord>();
}

void vp::PlanetModule::register_systems(flecs::world &ecs) {
}

void vp::PlanetModule::register_pipelines(flecs::world &ecs) {
}

void vp::PlanetModule::register_submodules(flecs::world &ecs) {
}

void vp::PlanetModule::register_entities(flecs::world &ecs) {
}

