#include "PlanetModule.h"

#include "planet_components.h"

using namespace vp;

void PlanetModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetComp>();
    ecs.component<PlanetChunkCoord>();
}

void PlanetModule::register_systems(flecs::world &ecs) {
}

void PlanetModule::register_pipelines(flecs::world &ecs) {
}

void PlanetModule::register_submodules(flecs::world &ecs) {
}

void PlanetModule::register_entities(flecs::world &ecs) {
}

