#include "PlanetModule.h"

#include "core/TracyIntegration.h"
#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "core/world/spatial/spatial_components.h"
#include "generator/PlanetTerrainSampler.h"
#include "planet_components.h"
#include "planet_transform.h"
#include "PlanetSurfaceChunkStore.h"

using namespace vp;

void PlanetModule::register_components(flecs::world& ecs) {
    ecs.component<Planet>();
    ecs.component<PlanetTerrainParams>();

    ecs.component<PlanetTerrainSampler>();

    ecs.component<PlanetSurfaceChunkStore>();
    ecs.component<PlanetSurfaceChunkGeneratorComp>();
    ecs.component<PlanetChunkLoader>();
}

void PlanetModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTerrainParams>("PlanetModule-ResyncTerrainGenerators")
        .event(flecs::OnSet)
        .each([](flecs::entity e, const PlanetTerrainParams& params) {
            e.set<PlanetTerrainSampler>(PlanetTerrainSampler(params));
        });

    ecs.observer<const PlanetTerrainParams, const Planet>("PlanetModule-SetupChunkGenerator")
        .without<PlanetSurfaceChunkGeneratorComp>()
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTerrainParams& params, const Planet& comp) {
            auto* existing = e.get<PlanetSurfaceChunkGeneratorComp>();
            if (existing) {
                return;
            }

            flecs::world ecs = e.world();
            auto registryRef = ecs.get_ref<const PlanetVoxelRegistry>();

            auto generator = std::make_unique<PlanetSurfaceChunkGenerator>(params, double(comp.radius), registryRef);

            e.set<PlanetSurfaceChunkGeneratorComp>({.generator = std::move(generator)});
            e.emplace<PlanetSurfaceChunkStore>();
        });

    ecs.system<PlanetSurfaceChunkGeneratorComp>("PlanetModule-BeginChunkFrame")
        .kind(flecs::PreUpdate)
        .each([](PlanetSurfaceChunkGeneratorComp& gen) {
            if (gen.generator) {
                gen.generator->begin_frame();
            }
        });

    ecs.system<PlanetChunkLoader, const CellCoord, const Transform, const Planet, const Grid,
               PlanetSurfaceChunkGeneratorComp, PlanetSurfaceChunkStore>("PlanetModule-RequestChunks")
        .kind(flecs::OnUpdate)
        .term_at(3)
        .parent()
        .term_at(4)
        .parent()
        .term_at(5)
        .parent()
        .term_at(6)
        .parent()
        .each([](flecs::entity e, PlanetChunkLoader& loader, const CellCoord& cell, const Transform& transform,
                 const Planet& planet, const Grid& grid, PlanetSurfaceChunkGeneratorComp& gen,
                 PlanetSurfaceChunkStore& store) {
            VOXEL_ZONE_N("PlanetModule-RequestChunks");
            if (!gen.generator)
                return;

            // 1. Get player chunk
            const glm::dvec3 posPlanet = grid.get_hp_grid_pos(cell, transform.pos);

            const PlanetVoxelCoord vc = planet_pos_to_voxel(posPlanet, double(planet.radius), 0);
            if (vc.face == FACE_UNKNOWN)
                return;

            const PlanetSurfaceChunkKey center = PlanetSurfaceChunkKey(vc.face, 0, glm::ivec3(vc.voxel));

            // 2. early out if we are still in the same chunk
            if (center == loader.lastCenter)
                return;
            loader.lastCenter = center;

            // 3. Request chunks in a sphere around the player
            const int32_t perSide =
                int32_t(std::llround(planet_voxels_per_face_side(double(planet.radius), 0))) / CHUNK_SIZE;

            const int R = int(loader.loadingDistance);
            const int altR = int(loader.altitudeDistance);

            for (int dz = -altR; dz <= altR; ++dz) {
                for (int dy = -R; dy <= R; ++dy) {
                    for (int dx = -R; dx <= R; ++dx) {
                        // spherical radius
                        const int d2 = (dx * dx) + (dy * dy) + (dz * dz);
                        if (d2 > R * R)
                            continue;

                        PlanetSurfaceChunkKey k = center;
                        k.x += dx;
                        k.y += dy;
                        k.alt += dz;

                        if (k.x < 0 || k.y < 0 || k.x >= perSide || k.y >= perSide)
                            continue;

                        if (store.contains(k))
                            continue;

                        gen.generator->request(k, float(d2));
                    }
                }
            }

            const int U = int(loader.unloadingDistance);
            store.erase_if([&](const PlanetSurfaceChunkKey& k) {
                if (k.level != center.level || k.face != center.face)
                    return true;
                const int dx = k.x - center.x;
                const int dy = k.y - center.y;
                const int dz = k.alt - center.alt;
                return ((dx * dx) + (dy * dy) + (dz * dz)) > U * U;
            });
        });

    ecs.system<PlanetSurfaceChunkGeneratorComp, PlanetSurfaceChunkStore>("PlanetModule-StreamChunks")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity e, PlanetSurfaceChunkGeneratorComp& gen, PlanetSurfaceChunkStore& store) {
            VOXEL_ZONE_N("PlanetModule-StreamChunks");

            if (!gen.generator)
                return;

            gen.generator->submit_pending(32);

            static std::vector<PlanetSurfaceChunkGenerator::PlanetSurfaceChunkGenerationResult> scratch;
            scratch.clear();
            gen.generator->drain(scratch, 32);

            for (auto& result : scratch) {
                if (!result.chunk)
                    result.chunk = std::make_shared<PlanetSurfaceVoxelChunk>(PlanetSurfaceVoxelChunk::Unallocated{});

                store.store(result.key, std::move(result.chunk));
            }
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
