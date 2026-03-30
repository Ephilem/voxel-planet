//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"
#include <glm/glm.hpp>

#include "PlayerControllerSystem.h"
#include "core/GameState.h"
#include "core/main_components.h"
#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "core/physics/physics_components.h"
#include "core/world/world_components.h"
#include "platform/inputs/input_state.h"
#include "renderer/rendering_components.h"
#include "debug/DebugUISystem.h"
#include "debug/LogConsole.h"
#include "debug/world/WorldInfo.h"
#include "debug/entities/PlayerPanel.h"
#include "debug/performance/FpsCounter.h"
#include "debug/renderer/VoxelBufferVisualizer.h"
#include "debug/world/ChunkManagerPanel.h"
#include "debug/world/WorldGenPanel.h"

ClientModule::ClientModule(flecs::world &ecs) {
    DebugUISystem::Register(ecs);
    auto* debugUI = ecs.get_mut<DebugUISystem>();
    debugUI->add_panel<FpsCounter>();
    debugUI->add_panel<WorldInfo>();
    debugUI->add_panel<LogConsole>();
    debugUI->add_panel<VoxelBufferVisualizer>();
    debugUI->add_panel<ChunkManagerPanel>();
    debugUI->add_panel<WorldGenPanel>();
    debugUI->add_panel<PlayerPanel>();

    PlayerControllerSystem::Register(ecs);

    ecs.system<Orientation>("MouseLookSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, Orientation& orientation) {
            VOXEL_ZONE_N("ClientModule-MouseLook");
            auto* inputState = e.world().get_mut<InputState>();
            if (!inputState->mouseCaptured) return;

            float sensitivity = -0.1f;
            orientation.yaw += inputState->mouseDeltaX * sensitivity;
            orientation.yaw = fmod(orientation.yaw, 360.0f);
            orientation.pitch += inputState->mouseDeltaY * sensitivity;
            orientation.pitch = fmod(orientation.pitch, 360.0f);

            if (orientation.pitch > 89.0f) orientation.pitch = 89.0f;
            if (orientation.pitch < -89.0f) orientation.pitch = -89.0f;
        });

    ecs.entity("Player")
        .add<Player>()
        .set<Position>({8.0f, 120.0f, 8.0f})
        .set<Camera3dParameters>({
            .fov = 80.0f
        })
        .set<ChunkLoader>({
            .loadRadius = 16,
            .unloadRadius = 18
        })
        .set<Velocity>({})
        .set<Orientation>({0.0f, 0.0f, 0.0f})
        .set<Camera3d>({})
        .set<Gravity>({ 0.0f, -9.81f, 0.0f })
        .set<RigidBody>({
            .haftExtent = {0.3f, 0.9f, 0.3f}
        })
        .set<PlayerController>({});
}

void shutdown_client(flecs::world &ecs) {
    LOG_INFO("ClientModule", "Shutting down...");
}
