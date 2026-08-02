#include "PlanetModule.h"

#include "planet_components.h"
#include "generator/PlanetTerrainSampler.h"

using namespace vp;

void PlanetModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetComp>();
    ecs.component<PlanetTerrainParams>();

    ecs.component<PlanetTerrainSampler>();
}

void PlanetModule::register_systems(flecs::world &ecs) {
    ecs.observer<const PlanetTerrainParams>("PlanetModule-ResyncTerrainSampler")
        .event(flecs::OnSet)
        .each([](flecs::entity e, const PlanetTerrainParams& params) {
            e.set<PlanetTerrainSampler>(PlanetTerrainSampler(params));
        });
}

void PlanetModule::register_pipelines(flecs::world &ecs) {
}

void PlanetModule::register_submodules(flecs::world &ecs) {
}

void PlanetModule::register_entities(flecs::world &ecs) {

}

