#include "PlanetModule.h"

#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "generator/PlanetTerrainSampler.h"
#include "planet_components.h"
#include "PlanetSurfaceChunkStore.h"

using namespace vp;

void PlanetModule::register_components(flecs::world& ecs) {
    ecs.component<Planet>();
    ecs.component<PlanetTerrainParams>();

    ecs.component<PlanetTerrainSampler>();

    ecs.component<PlanetSurfaceChunkStore>();
    ecs.component<PlanetSurfaceChunkGeneratorComp>();
}

void PlanetModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTerrainParams>("PlanetModule-ResyncTerrainGenerators")
        .event(flecs::OnSet)
        .each([](flecs::entity e, const PlanetTerrainParams& params) {
            e.set<PlanetTerrainSampler>(PlanetTerrainSampler(params));
        });

    ecs.observer<const PlanetTerrainParams, const Planet>("PlanetModule-SetupChunkGenerator")
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTerrainParams& params, const Planet& comp) {
            auto* existing = e.get<PlanetSurfaceChunkGeneratorComp>(); // à créer, cf. remarque
            if (existing)
                return;

            flecs::world ecs = e.world();
            auto registryRef = ecs.get_ref<const PlanetVoxelRegistry>();

            auto generator = std::make_unique<PlanetSurfaceChunkGenerator>(params, double(comp.radius), registryRef);

            e.set<PlanetSurfaceChunkGeneratorComp>({.generator = std::move(generator)});
        });

    ecs.system<const PlanetChunkLoader, PlanetSurfaceChunkStore>("PlanetModule-LoadChunks")
        .kind(flecs::OnUpdate)
        .term_at(1)
        .parent()
        .each([](flecs::entity e, const PlanetChunkLoader& loader, PlanetSurfaceChunkStore& store) {
            // 1. we need to know what chunk need to be loaded
        });
}

void PlanetModule::register_pipelines(flecs::world& ecs) {}

void PlanetModule::register_submodules(flecs::world& ecs) {}

void PlanetModule::register_entities(flecs::world& ecs) {
    PlanetVoxelRegistry reg;

    // FIRST, so 0 = air, which is the default value of a chunk
    BlockDefinition air = BlockDefinition::Uniform("voxelplanet:air"_asset, AssetID::Invalid);
    air.opaque = false;
    reg.register_block(air);

    reg.register_block(
        BlockDefinition::Uniform("voxelplanet:cobblestone"_asset, "voxelplanet:textures/cobblestone"_asset));

    reg.register_block(BlockDefinition::Uniform("voxelplanet:grass"_asset, "voxelplanet:textures/grass"_asset));

    ecs.set<PlanetVoxelRegistry>(std::move(reg));
}
