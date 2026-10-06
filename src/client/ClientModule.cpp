//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"

#include "core/CoreModule.h"
#include "debug/DebugUIModule.h"
#include "platform/PlatformModule.h"
#include "player/PlayerClientModule.h"
#include "render/SceneRenderModule.h"
#include "renderer/RendererModule.h"

namespace vp::client {
void ClientModule::register_components(flecs::world& ecs) {
}

void ClientModule::register_systems(flecs::world& ecs) {
}

void ClientModule::register_pipelines(flecs::world& ecs) {
}

void ClientModule::register_submodules(flecs::world& ecs) {
    ecs.import<core::CoreModule>();
    ecs.import<platform::PlatformModule>();

    ecs.import<renderer::RendererModule>();
    ecs.import<SceneRenderModule>();

    ecs.import<DebugUIModule>();
    ecs.import<PlayerClientModule>();
}

void ClientModule::register_entities(flecs::world& ecs) {
}
}