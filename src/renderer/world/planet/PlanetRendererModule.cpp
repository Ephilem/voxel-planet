//
// Created by raph on 05/08/2026.
//

#include "PlanetRendererModule.h"

#include "client/world/planet/planet_client_components.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "renderer/Renderer.h"

using namespace vp;

void PlanetRendererModule::init_renderers(flecs::world& ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();

    m_tileAtlas = std::make_unique<PlanetTileAtlas>(renderer->backend.get());

    m_tileRenderer = std::make_unique<PlanetTileRenderer>(renderer->backend.get(), gameState->resourceSystem.get(),
                                                          m_tileAtlas.get());

    ecs.set<PlanetTileAtlasRef>({.atlas = m_tileAtlas.get(), .renderer = m_tileRenderer.get()});
}

void PlanetRendererModule::register_components(flecs::world& ecs) {
    ecs.component<PlanetTileDrawListComp>();
    ecs.component<PlanetTileAtlasRef>();
}

void PlanetRendererModule::register_pipelines(flecs::world& ecs) {}

void PlanetRendererModule::register_systems(flecs::world& ecs) {
    ecs.observer<const PlanetTileLodComp, const PlanetTerrainParams, const PlanetComp>(
           "PlanetRendererModule-SetupStreamer")
        .event(flecs::OnAdd)
        .each([](flecs::entity e, const PlanetTileLodComp& lod, const PlanetTerrainParams& terrainParams,
                 const PlanetComp& comp) {
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

    ecs.system<const Renderer>("PlanetRendererModule-BeginFrame")
        .term_at(0)
        .singleton()
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            while (it.next()) {
                auto renderer = it.field<const Renderer>(0);
                if (!renderer->frameContext.frameActive)
                    return;
                m_tileAtlas->begin_frame();
            }
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

void PlanetRendererModule::register_entities(flecs::world& ecs) {}
