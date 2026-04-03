#pragma once

#include <glm/glm.hpp>

namespace vp::renderer {
struct RenderView {
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};
} // namespace vp::renderer
