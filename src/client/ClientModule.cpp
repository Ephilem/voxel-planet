//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"

#include "player/PlayerControllerSystem.h"
#include "debug/DebugUIModule.h"
#include "core/main_components.h"
#include "player/PlayerClientModule.h"
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
    ecs.entity("Player")
        .set<Camera3d>({})
        .set<Camera3dParameters>({
            .fov = 80.0f
        })
        .add<Transform>()
        .add<Player>();
}


