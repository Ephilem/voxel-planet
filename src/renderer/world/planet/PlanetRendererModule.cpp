//
// Created by raph on 05/08/2026.
//

#include "PlanetRendererModule.h"

#include "client/world/planet/planet_client_components.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"
#include "renderer/Renderer.h"

#include <unordered_set>

using namespace vp;

void PlanetRendererModule::init_renderers(flecs::world& ecs) {
    auto* renderer = ecs.try_get_mut<Renderer>();
    auto* gameState = ecs.try_get_mut<GameState>();

    m_tileAtlas = std::make_unique<PlanetTileAtlas>(renderer->backend.get());

    m_tileRenderer = std::make_unique<PlanetTileRenderer>(renderer->backend.get(), gameState->resourceSystem.get(),
                                                          m_tileAtlas.get());

    m_voxelTextureManager =
        std::make_unique<PlanetVoxelTextureManager>(renderer->backend.get(), gameState->resourceSystem.get());

    m_surfaceChunkRenderer = std::make_unique<PlanetSurfaceChunkRenderer>(
        renderer->backend.get(), gameState->resourceSystem.get(), m_voxelTextureManager.get());

    m_voxelRenderTable = std::make_unique<PlanetVoxelRenderTable>();

    m_chunkMesher = std::make_unique<PlanetSurfaceChunkMesher>(m_voxelRenderTable.get());

    ecs.component<PlanetTileAtlasRef>().add(flecs::Singleton);
    ecs.component<PlanetSurfaceChunkRenderingRef>().add(flecs::Singleton);
    ecs.set<PlanetTileAtlasRef>({.atlas = m_tileAtlas.get(), .renderer = m_tileRenderer.get()});
    ecs.set<PlanetSurfaceChunkRenderingRef>({.mesher = m_chunkMesher.get(), .renderer = m_surfaceChunkRenderer.get()});
}

void PlanetRendererModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetTileDrawListComp>();
    ecs.component<PlanetTileAtlasRef>();
    ecs.component<PlanetSurfaceChunkRenderingRef>();
}

void PlanetRendererModule::register_pipelines(flecs::world& ecs) {}

void PlanetRendererModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTileLodComp, const PlanetTerrainParams, const Planet>("PlanetRendererModule-SetupStreamer")
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTileLodComp&, const PlanetTerrainParams& terrainParams, const Planet&) {
            auto* streamer = e.try_get<PlanetTileStreamComp>();
            if (!streamer) {
                auto generator = std::make_unique<PlanetTileGenerator>(terrainParams, PLANET_TILE_ATLAS_RESOLUTION);

                // preload pinned level
                for (uint8_t face = 0; face < 6; ++face)
                    for (uint8_t level = 0; level <= PLANET_TILE_ATLAS_PINNED_LEVEL; ++level)
                        for (uint32_t y = 0; y < (1u << level); ++y)
                            for (uint32_t x = 0; x < (1u << level); ++x)
                                generator->request({CubemapFace(face), level, x, y});

                generator->submit_pending(uint32_t(-1));

                e.set<PlanetTileStreamComp>({
                    .generator = std::move(generator),
                });
            }
        });

    ecs.system<const Renderer>("PlanetRendererModule-UploadTextures")
        .kind(flecs::PreStore)
        .run([this](flecs::iter& it) {
            while (it.next()) {
                auto renderer = it.field<const Renderer>(0);
                if (!renderer->frameContext.frameActive) {
                    continue;
                }
                m_voxelTextureManager->upload_pending(renderer->frameContext.commandList);
            }
        });

    /**
     * Check store of the current planet for new chunk to mesh, old mesh to delete, or chunk to remesh (update)
     * We do relatively by the camera position (so the player, that is in a planet, so its parent is generally the
     * planet)
     */
    ecs.system<const PlanetSurfaceChunkStore, const Planet, const GlobalTransform, const Camera3d>(
           "PlanetRendererModule-PullChunksUpdate")
        .term_at(0)
        .parent()
        .term_at(1)
        .parent()
        .term_at(2)
        .parent()
        .kind(flecs::PreStore)
        .each([this](const PlanetSurfaceChunkStore& store, const Planet& planet, const GlobalTransform& planetCamPos,
                     const Camera3d& cameraInfo) {
            VOXEL_ZONE_N("PullChunksUpdate");
            constexpr glm::ivec3 kNeighborOffsets[6] = {
                {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
            };

            std::vector<PlanetSurfaceChunkKey> unloadedChunk{};
            std::unordered_set<PlanetSurfaceChunkKey> toRemesh{};
            for (const auto [key, kind] : store.changed_chunks()) {
                if (kind == PlanetSurfaceChunkStore::ChunkChangeKind::Removed) {
                    m_chunkMesher->cancel(key);
                    unloadedChunk.push_back(key);
                } else {
                    // added or updated
                    toRemesh.insert(key);
                }

                // the border faces of the neighbors depend on this chunk
                for (const glm::ivec3& o : kNeighborOffsets) {
                    PlanetSurfaceChunkKey neighbor = key;
                    neighbor.x += o.x;
                    neighbor.y += o.y;
                    neighbor.alt += o.z;
                    toRemesh.insert(neighbor);
                }
            }

            for (const PlanetSurfaceChunkKey& key : toRemesh) {
                // skips the neighbors not loaded and the chunks removed this frame
                if (const auto chunk = store.find(key)) {
                    m_chunkMesher->enqueue(key, chunk, store.neighbors_of(key));
                }
            }

            m_surfaceChunkRenderer->unload_chunks(unloadedChunk);
        });

    ecs.system("PlanetRendererModule-MesherPull").kind(flecs::PreStore).run([this](flecs::iter& it) {
        std::vector<PlanetSurfaceChunkMesher::MeshingResult> uploads;
        uploads.reserve(32);

        m_chunkMesher->drain(uploads, 32);

        if (!uploads.empty()) {
            m_surfaceChunkRenderer->load_chunks(uploads);
        }
    });

    ecs.system<const Renderer>("PlanetRendererModule-BeginFrame").kind(flecs::OnStore).run([this](flecs::iter& it) {
        while (it.next()) {
            auto renderer = it.field<const Renderer>(0);

            // continue, not return: an iterator left before next() returns false has to be fini()'d
            if (!renderer->frameContext.frameActive) {
                continue;
            }

            m_tileAtlas->begin_frame();
        }
    });

    ecs.system<const Renderer>("PlanetRendererModule-UploadChunkMeshes")
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            while (it.next()) {
                auto renderer = it.field<const Renderer>(0);

                if (!renderer->frameContext.frameActive) {
                    continue;
                }

                m_surfaceChunkRenderer->upload_chunk_to_gpu(renderer->frameContext.commandList);
            }
        });

    ecs.system<const Renderer, Camera3d, const Planet, const GlobalTransform, const RenderingPreferences>(
           "PlanetRendererModule-RenderSurface")
        .term_at(2)
        .parent()
        .term_at(3)
        .parent()
        .kind(flecs::OnStore)
        .each([this](const Renderer& renderer, Camera3d& camera, const Planet& playerPlanet,
                     const GlobalTransform& planetTransform, const RenderingPreferences& renderParam) {
            if (!renderer.frameContext.frameActive)
                return;
            VOXEL_ZONE_N("PlanetSurfaceChunkRenderer-Render");
            m_surfaceChunkRenderer->render(renderer.frameContext.commandList, camera, planetTransform, playerPlanet,
                                           renderParam);
        });

    ecs.system<const Renderer, Camera3d>("PlanetRendererModule-RenderTile")
        .kind(flecs::OnStore)
        .each([this](flecs::entity e, const Renderer& renderer, Camera3d& camera) {
            if (!renderer.frameContext.frameActive)
                return;
            VOXEL_ZONE_N("PlanetTileRenderer-Render");
            flecs::world ecs = e.world();
            m_tileRenderer->render_planets(renderer.frameContext.commandList, camera, ecs);
        });
}

void PlanetRendererModule::register_submodules(flecs::world& ecs) {}

void PlanetRendererModule::register_entities(flecs::world& ecs) {
    const auto* registry = ecs.try_get<PlanetVoxelRegistry>();
    if (registry == nullptr) {
        LOG_ERROR("PlanetRendererModule", "No PlanetVoxelRegistry, PlanetModule must be imported first");
        return;
    }

    std::vector<AssetID> texturesUsed;
    for (const auto& block : registry->get_all()) {
        texturesUsed.push_back(block.texture);
    }

    m_voxelTextureManager->register_textures(texturesUsed);
    m_voxelRenderTable->build(*registry, *m_voxelTextureManager);
}
