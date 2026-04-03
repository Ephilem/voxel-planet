//
// Created by raph on 05/08/2026.
//

#include "PlanetRenderModule.h"

#include "client/render/camera/camera3d_components.h"
#include "client/render/planet/planet_rendering_components.h"
#include "client/render/render_settings.h"
#include "core/GameState.h"
#include "core/math/frustrum.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/Renderer.h"
#include "renderer/render_phases.h"
#include "renderer/rendering_components.h"

#include <unordered_set>

namespace vp::client {
void PlanetRenderModule::init_renderers(flecs::world& ecs) {
    auto* renderer = ecs.try_get_mut<renderer::Renderer>();
    auto* gameState = ecs.try_get_mut<core::GameState>();

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
    ecs.component<PlanetTileLodComp>();
    ecs.set<PlanetTileAtlasRef>({.atlas = m_tileAtlas.get(), .renderer = m_tileRenderer.get()});
    ecs.set<PlanetSurfaceChunkRenderingRef>({
        .mesher = m_chunkMesher.get(), .renderer = m_surfaceChunkRenderer.get()
    });
}

void PlanetRenderModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetTileDrawListComp>();
    ecs.component<PlanetTileAtlasRef>();
    ecs.component<PlanetSurfaceChunkRenderingRef>();
}

void PlanetRenderModule::register_pipelines(flecs::world& ecs) {
}

void PlanetRenderModule::register_systems(flecs::world& ecs) {
    // The LOD reads Camera3d, not RenderView: ExtractCameraView writes RenderView in the same phase,
    // and relying on the creation order between the two modules would hide the dependency
    flecs::query<const Camera3d> mainCamera = ecs.query_builder<const Camera3d>().with<MainCamera>().build();

    // RenderExtract: after GlobalTransform propagation (PreStore), before any draw of RenderSubmit
    ecs.system<PlanetTileLodComp, PlanetTileDrawListComp, const core::Planet, const core::GlobalTransform>(
           "PlanetRenderModule-UpdateLod")
       .kind<renderer::phases::RenderExtract>()
       .each([mainCamera](PlanetTileLodComp& lod, PlanetTileDrawListComp& drawList, const core::Planet& planet,
                          const core::GlobalTransform& transform) {
           if (!lod.quadtree) {
               lod.quadtree = std::make_unique<PlanetTileQuadtrees>();
           }

           lod.params.planetRadius = planet.radius;

           const glm::dvec3 cameraPosPlanet = -transform.pos;

           lod.quadtree->update(cameraPosPlanet, lod.params);

           // Rendering is camera relative, so viewProj already yields planes in the space
           // the draw items live in. No world space conversion is needed
           core::Frustrum frustum;
           bool hasFrustum = false;
           if (lod.frustumCulling) {
               mainCamera.each([&](const Camera3d& camera) {
                   frustum.update(camera.projectionMatrix * camera.viewMatrix);
                   hasFrustum = true;
               });
           }

           // Flatten for the renderer, which never sees the quadtree itself.
           // clear keeps the capacity, so this does not allocate after the first frames
           {
               VOXEL_ZONE_N("PlanetClient-CollectDrawList");
               drawList.drawItems.clear();
               lod.quadtree->begin_collect();
               for (uint8_t face = 0; face < 6; ++face) {
                   lod.quadtree->collect_node(lod.quadtree->root(static_cast<core::CubemapFace>(face)), lod.params,
                                              cameraPosPlanet, drawList.drawItems, hasFrustum ? &frustum : nullptr);
               }
           }

           if (lod.debugDrawNodes) {
               lod.quadtree->debug_draw(glm::vec3(transform.pos), lod.params, lod.debugMode, lod.debugSegmentsPerEdge);
           }
       });

    ecs.observer<const PlanetTileLodComp, const core::PlanetTerrainParams, const core::Planet>(
           "PlanetRenderModule-SetupStreamer")
       .event(flecs::OnAdd)
       .each([](flecs::entity e, const PlanetTileLodComp&, const core::PlanetTerrainParams& terrainParams,
                const core::Planet&) {
           auto* streamer = e.try_get<PlanetTileStreamComp>();
           if (!streamer) {
               auto generator = std::make_unique<PlanetTileGenerator>(
                   terrainParams, PLANET_TILE_ATLAS_RESOLUTION, 4);

               // preload pinned level
               for (uint8_t face = 0; face < 6; ++face)
                   for (uint8_t level = 0; level <= PLANET_TILE_ATLAS_PINNED_LEVEL; ++level)
                       for (uint32_t y = 0; y < (1u << level); ++y)
                           for (uint32_t x = 0; x < (1u << level); ++x)
                               generator->request({core::CubemapFace(face), level, x, y});

               generator->submit_pending(uint32_t(-1));

               e.set<PlanetTileStreamComp>({
                   .generator = std::move(generator),
               });
           }
       });

    // RenderSubmit systems run in their creation order: texture upload, atlas frame, mesh upload, surface, tiles
    ecs.system<const renderer::Renderer>("PlanetRenderModule-UploadTextures")
       .kind<renderer::phases::RenderSubmit>()
       .run([this](flecs::iter& it) {
           while (it.next()) {
               auto renderer = it.field<const renderer::Renderer>(0);
               if (!renderer->frameContext.frameActive) {
                   continue;
               }
               m_voxelTextureManager->upload_pending(renderer->frameContext.commandList);
           }
       });

    /**
 * Check store of the current planet for new chunk to mesh, old mesh to delete, or chunk to remesh (update)
 * We do relatively by the main camera (so the player, that is in a planet, so its parent is generally the
 * planet)
 */
    ecs.system<const core::PlanetSurfaceChunkStore, const core::Planet, const core::GlobalTransform,
               const core::PlanetSurfaceChunkGeneratorComp>("PlanetRenderModule-PullChunksUpdate")
       .term_at(0)
       .parent()
       .term_at(1)
       .parent()
       .term_at(2)
       .parent()
       .term_at(3)
       .parent()
       .with<MainCamera>()
       .kind(flecs::PreStore)
       .each([this](const core::PlanetSurfaceChunkStore& store, const core::Planet& planet,
                    const core::GlobalTransform& planetCamPos, const core::PlanetSurfaceChunkGeneratorComp& gen) {
           VOXEL_ZONE_N("PullChunksUpdate");

           // the dual contouring cells of a chunk read its + neighbors only
           constexpr glm::ivec3 kPlusOffsets[] = {
               {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
           };

           const auto offset_key = [](core::PlanetSurfaceChunkKey k, glm::ivec3 o) {
               k.x += o.x;
               k.y += o.y;
               k.alt += o.z;
               return k;
           };

           std::vector<core::PlanetSurfaceChunkKey> unloadedChunk{};
           std::unordered_set<core::PlanetSurfaceChunkKey> toRemesh{};
           for (const auto [key, kind] : store.changed_chunks()) {
               if (kind == core::PlanetSurfaceChunkStore::ChunkChangeKind::Removed) {
                   m_chunkMesher->cancel(key);
                   unloadedChunk.push_back(key);
               } else {
                   // added or updated
                   toRemesh.insert(key);
               }

               // the chunks reading this one as a + neighbor
               for (const glm::ivec3& o : kPlusOffsets) {
                   toRemesh.insert(offset_key(key, -o));
               }
           }

           for (const core::PlanetSurfaceChunkKey& key : toRemesh) {
               // skips the neighbors not loaded and the chunks removed this frame
               const auto chunk = store.find(key);
               if (!chunk) {
                   continue;
               }

               bool waiting = false;
               if (gen.generator) {
                   for (const glm::ivec3& o : kPlusOffsets) {
                       const core::PlanetSurfaceChunkKey n = offset_key(key, o);
                       if (!store.contains(n) && gen.generator->is_requested(n)) {
                           waiting = true;
                           break;
                       }
                   }
               }

               if (waiting) {
                   continue;
               }

               m_chunkMesher->enqueue(key, chunk, store.neighbors_of(key));
           }

           m_surfaceChunkRenderer->unload_chunks(unloadedChunk);
       });

    ecs.system("PlanetRenderModule-MesherPull").kind(flecs::PreStore).run([this](flecs::iter& it) {
        std::vector<PlanetSurfaceChunkMesher::MeshingResult> uploads;
        uploads.reserve(32);

        m_chunkMesher->drain(uploads, 32);

        if (!uploads.empty()) {
            m_surfaceChunkRenderer->load_chunks(uploads);
        }
    });

    ecs.system<const renderer::Renderer>("PlanetRenderModule-BeginFrame")
       .kind<renderer::phases::RenderSubmit>()
       .run([this](flecs::iter& it) {
           while (it.next()) {
               auto renderer = it.field<const renderer::Renderer>(0);

               // continue, not return: an iterator left before next() returns false has to be fini()'d
               if (!renderer->frameContext.frameActive) {
                   continue;
               }

               m_tileAtlas->begin_frame();
           }
       });

    ecs.system<const renderer::Renderer>("PlanetRenderModule-UploadChunkMeshes")
       .kind<renderer::phases::RenderSubmit>()
       .run([this](flecs::iter& it) {
           while (it.next()) {
               auto renderer = it.field<const renderer::Renderer>(0);

               if (!renderer->frameContext.frameActive) {
                   continue;
               }

               m_surfaceChunkRenderer->upload_chunk_to_gpu(renderer->frameContext.commandList);
           }
       });

    // The main camera only anchors the planet it is in, the matrices come from RenderView
    ecs.system<const renderer::Renderer, const renderer::RenderView, const core::Planet, const core::GlobalTransform,
               const RenderingPreferences>("PlanetRenderModule-RenderSurface")
       .term_at(2)
       .parent()
       .term_at(3)
       .parent()
       .with<MainCamera>()
       .kind<renderer::phases::RenderSubmit>()
       .each([this](const renderer::Renderer& renderer, const renderer::RenderView& view,
                    const core::Planet& playerPlanet, const core::GlobalTransform& planetTransform,
                    const RenderingPreferences& renderParam) {
           if (!renderer.frameContext.frameActive)
               return;
           VOXEL_ZONE_N("PlanetSurfaceChunkRenderer-Render");
           m_surfaceChunkRenderer->render(renderer.frameContext.commandList, view, planetTransform, playerPlanet,
                                          renderParam);
       });

    // Singletons only: runs once per frame
    ecs.system<const renderer::Renderer, const renderer::RenderView>("PlanetRenderModule-RenderTile")
       .kind<renderer::phases::RenderSubmit>()
       .each([this](flecs::iter& it, size_t, const renderer::Renderer& renderer, const renderer::RenderView& view) {
           if (!renderer.frameContext.frameActive)
               return;
           VOXEL_ZONE_N("PlanetTileRenderer-Render");
           flecs::world ecs = it.world();
           m_tileRenderer->render_planets(renderer.frameContext.commandList, view, ecs);
       });
}

void PlanetRenderModule::register_submodules(flecs::world& ecs) {
}

void PlanetRenderModule::register_entities(flecs::world& ecs) {
    const auto* registry = ecs.try_get<core::PlanetVoxelRegistry>();
    if (registry == nullptr) {
        LOG_ERROR("PlanetRenderModule", "No PlanetVoxelRegistry, PlanetModule must be imported first");
        return;
    }

    std::vector<core::AssetID> texturesUsed;
    for (const auto& block : registry->get_all()) {
        texturesUsed.push_back(block.texture);
    }

    m_voxelTextureManager->register_textures(texturesUsed);
    m_voxelRenderTable->build(*registry, *m_voxelTextureManager);
}
} // namespace vp::client
