#pragma once

#include <glm/glm.hpp>
#define MAX_FRAMES_IN_FLIGHT 2

enum class CameraViewType : uint8_t {
    FirstPerson,
    ThirdPerson,
};

struct Camera3dParameters {
    glm::float32 fov = 45.0f;
    CameraViewType viewType = CameraViewType::FirstPerson;
};

struct Camera3d {
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::float32 nearClip = 0.1f;
    glm::float32 aspect_ratio = 16.0f / 9.0f;
};
