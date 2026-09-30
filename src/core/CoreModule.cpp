#include "CoreModule.h"

#include "debug/DebugDrawModule.h"
#include "GameState.h"
#include "main_components.h"
#include "world/planet/PlanetModule.h"
#include "world/spatial/SpatialModule.h"

using namespace vp;

void CoreModule::register_components(flecs::world& ecs) {
    ecs.component<Player>();

    auto assetRegistry = std::make_unique<AssetRegistry>();
    ecs.component<GameState>().set<GameState>({.resourceSystem = std::make_unique<ResourceSystem>(assetRegistry.get()),
                                               .assetRegistry = std::move(assetRegistry),
                                               .isRunning = true});
}

void CoreModule::register_systems(flecs::world& ecs) {}

void CoreModule::register_pipelines(flecs::world& ecs) {}

void CoreModule::register_submodules(flecs::world& ecs) {
    ecs.import<SpatialModule>();
    ecs.import<DebugDrawModule>();
    ecs.import<PlanetModule>();
}

void CoreModule::register_entities(flecs::world& ecs) {}
