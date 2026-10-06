#pragma once

#include <glm/glm.hpp>

namespace vp::client {

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
};

struct MainCamera {
};
}