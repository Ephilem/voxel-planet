#pragma once
#include "PlanetTileAtlas.h"
#include "PlanetTileRenderer.h"
#include "renderer/world/planet/PlanetSurfaceChunkRenderer.h"
#include "renderer/world/PlanetVoxelTextureManager.h"
#include "utils/common/BaseModule.h"

namespace vp {
class PlanetRendererModule : public utils::BaseModule<PlanetRendererModule> {

public:
    PlanetRendererModule(flecs::world& ecs) : BaseModule(ecs) { init_renderers(ecs); }

private:
    std::unique_ptr<PlanetSurfaceChunkRenderer> m_surfaceChunkRenderer;
    std::unique_ptr<PlanetTileRenderer> m_tileRenderer;
    std::unique_ptr<PlanetTileAtlas> m_tileAtlas;
    std::unique_ptr<PlanetVoxelTextureManager> m_voxelTextureManager;

    void init_renderers(flecs::world& ecs);

    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp
