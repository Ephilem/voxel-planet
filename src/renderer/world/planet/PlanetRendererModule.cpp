//
// Created by raph on 05/08/2026.
//

#include "PlanetRendererModule.h"

#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "renderer/Renderer.h"

using namespace vp;

void PlanetRendererModule::init_renderers(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();

    m_tileRenderer = std::make_unique<PlanetTileRenderer>(
        renderer->backend.get(),
        gameState->resourceSystem.get()
    );
}

void PlanetRendererModule::register_components(flecs::world &ecs) {
    ecs.component<PlanetTileDrawList>();
}

void PlanetRendererModule::register_pipelines(flecs::world &ecs) {
}

void PlanetRendererModule::register_systems(flecs::world &ecs) {
    ecs.system<const Renderer, Camera3d>("PlanetRendererModule-RenderTile")
        .term_at(0).singleton()
        .kind(flecs::OnStore)
        .each([this](flecs::entity e, const Renderer &renderer, Camera3d &camera) {
            if (!renderer.frameContext.frameActive) return;
            VOXEL_ZONE_N("PlanetTileRenderer-Render");
            // The world comes from the entity: capturing the one passed to
            // register_systems would dangle, it is a reference to a local
            flecs::world ecs = e.world();
            m_tileRenderer->render_planets(renderer.frameContext.commandList, camera, ecs);
        });
}

void PlanetRendererModule::register_submodules(flecs::world &ecs) {
}

void PlanetRendererModule::register_entities(flecs::world &ecs) {
}
