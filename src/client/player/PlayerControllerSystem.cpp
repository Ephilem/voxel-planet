//
// Player controller: Walking and FreeCam modes
//

#include "PlayerControllerSystem.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "player_components.h"
#include "client/world/planet/planet_client_components.h"
#include "core/main_components.h"
#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"
#include "core/world/spatial/spatial_components.h"
#include "platform/inputs/input_state.h"
#include "renderer/rendering_components.h"

static glm::vec2 move_towards(glm::vec2 current, glm::vec2 target, float maxDelta) {
    glm::vec2 diff = target - current;
    float dist = glm::length(diff);
    if (dist <= maxDelta || dist < 1e-6f)
        return target;
    return current + (diff / dist) * maxDelta;
}

void PlayerControllerSystem::Register(flecs::world& ecs) {

    // --- Mode switch (F5) ---
    ecs.system<PlayerController, Velocity>("PlayerController-ModeSwitch")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity e, PlayerController& ctrl, Velocity& vel) {
            const auto* actions = e.world().get<InputActionState>();
            if (!actions->is_action_pressed(ActionInputType::ToggleControllerMode)) return;

            if (ctrl.mode == ControllerMode::Walking) {
                ctrl.mode = ControllerMode::FreeCam;
                // Disable gravity and collision while in freecam
                e.remove<Gravity>();
                e.remove<RigidBody>();
                vel = glm::vec3(0.0f);
            } else {
                ctrl.mode = ControllerMode::Walking;
                e.set<Gravity>({ 0.0f, -9.81f, 0.0f });
                e.set<RigidBody>({ .haftExtent = { 0.3f, 0.9f, 0.3f } });
                vel = glm::vec3(0.0f);
            }
        });

    // --- Walking input + physics ---
    ecs.system<const vp::Transform, PlayerController, Velocity>("PlayerController-Walking")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity e, const vp::Transform& transform, PlayerController& ctrl, Velocity& vel) {
            if (ctrl.mode != ControllerMode::Walking) return;

            const auto* actions = e.world().get<InputActionState>();
            const auto* body    = e.get<RigidBody>();
            float dt = e.world().delta_time();

            glm::vec3 worldUp = glm::vec3(0.f, 1.f, 0.f);
            if (const auto* planetUp = e.get<vp::PlanetUpVector>())
                worldUp = planetUp->up;

            // Build forward/right in the tangent plane of worldUp, yaw-only (no pitch)
            glm::vec3 absUp = glm::abs(worldUp);
            glm::vec3 helper = (absUp.x < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 refForward = glm::normalize(glm::cross(helper, worldUp));
            float yawRad = glm::radians(transform.rot.y);
            glm::vec3 forward = glm::normalize(
                glm::rotate(glm::mat4(1.f), yawRad, worldUp) * glm::vec4(refForward, 0.f));
            glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));

            glm::vec3 wishDir = glm::vec3(0.0f);
            if (actions->is_action_active(ActionInputType::Forward))  wishDir += forward;
            if (actions->is_action_active(ActionInputType::Backward)) wishDir -= forward;
            if (actions->is_action_active(ActionInputType::Left))     wishDir += right;
            if (actions->is_action_active(ActionInputType::Right))    wishDir -= right;

            float targetSpeed = actions->is_action_active(ActionInputType::Sprint)
                ? ctrl.sprintSpeed : ctrl.walkSpeed;

            glm::vec3 wishVel3 = (glm::length(wishDir) > 0.01f)
                ? glm::normalize(wishDir) * targetSpeed
                : glm::vec3(0.0f);

            bool onGround = body && body->onGround;
            float accel = onGround ? ctrl.groundAccel : ctrl.airAccel;

            // Project current velocity onto tangent plane for friction/accel
            glm::vec3 curTangent = vel - glm::dot(glm::vec3(vel), worldUp) * worldUp;
            float friction = (onGround && glm::length(wishVel3) < 0.01f) ? ctrl.groundFriction : accel;
            glm::vec2 newXZ = move_towards(
                glm::vec2(curTangent.x, curTangent.z),
                glm::vec2(wishVel3.x, wishVel3.z),
                friction * dt);
            vel.x = newXZ.x;
            vel.z = newXZ.y;

            // Jump
            if (actions->is_action_pressed(ActionInputType::Jump) && onGround) {
                vel.y = ctrl.jumpForce;
            }
        });

    // --- FreeCam movement ---
    ecs.system<vp::Transform, PlayerController, Velocity>("PlayerController-FreeCam")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity e, vp::Transform& transform, PlayerController& ctrl, Velocity& vel) {
            if (ctrl.mode != ControllerMode::FreeCam) return;

            const auto* actions    = e.world().get<InputActionState>();
            const auto* inputState = e.world().get<InputState>();

            float dt = e.world().delta_time();

            if (inputState && inputState->scrollDeltaY != 0.0f) {
                constexpr float scrollSensitivity = 0.15f;
                ctrl.freeCamSpeedMultiplier *= std::pow(2.0f, inputState->scrollDeltaY * scrollSensitivity);
                ctrl.freeCamSpeedMultiplier = glm::clamp(ctrl.freeCamSpeedMultiplier, 0.01f, 1000000.0f);
            }

            float speed = ctrl.freeCamSpeed * ctrl.freeCamSpeedMultiplier;
            if (actions->is_action_active(ActionInputType::Accelerate))
                speed *= ctrl.freeCamFastMult;
            if (actions->is_action_active(ActionInputType::Slowdown))
                speed *= 0.25f;

            glm::vec3 worldUp = glm::vec3(0.f, 1.f, 0.f);
            if (const auto* planetUp = e.get<vp::PlanetUpVector>())
                worldUp = planetUp->up;

            glm::vec3 absUp = glm::abs(worldUp);
            glm::vec3 helper = (absUp.x < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 refForward = glm::normalize(glm::cross(helper, worldUp));
            float yawRad   = glm::radians(transform.rot.y);
            float pitchRad = glm::radians(transform.rot.x);
            glm::vec3 forward = glm::normalize(
                glm::rotate(glm::mat4(1.f), yawRad, worldUp) * glm::vec4(refForward, 0.f));
            glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
            forward = glm::normalize(
                glm::rotate(glm::mat4(1.f), pitchRad, right) * glm::vec4(forward, 0.f));
            right = glm::normalize(glm::cross(worldUp, forward));
            glm::vec3 up = worldUp;

            glm::vec3 dir = glm::vec3(0.0f);
            if (actions->is_action_active(ActionInputType::Forward))  dir += forward;
            if (actions->is_action_active(ActionInputType::Backward)) dir -= forward;
            if (actions->is_action_active(ActionInputType::Left))     dir += right;
            if (actions->is_action_active(ActionInputType::Right))    dir -= right;
            if (actions->is_action_active(ActionInputType::Jump))     dir += up;
            if (actions->is_action_active(ActionInputType::Down))     dir -= up;

            if (glm::length(dir) > 0.01f)
                dir = glm::normalize(dir);

            transform.pos.x += dir.x * speed * dt;
            transform.pos.y += dir.y * speed * dt;
            transform.pos.z += dir.z * speed * dt;
            vel = glm::vec3(0.0f);
        });


    ecs.system<vp::Transform>("MouseLookSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, vp::Transform& transform) {
            VOXEL_ZONE_N("ClientModule-MouseLook");
            auto* inputState = e.world().get_mut<InputState>();
            if (!inputState->mouseCaptured) return;

            float sensitivity = -0.1f;
            transform.rot.y += inputState->mouseDeltaX * sensitivity;
            transform.rot.y = fmod(transform.rot.y, 360.0f);
            transform.rot.x -= inputState->mouseDeltaY * sensitivity;
            transform.rot.x = fmod(transform.rot.x, 360.0f);

            if (transform.rot.x > 89.0f) transform.rot.x = 89.0f;
            if (transform.rot.x < -89.0f) transform.rot.x = -89.0f;
        });
}
