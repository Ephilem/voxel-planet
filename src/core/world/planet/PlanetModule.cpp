#include "PlanetModule.h"

#include "generator/PlanetTerrainSampler.h"
#include "planet_components.h"
#include "PlanetSurfaceChunkStore.h"

using namespace vp;

void PlanetModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetComp>();
    ecs.component<PlanetTerrainParams>();

    ecs.component<PlanetTerrainSampler>();

    ecs.component<PlanetSurfaceChunkStore>();
}

void PlanetModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTerrainParams>("PlanetModule-ResyncTerrainSampler")
        .event(flecs::OnSet)
        .each([](flecs::entity e, const PlanetTerrainParams& params) {
            e.set<PlanetTerrainSampler>(PlanetTerrainSampler(params));
        });
}

void PlanetModule::register_pipelines(flecs::world& ecs) {}

void PlanetModule::register_submodules(flecs::world& ecs) {}

void PlanetModule::register_entities(flecs::world& ecs) {
    PlanetVoxelRegistryComp reg;

    // FIRST, so 0 = air, which is the default value of a chunk
    BlockDefinition air = BlockDefinition::Uniform("voxelplanet:air"_asset, AssetID::Invalid);
    air.opaque = false;
    reg.register_block(air);

    reg.register_block(
        BlockDefinition::Uniform("voxelplanet:cobblestone"_asset, "voxelplanet:textures/cobblestone"_asset));

    reg.register_block(BlockDefinition::Uniform("voxelplanet:grass"_asset, "voxelplanet:textures/grass"_asset));

    ecs.set<PlanetVoxelRegistryComp>(std::move(reg));
}
