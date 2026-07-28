//
// Created by raph on 14/05/2026.
//

#include "PlanetClientModule.h"

#include "planet_client_components.h"

using namespace vp;

void PlanetClientModule::register_components(flecs::world &ecs) {
    ecs.component<InPlanetSurface>();
    ecs.component<PlanetUpVector>();
}

void PlanetClientModule::register_systems(flecs::world &ecs) {
}

void PlanetClientModule::register_pipelines(flecs::world &ecs) {
}

void PlanetClientModule::register_submodules(flecs::world &ecs) {
}

void PlanetClientModule::register_entities(flecs::world &ecs) {
}
