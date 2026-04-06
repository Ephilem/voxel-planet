//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"

#include "player/PlayerControllerSystem.h"
#include "debug/DebugUIModule.h"
#include "core/main_components.h"
#include "core/physics/physics_components.h"
#include "core/world/spatial/spatial_prefabs.h"
#include "player/PlayerClientModule.h"
#include "player/player_components.h"
#include "renderer/rendering_components.h"

void vp::ClientModule::register_components(flecs::world &ecs) {
}

void vp::ClientModule::register_systems(flecs::world &ecs) {
}

void vp::ClientModule::register_pipelines(flecs::world &ecs) {
}

void vp::ClientModule::register_submodules(flecs::world &ecs) {
    ecs.import<DebugUIModule>();
    ecs.import<PlayerClientModule>();
}

void vp::ClientModule::register_entities(flecs::world &ecs) {
}


