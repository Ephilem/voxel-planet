//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"
#include <glm/glm.hpp>

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

    ecs.system<Movement>("Client-MovementSpeedControl")
        .with<Player>()
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Movement& movement) {
            VOXEL_ZONE_N("ClientModule-MovementSpeedControl");
            const auto* inputState = e.world().get<InputActionState>();

            if (inputState->is_action_pressed(ActionInputType::Accelerate)) {
                movement.speed *= 2.0f;
            }
            if (inputState->is_action_pressed(ActionInputType::Slowdown)) {
                movement.speed *= 0.5f;
            }
        });

    ecs.system<const Orientation, Movement>("BasicCameraMovementSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, const Orientation& orientation, Movement& movement) {
            VOXEL_ZONE_N("ClientModule-MovementSystem");
            const auto* inputState = e.world().get<InputActionState>();

            glm::vec3 forward = glm::vec3(
                cos(glm::radians(orientation.pitch)) * sin(glm::radians(orientation.yaw)),
                0,
                cos(glm::radians(orientation.pitch)) * cos(glm::radians(orientation.yaw))
            );

            // Right vector is always horizontal
            glm::vec3 right = glm::vec3(
                cos(glm::radians(orientation.yaw)),
                0.0f,
                -sin(glm::radians(orientation.yaw))
            );

            glm::vec3 direction = glm::vec3(0.0f);
            if (inputState->is_action_active(ActionInputType::Forward))
                direction += forward;
            if (inputState->is_action_active(ActionInputType::Backward))
                direction -= forward;
            if (inputState->is_action_active(ActionInputType::Left))
                direction += right;
            if (inputState->is_action_active(ActionInputType::Right))
                direction -= right;
            if (inputState->is_action_active(ActionInputType::Up))
                direction += glm::vec3(0.0f, 1.0f, 0.0f);
            if (inputState->is_action_active(ActionInputType::Down))
                direction -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (direction == glm::vec3(0.0f)) {
                movement.direction = direction;
                return;
            }

            movement.direction = glm::normalize(direction);
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
        .set<Movement>({ .speed = 50.0f });
}

void shutdown_client(flecs::world &ecs) {
    LOG_INFO("ClientModule", "Shutting down...");
}
