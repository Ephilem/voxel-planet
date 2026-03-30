//
// Created by raph on 07/12/2025.
//

#include "Camera3dSystems.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"
#include "platform/inputs/input_state.h"

void Camera3dSystems::Register(flecs::world &ecs) {
    ecs.component<Camera3d>();
    ecs.component<Camera3dParameters>();

    ecs.system<Camera3dParameters>("ToggleCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3dParameters &parameters) {
            const auto* actions = e.world().get<InputActionState>();
            if (!actions || !actions->is_action_pressed(ActionInputType::ToggleCameraView)) return;
            parameters.viewType = (parameters.viewType == CameraViewType::FirstPerson)
                ? CameraViewType::ThirdPerson
                : CameraViewType::FirstPerson;
        });

    ecs.system<Camera3d, const Position, const Orientation, const Camera3dParameters>("UpdateCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3d &camera, const Position &position, const Orientation &orientation, const Camera3dParameters &parameters) {
            VOXEL_ZONE_N("Camera-UpdateView");
            glm::vec3 playerPos = position;
            glm::vec3 eyePos = position;
            auto type = parameters.viewType;
            if (type == CameraViewType::FirstPerson) {
                if (auto* body = e.get<RigidBody>()) {
                    eyePos.y += body->haftExtent.y * 0.75f;
                }
                update_camera_view_system(camera, eyePos, orientation);
            } else if (type == CameraViewType::ThirdPerson) {
                float distance = 10.0f;
                float yawRad = glm::radians(orientation.yaw);
                float pitchRad = glm::radians(orientation.pitch);
                eyePos.x -= distance * cos(pitchRad) * sin(yawRad);
                eyePos.y -= distance * sin(pitchRad);
                eyePos.z -= distance * cos(pitchRad) * cos(yawRad);
                update_camera_third_person_system(camera, eyePos, playerPos);
            }
        });

    ecs.observer<Camera3d, const Camera3dParameters>("UpdateCameraProjectionSystem")
        .event(flecs::OnSet)
        .each([](flecs::entity e, Camera3d &camera, const Camera3dParameters &parameters) {
            VOXEL_ZONE_N("Camera-UpdateProjection");
            const auto* renderer = e.world().get<Renderer>();
            if (renderer) {
                update_camera_projection_system(camera, parameters, *renderer);
            }
        });
}

void Camera3dSystems::update_camera_projection_system(Camera3d &camera, const Camera3dParameters &parameters,
                                                      const Renderer &renderer) {
    // calculate aspect ratio
    auto rendererParameters = renderer.backend->renderParameters;
    camera.aspect_ratio = static_cast<glm::float32>(rendererParameters.width) / static_cast<glm::float32>(rendererParameters.height);

    camera.projectionMatrix = glm::perspective(
        glm::radians(parameters.fov),
        camera.aspect_ratio,
        camera.nearClip,
        camera.farClip);
}

void Camera3dSystems::update_camera_third_person_system(Camera3d &camera, const glm::vec3 &cameraPos, const glm::vec3 &target) {
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.viewMatrix = glm::lookAt(cameraPos, target, worldUp);
}

void Camera3dSystems::update_camera_view_system(Camera3d &camera, const glm::vec3 &position, const Orientation &orientation) {
    float yawRad = glm::radians(orientation.yaw);
    float pitchRad = glm::radians(orientation.pitch);

    glm::vec3 forward;
    forward.x = cos(pitchRad) * sin(yawRad);
    forward.y = sin(pitchRad);
    forward.z = cos(pitchRad) * cos(yawRad);
    forward = glm::normalize(forward);

    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));

    glm::vec3 up = glm::cross(forward, right);

    glm::vec3 cameraPos = glm::vec3(position.x, position.y, position.z);

    camera.viewMatrix = glm::lookAt(cameraPos, cameraPos + forward, up);
}