//
// Player controller: Walking and FreeCam modes
//

#include "PlayerControllerSystem.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "client/render/camera/camera3d_components.h"
#include "core/main_components.h"
#include "core/physics/physics_components.h"
#include "core/TracyIntegration.h"
#include "core/world/spatial/spatial_components.h"
#include "platform/inputs/input_state.h"
#include "player_components.h"

namespace vp::client {
void PlayerControllerSystem::Register(flecs::world& ecs) {

    // --- Mode switch (F6) ---
    // Walking has no movement system yet: it only adds gravity and a rigid body, nothing reads them
    ecs.system<PlayerController, core::Velocity>("PlayerController-ModeSwitch")
        .kind(flecs::OnUpdate)
        .with<core::Player>()
        .each([](flecs::entity e, PlayerController& ctrl, core::Velocity& vel) {
            const auto* actions = e.world().try_get<platform::InputActionState>();
            if (!actions->is_action_pressed(platform::ActionInputType::ToggleControllerMode))
                return;

            if (ctrl.mode == ControllerMode::Walking) {
                ctrl.mode = ControllerMode::FreeCam;
                // Disable gravity and collision while in freecam
                e.remove<core::Gravity>();
                e.remove<core::RigidBody>();
                vel = glm::vec3(0.0f);
            } else {
                ctrl.mode = ControllerMode::Walking;
                e.set<core::Gravity>({0.0f, -9.81f, 0.0f});
                e.set<core::RigidBody>({.haftExtent = {0.3f, 0.9f, 0.3f}});
                vel = glm::vec3(0.0f);
            }
        });

    // --- FreeCam movement ---
    ecs.system<core::Transform, PlayerController, core::Velocity, const core::LookAngles>("PlayerController-FreeCam")
        .kind(flecs::OnUpdate)
        .with<core::Player>()
        .each([](flecs::entity e, core::Transform& transform, PlayerController& ctrl, core::Velocity& vel,
                 const core::LookAngles& angles) {
            if (ctrl.mode != ControllerMode::FreeCam)
                return;

            const auto* actions = e.world().try_get<platform::InputActionState>();
            const auto* inputState = e.world().try_get<platform::InputState>();

            float dt = e.world().delta_time();

            if (inputState && inputState->scrollDeltaY != 0.0f) {
                constexpr float scrollSensitivity = 0.15f;
                ctrl.freeCamSpeedMultiplier *= std::pow(2.0f, inputState->scrollDeltaY * scrollSensitivity);
                ctrl.freeCamSpeedMultiplier = glm::clamp(ctrl.freeCamSpeedMultiplier, 0.01f, 1000000.0f);
            }

            float speed = ctrl.freeCamSpeed * ctrl.freeCamSpeedMultiplier;
            if (actions->is_action_active(platform::ActionInputType::Accelerate))
                speed *= ctrl.freeCamFastMult;
            if (actions->is_action_active(platform::ActionInputType::Slowdown))
                speed *= 0.25f;

            // Body rotation is aligned on the local up, the head pitch is added so we fly where we look
            const glm::quat body = glm::normalize(transform.rot);
            const float pitch = angles.pitch;
            const glm::quat look = body * glm::angleAxis(glm::radians(pitch), glm::vec3(1.f, 0.f, 0.f));

            glm::vec3 forward = look * glm::vec3(0.f, 0.f, -1.f);
            glm::vec3 right = body * glm::vec3(1.f, 0.f, 0.f);
            glm::vec3 up = body * glm::vec3(0.f, 1.f, 0.f);

            glm::vec3 dir = glm::vec3(0.0f);
            if (actions->is_action_active(platform::ActionInputType::Forward))
                dir += forward;
            if (actions->is_action_active(platform::ActionInputType::Backward))
                dir -= forward;
            if (actions->is_action_active(platform::ActionInputType::Left))
                dir -= right;
            if (actions->is_action_active(platform::ActionInputType::Right))
                dir += right;
            if (actions->is_action_active(platform::ActionInputType::Jump))
                dir += up;
            if (actions->is_action_active(platform::ActionInputType::Down))
                dir -= up;

            if (glm::length(dir) > 0.01f)
                dir = glm::normalize(dir);

            transform.pos.x += dir.x * speed * dt;
            transform.pos.y += dir.y * speed * dt;
            transform.pos.z += dir.z * speed * dt;
            vel = glm::vec3(0.0f);
        });

    // Only writes the look angles, Transform.rot is built by PlanetModule-SurfaceAlign
    ecs.system<core::LookAngles>("MouseLookSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, core::LookAngles& angles) {
            VOXEL_ZONE_N("ClientModule-MouseLook");
            auto* inputState = e.world().try_get_mut<platform::InputState>();
            if (!inputState->mouseCaptured)
                return;

            float sensitivity = 0.1f;
            angles.yaw -= inputState->mouseDeltaX * sensitivity;
            angles.pitch -= inputState->mouseDeltaY * sensitivity;

            angles.yaw = fmod(angles.yaw, 360.0f);
            angles.pitch = glm::clamp(angles.pitch, -89.0f, 89.0f);
        });
}
} // namespace vp::client
