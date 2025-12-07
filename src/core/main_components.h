#pragma once

#include <glm/glm.hpp>

struct Position : glm::vec3 { using glm::vec3::vec3; };
struct Velocity : glm::vec3 { using glm::vec3::vec3; };
struct Scale : glm::vec3 { using glm::vec3::vec3; };

// pitch, yaw, roll in degrees
struct Orientation {
    union {
        glm::vec3 vec;
        struct { float pitch, yaw, roll; };
    };

    Orientation(float p = 0, float y = 0, float r = 0) : vec(p, y, r) {}

    operator glm::vec3() const { return vec; }
};