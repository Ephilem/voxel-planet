//
// Player controller: Walking and FreeCam modes
//

#include "PlayerControllerSystem.h"

#include <glm/glm.hpp>

#include "core/main_components.h"
#include "core/physics/physics_components.h"
#include "platform/inputs/input_state.h"

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
    ecs.system<const Orientation, PlayerController, Velocity>("PlayerController-Walking")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity e, const Orientation& ori, PlayerController& ctrl, Velocity& vel) {
            if (ctrl.mode != ControllerMode::Walking) return;

            const auto* actions = e.world().get<InputActionState>();
            const auto* body    = e.get<RigidBody>();
            float dt = e.world().delta_time();

            // Horizontal direction from input (yaw only, no pitch)
            glm::vec3 forward = glm::vec3(
                sin(glm::radians(ori.yaw)),
                0.0f,
                cos(glm::radians(ori.yaw))
            );
            glm::vec3 right = glm::vec3(
                cos(glm::radians(ori.yaw)),
                0.0f,
                -sin(glm::radians(ori.yaw))
            );

            glm::vec3 wishDir = glm::vec3(0.0f);
            if (actions->is_action_active(ActionInputType::Forward))  wishDir += forward;
            if (actions->is_action_active(ActionInputType::Backward)) wishDir -= forward;
            if (actions->is_action_active(ActionInputType::Left))     wishDir += right;
            if (actions->is_action_active(ActionInputType::Right))    wishDir -= right;

            float targetSpeed = actions->is_action_active(ActionInputType::Sprint)
                ? ctrl.sprintSpeed : ctrl.walkSpeed;

            glm::vec2 wishVelXZ = (glm::length(wishDir) > 0.01f)
                ? glm::normalize(glm::vec2(wishDir.x, wishDir.z)) * targetSpeed
                : glm::vec2(0.0f);

            bool onGround = body && body->onGround;
            float accel = onGround ? ctrl.groundAccel : ctrl.airAccel;

            // Friction when on ground and no input
            if (onGround && glm::length(wishVelXZ) < 0.01f) {
                accel = ctrl.groundFriction;
            }

            glm::vec2 curXZ = { vel.x, vel.z };
            glm::vec2 newXZ = move_towards(curXZ, wishVelXZ, accel * dt);
            vel.x = newXZ.x;
            vel.z = newXZ.y;

            // Jump
            if (actions->is_action_pressed(ActionInputType::Jump) && onGround) {
                vel.y = ctrl.jumpForce;
            }
        });

    // --- FreeCam movement ---
    ecs.system<const Orientation, const PlayerController, Velocity>("PlayerController-FreeCam")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity e, const Orientation& ori, const PlayerController& ctrl, Velocity& vel) {
            if (ctrl.mode != ControllerMode::FreeCam) return;

            const auto* actions = e.world().get<InputActionState>();
            auto* pos = e.get_mut<Position>();
            if (!pos) return;

            float dt = e.world().delta_time();
            float speed = ctrl.freeCamSpeed;
            if (actions->is_action_active(ActionInputType::Accelerate))
                speed *= ctrl.freeCamFastMult;
            if (actions->is_action_active(ActionInputType::Slowdown))
                speed *= 0.25f;

            glm::vec3 forward = glm::vec3(
                cos(glm::radians(ori.pitch)) * sin(glm::radians(ori.yaw)),
                0,
                cos(glm::radians(ori.pitch)) * cos(glm::radians(ori.yaw))
            );
            glm::vec3 right = glm::vec3(
                cos(glm::radians(ori.yaw)),
                0.0f,
                -sin(glm::radians(ori.yaw))
            );
            glm::vec3 up = glm::vec3(0, 1, 0);

            glm::vec3 dir = glm::vec3(0.0f);
            if (actions->is_action_active(ActionInputType::Forward))  dir += forward;
            if (actions->is_action_active(ActionInputType::Backward)) dir -= forward;
            if (actions->is_action_active(ActionInputType::Left))     dir += right;
            if (actions->is_action_active(ActionInputType::Right))    dir -= right;
            if (actions->is_action_active(ActionInputType::Jump))     dir += up;
            if (actions->is_action_active(ActionInputType::Down))     dir -= up;

            if (glm::length(dir) > 0.01f)
                dir = glm::normalize(dir);

            pos->x += dir.x * speed * dt;
            pos->y += dir.y * speed * dt;
            pos->z += dir.z * speed * dt;
            vel = glm::vec3(0.0f);
        });
}
