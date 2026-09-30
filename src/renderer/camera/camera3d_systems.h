#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/detail/type_quat.hpp"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"

namespace vp::systems {
void update_camera_third_person_system(Camera3d& camera, const glm::vec3& cameraPos, const glm::vec3& target,
                                       const glm::vec3& up) {
    camera.viewMatrix = glm::lookAt(cameraPos, target, up);
}

void update_camera_view_system(Camera3d& camera, const glm::vec3& position, const glm::quat& rot) {
    const glm::quat orientation = glm::normalize(rot);
    camera.viewMatrix = glm::mat4_cast(glm::conjugate(orientation)) * glm::translate(glm::mat4(1.0f), -position);
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
