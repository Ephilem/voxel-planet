//
// Created by raph on 07/12/2025.
//

#include "Camera3dSystems.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

void Camera3dSystems::Register(flecs::world &ecs) {
    ecs.component<Camera3d>();
    ecs.component<Camera3dParameters>();

    ecs.system<Camera3d, const Position, const Orientation>("UpdateCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3d &camera, const Position &position, const Orientation &orientation) {
            update_camera_view_system(camera, position, orientation);
        });

    ecs.observer<Camera3d, const Camera3dParameters>("UpdateCameraProjectionSystem")
        .event(flecs::OnSet)
        .each([](flecs::entity e, Camera3d &camera, const Camera3dParameters &parameters) {
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

void Camera3dSystems::update_camera_view_system(Camera3d &camera, const Position &position, const Orientation &orientation) {
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