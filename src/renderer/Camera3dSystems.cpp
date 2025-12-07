//
// Created by raph on 07/12/2025.
//

#include "Camera3dSystems.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

void Camera3dSystems::Register(flecs::world &ecs) {
    // ecs.component<Camera3d>();
    // ecs.component<Camera3dParameters>();

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
    auto view = glm::mat4(1.0f);

    view = glm::rotate(view, glm::radians(orientation.yaw), glm::vec3(0.0f, 1.0f, 0.0f));   // Yaw
    view = glm::rotate(view, glm::radians(orientation.pitch), glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
    view = glm::rotate(view, glm::radians(orientation.roll), glm::vec3(0.0f, 0.0f, 1.0f));  // Roll

    // Apply translation
    view = glm::translate(view, -glm::vec3(position.x, position.y, position.z));

    camera.viewMatrix = view;
}