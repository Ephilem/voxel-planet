#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "glm/detail/type_quat.hpp"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"

namespace vp::systems {
void update_camera_third_person_system(Camera3d& camera, const glm::vec3& cameraPos, const glm::vec3& target) {
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.viewMatrix = glm::lookAt(cameraPos, target, worldUp);
}

void update_camera_view_system(Camera3d& camera, const glm::vec3& position, const glm::quat& rot,
                               const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f)) {
    const glm::quat orientation = glm::normalize(rot);
    glm::vec3 forward = glm::normalize(orientation * glm::vec3(0.f, 0.f, -1.f));

    glm::vec3 right = glm::cross(forward, worldUp);
    if (glm::length(right) < 1e-6f) {
        glm::vec3 helper = (glm::abs(worldUp.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        right = glm::cross(forward, helper);
    }
    right = glm::normalize(right);
    glm::vec3 up = glm::cross(right, forward);

    camera.viewMatrix = glm::lookAt(position, position + forward, up);
}

void update_camera_projection_system(Camera3d& camera, const Camera3dParameters& parameters, const Renderer& renderer) {
    // calculate aspect ratio
    auto rendererParameters = renderer.backend->renderParameters;
    camera.aspect_ratio =
        static_cast<glm::float32>(rendererParameters.width) / static_cast<glm::float32>(rendererParameters.height);

    const float f = 1.0f / std::tan(glm::radians(parameters.fov) * 0.5f);
    const float aspect = float(rendererParameters.width) / float(rendererParameters.height);

    camera.projectionMatrix =
        glm::mat4(f / aspect, 0.f, 0.f, 0.f, 0.f, f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, camera.nearClip, 0.f);
}
} // namespace vp::systems
