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

    glm::vec3 forward() const {
        float cosPitch = cos(glm::radians(pitch));
        float sinPitch = sin(glm::radians(pitch));
        float cosYaw = cos(glm::radians(yaw));
        float sinYaw = sin(glm::radians(yaw));

        return {
            cosPitch * sinYaw,
            -sinPitch,
            cosPitch * cosYaw
        };
    }

    operator glm::vec3() const { return vec; }
};