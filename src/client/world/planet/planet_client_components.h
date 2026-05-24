#pragma once

#include <glm/glm.hpp>

namespace vp {
    // -- tags --
    struct InPlanetSurface {};

    struct PlanetUpVector {
        glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);
    };
}