#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "core/main_components.h"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"

namespace vp::systems {
void update_camera_third_person_system(Camera3d &camera, const glm::vec3 &cameraPos, const glm::vec3 &target) {
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.viewMatrix = glm::lookAt(cameraPos, target, worldUp);
}

void update_camera_view_system(Camera3d &camera, const glm::vec3 &position, const glm::vec3 &rot,
                               const glm::vec3 &worldUp = glm::vec3(0.0f, 1.0f, 0.0f)) {
    float yawRad   = glm::radians(rot.y);
    float pitchRad = glm::radians(rot.x);

    // Build a stable reference tangent perpendicular to worldUp
    glm::vec3 absUp = glm::abs(worldUp);
    glm::vec3 helper = (absUp.x < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 refForward = glm::normalize(glm::cross(helper, worldUp));

    // Yaw: rotate refForward around worldUp
    glm::vec3 forward = glm::normalize(
        glm::rotate(glm::mat4(1.f), yawRad, worldUp) * glm::vec4(refForward, 0.f));
    // Pitch: rotate forward around the resulting right axis
    glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
    forward = glm::normalize(
        glm::rotate(glm::mat4(1.f), pitchRad, right) * glm::vec4(forward, 0.f));

    right = glm::normalize(glm::cross(worldUp, forward));
    glm::vec3 up = glm::cross(forward, right);

    glm::vec3 cameraPos = glm::vec3(position.x, position.y, position.z);
    camera.viewMatrix = glm::lookAt(cameraPos, cameraPos + forward, up);
}

void update_camera_projection_system(Camera3d &camera, const Camera3dParameters &parameters, const Renderer &renderer) {
    // calculate aspect ratio
    auto rendererParameters = renderer.backend->renderParameters;
    camera.aspect_ratio = static_cast<glm::float32>(rendererParameters.width) / static_cast<glm::float32>(
                              rendererParameters.height);

    camera.projectionMatrix = glm::perspective(
        glm::radians(parameters.fov),
        camera.aspect_ratio,
        camera.nearClip,
        camera.farClip);
}
}
