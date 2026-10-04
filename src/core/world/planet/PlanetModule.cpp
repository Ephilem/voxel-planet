#include "PlanetModule.h"

#include "core/TracyIntegration.h"
#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "core/world/spatial/spatial_components.h"
#include "planet_components.h"
#include "planet_transform.h"
#include "PlanetSurfaceChunkStore.h"

using namespace vp;

void PlanetModule::register_components(flecs::world& ecs) {
    ecs.component<Planet>();
    ecs.component<PlanetTerrainParams>();

    ecs.component<PlanetSurfaceChunkStore>();
    ecs.component<PlanetSurfaceChunkGeneratorComp>();
    ecs.component<PlanetChunkLoader>();
    ecs.component<SurfaceAligned>().member<float>("frame", 4);
}

void PlanetModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTerrainParams, const Planet>("PlanetModule-SetupChunkGenerator")
        .without<PlanetSurfaceChunkGeneratorComp>()
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTerrainParams& params, const Planet& comp) {
            const auto* registry = e.world().try_get<PlanetVoxelRegistry>();
            if (registry == nullptr) {
                LOG_ERROR("PlanetModule", "No PlanetVoxelRegistry, chunk generator not created for {}",
                          e.name().c_str());
                return;
            }

            auto generator = std::make_unique<PlanetSurfaceChunkGenerator>(params, double(comp.radius), *registry);

            e.set<PlanetSurfaceChunkGeneratorComp>({.generator = std::move(generator)});
            e.emplace<PlanetSurfaceChunkStore>();
        });

    ecs.system<PlanetSurfaceChunkStore>("PlanetModule-ResetChangeList")
        .kind(flecs::PreUpdate)
        .each([](PlanetSurfaceChunkStore& store) { store.clear_changed_chunks(); });

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
            if (!gen.generator) {
                return;
            }

            // 1. Get player chunk
            const glm::dvec3 posPlanet = grid.get_hp_grid_pos(cell, transform.pos);

            const PlanetVoxelCoord vc = planet_pos_to_voxel(posPlanet, double(planet.radius), 0);
            if (vc.face == FACE_UNKNOWN) {
                return;
            }

            const PlanetSurfaceChunkKey center = PlanetSurfaceChunkKey(vc.face, 0, glm::ivec3(vc.voxel));

            // 2. early out if we are still in the same chunk
            if (center == loader.lastCenter) {
                return;
            }
            loader.lastCenter = center;

            // 3. Request chunks in a sphere around the player
            const int32_t perSide =
                int32_t(std::llround(planet_voxels_per_face_side(double(planet.radius), 0))) / CHUNK_SIZE;

            const int R = int(loader.loadingDistance);
            const int altR = int(loader.altitudeDistance);

            gen.generator->reprioritize([&](const PlanetSurfaceChunkKey& k) -> std::optional<float> {
                if (k.face != center.face || k.level != center.level) {
                    return std::nullopt;
                }
                const int dx = k.x - center.x, dy = k.y - center.y, dz = k.alt - center.alt;
                const int d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > R * R) {
                    return std::nullopt;
                }
                return float(d2);
            });

            for (int dz = -altR; dz <= altR; ++dz) {
                for (int dy = -R; dy <= R; ++dy) {
                    for (int dx = -R; dx <= R; ++dx) {
                        // spherical radius
                        const int d2 = (dx * dx) + (dy * dy) + (dz * dz);
                        if (d2 > R * R) {
                            continue;
                        }

                        PlanetSurfaceChunkKey k = center;
                        k.x += dx;
                        k.y += dy;
                        k.alt += dz;

                        if (k.x < 0 || k.y < 0 || k.x >= perSide || k.y >= perSide) {
                            continue;
                        }

                        if (store.contains(k)) {
                            continue;
                        }

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

    // Body rotation follows the local up of the planet
    ecs.system<SurfaceAligned, Transform, const CellCoord, const Grid, const Planet>("PlanetModule-SurfaceAlign")
        .kind(flecs::PostUpdate)
        .term_at(3)
        .parent()
        .term_at(4)
        .parent()
        .each([](flecs::entity e, SurfaceAligned& aligned, Transform& transform, const CellCoord& cell,
                 const Grid& grid, const Planet&) {
            const glm::dvec3 p = grid.get_hp_grid_pos(cell, transform.pos);
            const double r = glm::length(p);

            if (r > 1.0) {
                const glm::dvec3 newUp = p / r;
                const glm::dvec3 curUp = glm::dquat(aligned.frame) * glm::dvec3(0.0, 1.0, 0.0);
                aligned.frame = glm::quat(glm::normalize(glm::dquat(curUp, newUp) * glm::dquat(aligned.frame)));
            }

            const auto* angles = e.try_get<LookAngles>();
            const float yaw = angles ? angles->yaw : 0.0f;
            transform.rot = glm::normalize(aligned.frame * glm::angleAxis(glm::radians(yaw), glm::vec3(0.f, 1.f, 0.f)));
        });

    ecs.system<PlanetSurfaceChunkGeneratorComp, PlanetSurfaceChunkStore>("PlanetModule-StreamChunks")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity e, PlanetSurfaceChunkGeneratorComp& gen, PlanetSurfaceChunkStore& store) {
            VOXEL_ZONE_N("PlanetModule-StreamChunks");

            if (!gen.generator) {
                return;
            }

            gen.generator->submit_pending(32);

            static std::vector<PlanetSurfaceChunkGenerator::PlanetSurfaceChunkGenerationResult> scratch;
            scratch.clear();
            gen.generator->drain(scratch, 32);

            for (auto& result : scratch) {
                if (!result.chunk)
                    result.chunk = std::make_shared<PlanetSurfaceVoxelChunk>(); // air chunk

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

    ecs.component<PlanetVoxelRegistry>().add(flecs::Singleton);
    ecs.set<PlanetVoxelRegistry>(std::move(reg));
}
