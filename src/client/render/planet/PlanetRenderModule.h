#pragma once
#include "core/ecs/BaseModule.h"
#include "client/render/planet/tile/PlanetTileAtlas.h"
#include "client/render/planet/tile/PlanetTileRenderer.h"
#include "client/render/planet/voxel/PlanetSurfaceChunkMesher.h"
#include "client/render/planet/voxel/PlanetSurfaceChunkRenderer.h"


namespace vp::client {
class PlanetRenderModule : public core::BaseModule<PlanetRenderModule> {

public:
    PlanetRenderModule(flecs::world& ecs) : BaseModule(ecs) {
        init_renderers(ecs);
        register_all(ecs);
    }

private:
    std::unique_ptr<PlanetSurfaceChunkRenderer> m_surfaceChunkRenderer;
    std::unique_ptr<PlanetTileRenderer> m_tileRenderer;
    std::unique_ptr<PlanetTileAtlas> m_tileAtlas;
    std::unique_ptr<PlanetVoxelTextureManager> m_voxelTextureManager;
    std::unique_ptr<PlanetSurfaceChunkMesher> m_chunkMesher;
    std::unique_ptr<PlanetVoxelRenderTable> m_voxelRenderTable;

    void init_renderers(flecs::world& ecs);

    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp::client
