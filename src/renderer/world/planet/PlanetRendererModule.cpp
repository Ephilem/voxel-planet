//
// Created by raph on 05/08/2026.
//

#include "PlanetRendererModule.h"

#include "client/world/planet/planet_client_components.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"
#include "renderer/Renderer.h"

using namespace vp;

void PlanetRendererModule::init_renderers(flecs::world& ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();

    m_tileAtlas = std::make_unique<PlanetTileAtlas>(renderer->backend.get());

    m_tileRenderer = std::make_unique<PlanetTileRenderer>(renderer->backend.get(), gameState->resourceSystem.get(),
                                                          m_tileAtlas.get());

    m_voxelTextureManager =
        std::make_unique<PlanetVoxelTextureManager>(renderer->backend.get(), gameState->resourceSystem.get());

    m_surfaceChunkRenderer = std::make_unique<PlanetSurfaceChunkRenderer>(
        renderer->backend.get(), gameState->resourceSystem.get(), m_voxelTextureManager.get());

    m_chunkMesher = std::make_unique<PlanetSurfaceChunkMesher>();

    ecs.set<PlanetTileAtlasRef>({.atlas = m_tileAtlas.get(), .renderer = m_tileRenderer.get()});
}

void PlanetRendererModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetTileDrawListComp>();
    ecs.component<PlanetTileAtlasRef>();
}

void PlanetRendererModule::register_pipelines(flecs::world& ecs) {}

void PlanetRendererModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTileLodComp, const PlanetTerrainParams, const Planet>("PlanetRendererModule-SetupStreamer")
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTileLodComp& lod, const PlanetTerrainParams& terrainParams,
                 const Planet& comp) {
            auto* streamer = e.get<PlanetTileStreamComp>();
            if (!streamer) {
                auto generator =
                    std::make_unique<PlanetTileGenerator>(terrainParams, PLANET_TILE_ATLAS_RESOLUTION, comp.radius);

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
        .term_at(0)
        .singleton()
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
        .each([this](const PlanetSurfaceChunkStore store, const Planet planet, const GlobalTransform planetCamPos,
                     const Camera3d cameraInfo) {
            VOXEL_ZONE_N("PullChunksUpdate");
            std::vector<PlanetSurfaceChunkKey> unloadedChunk{};
            for (const auto [key, kind] : store.changed_chunks()) {
                if (kind == PlanetSurfaceChunkStore::ChunkChangeKind::Removed) {
                    unloadedChunk.push_back(key);
                } else {
                    m_chunkMesher->enqueue(key, store.find(key), 0);
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

    ecs.system<const Renderer>("PlanetRendererModule-BeginFrame")
        .term_at(0)
        .singleton()
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            while (it.next()) {
                auto renderer = it.field<const Renderer>(0);

                if (!renderer->frameContext.frameActive) {
                    return;
                }

                m_tileAtlas->begin_frame();
                m_surfaceChunkRenderer->upload_chunk_to_gpu(renderer->frameContext.commandList);
            }
        });

    ecs.system<const Renderer>("PlanetRendererModule-UploadChunkMeshes")
        .term_at(0)
        .singleton()
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            while (it.next()) {
                auto renderer = it.field<const Renderer>(0);

                if (!renderer->frameContext.frameActive) {
                    return;
                }

                m_surfaceChunkRenderer->upload_chunk_to_gpu(renderer->frameContext.commandList);
            }
        });

    ecs.system<const Renderer, Camera3d, const Planet, const GlobalTransform>("PlanetRendererModule-RenderSurface")
        .term_at(0)
        .singleton()
        .term_at(2)
        .parent()
        .term_at(3)
        .parent()
        .kind(flecs::OnStore)
        .each([this](flecs::entity e, const Renderer& renderer, Camera3d& camera, const Planet playerPlanet,
                     const GlobalTransform planetTransform) {
            if (!renderer.frameContext.frameActive)
                return;
            VOXEL_ZONE_N("PlanetTileRenderer-Render");
            m_surfaceChunkRenderer->render(renderer.frameContext.commandList, camera, planetTransform, playerPlanet);
            flecs::world ecs = e.world();
        });

    ecs.system<const Renderer, Camera3d>("PlanetRendererModule-RenderTile")
        .term_at(0)
        .singleton()
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
    std::vector<AssetID> texturesUsed;
    const auto* registry = ecs.get<PlanetVoxelRegistry>();

    for (auto& block : registry->get_all()) {
        texturesUsed.push_back(block.texture);
    }

    m_voxelTextureManager->register_textures(texturesUsed);
}
