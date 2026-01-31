//
// Created by raph on 17/01/2026.
//

#include "ClientModule.h"
#include <glm/glm.hpp>

#include "core/GameState.h"
#include "core/main_components.h"
#include "core/log/Logger.h"
#include "core/world/world_components.h"
#include "platform/inputs/input_state.h"
#include "renderer/rendering_components.h"

ClientModule::ClientModule(flecs::world &ecs) {
    ecs.system<Orientation>("MouseLookSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, Orientation& orientation) {
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

    ecs.system<Position, const Orientation>("BasicCameraMovementSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, Position& pos, const Orientation& orientation) {
            float moveSpeed = 50.0f;
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

            glm::vec3 velocity = glm::vec3(0.0f);

            if (inputState->is_action_active(ActionInputType::Forward))
                velocity += forward;
            if (inputState->is_action_active(ActionInputType::Backward))
                velocity -= forward;
            if (inputState->is_action_active(ActionInputType::Left))
                velocity += right;
            if (inputState->is_action_active(ActionInputType::Right))
                velocity -= right;
            if (inputState->is_action_active(ActionInputType::Up))
                velocity += glm::vec3(0.0f, 1.0f, 0.0f);
            if (inputState->is_action_active(ActionInputType::Down))
                velocity -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(velocity) > 0.0f) {
                velocity = glm::normalize(velocity) * moveSpeed * static_cast<float>(e.world().get<GameState>()->deltaTime);
                pos += velocity;
            }
        });

    ecs.entity("Player")
        .set<Position>({8.0f, 120.0f, 8.0f})
        .set<Camera3dParameters>({
            .fov = 80.0f
        })
        .set<ChunkLoader>({
            .loadRadius = 1,
            .unloadRadius = 2
        })
        .set<Orientation>({-89.0f, 0.0f, 0.0f})
        .set<Camera3d>({});
}

void shutdown_client(flecs::world &ecs) {
    LOG_INFO("ClientModule", "Shutting down...");
}
