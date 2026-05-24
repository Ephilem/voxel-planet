//
// Created by raph on 14/05/2026.
//

#include "PlanetClientModule.h"

#include "planet_client_components.h"
#include "SurfaceWindowManager.h"
#include "client/player/player_components.h"
#include "core/world/planet/planet_components.h"

using namespace vp;

void PlanetClientModule::register_components(flecs::world &ecs) {
    ecs.component<InPlanetSurface>();
    ecs.component<PlanetUpVector>();
    ecs.component<SurfaceAnchorComp>()
            .member<int8_t>("face", offsetof(SurfaceAnchorComp, face))
            .member<float>("anchorRight", 3, offsetof(SurfaceAnchorComp, anchorRight))
            .member<float>("anchorForward", 3, offsetof(SurfaceAnchorComp, anchorForward))
            .member<float>("anchorUp", 3, offsetof(SurfaceAnchorComp, anchorUp))
            .member<int32_t>("gridCenter", 2, offsetof(SurfaceAnchorComp, gridCenter))
            .member<int32_t>("gridOrigin", 2, offsetof(SurfaceAnchorComp, gridOrigin));
}

void PlanetClientModule::register_systems(flecs::world &ecs) {
    // When the player is near a planet surface, update its up vector from the surface anchor
    ecs.system<PlanetUpVector>("UpdatePlayerPlanetUpVector")
        .kind(flecs::OnUpdate)
        .with<PlayerClient>()
        .each([](flecs::entity playerE, PlanetUpVector& upVec) {
            // Find the planet this player is on via InPlanetSurface relationship
            flecs::entity planet;
            playerE.each<InPlanetSurface>([&](flecs::entity p) { planet = p; });
            if (!planet.is_valid()) return;

            // Find the SurfaceAnchor child of the planet
            planet.children([&](flecs::entity child) {
                if (const auto* anchor = child.get<SurfaceAnchorComp>()) {
                    upVec.up = anchor->anchorUp;
                }
            });
        });
}

void PlanetClientModule::register_pipelines(flecs::world &ecs) {
}

void PlanetClientModule::register_submodules(flecs::world &ecs) {
}

void PlanetClientModule::register_entities(flecs::world &ecs) {
    SurfaceWindowManager::Register(ecs);
}
