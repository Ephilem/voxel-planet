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

            const glm::quat orientation = glm::normalize(transform.rot);
            glm::vec3 forward = orientation * glm::vec3(0.f, 0.f, -1.f);
            glm::vec3 right = orientation * glm::vec3(1.f, 0.f, 0.f);
            glm::vec3 up = worldUp;

            glm::vec3 dir = glm::vec3(0.0f);
            if (actions->is_action_active(ActionInputType::Forward))  dir += forward;
            if (actions->is_action_active(ActionInputType::Backward)) dir -= forward;
            if (actions->is_action_active(ActionInputType::Left))     dir -= right;
            if (actions->is_action_active(ActionInputType::Right))    dir += right;
            if (actions->is_action_active(ActionInputType::Jump))     dir += up;
            if (actions->is_action_active(ActionInputType::Down))     dir -= up;

            if (glm::length(dir) > 0.01f)
                dir = glm::normalize(dir);

            transform.pos.x += dir.x * speed * dt;
            transform.pos.y += dir.y * speed * dt;
            transform.pos.z += dir.z * speed * dt;
            vel = glm::vec3(0.0f);
        });


    ecs.system<vp::Transform, PlayerController>("MouseLookSystem")
    .kind(flecs::OnUpdate)
    .with<Camera3d>()
    .each([](flecs::entity e, vp::Transform& transform, PlayerController& ctrl) {
        VOXEL_ZONE_N("ClientModule-MouseLook");
        auto* inputState = e.world().get_mut<InputState>();
        if (!inputState->mouseCaptured) return;

        float sensitivity = 0.1f;
        ctrl.yaw -= inputState->mouseDeltaX * sensitivity;
        ctrl.pitch -= inputState->mouseDeltaY * sensitivity;

        ctrl.yaw = fmod(ctrl.yaw, 360.0f);
        ctrl.pitch = glm::clamp(ctrl.pitch, -89.0f, 89.0f);

        glm::quat yawQuat   = glm::angleAxis(glm::radians(ctrl.yaw),   glm::vec3(0, 1, 0));
        glm::quat pitchQuat = glm::angleAxis(glm::radians(ctrl.pitch), glm::vec3(1, 0, 0));
        transform.rot = glm::normalize(yawQuat * pitchQuat);
    });
}
