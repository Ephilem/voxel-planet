#pragma once

#include <glm/glm.hpp>

// Position in a grid
struct GridCellCoord : glm::i64vec3 {
    using glm::i64vec3::i64vec3;

    GridCellCoord(const glm::i64vec3& v) : glm::i64vec3(v) {}
    GridCellCoord(const glm::ivec3& v) : glm::i64vec3(v) {}
};

struct Transform {
    glm::vec3 pos{0.0f};
    glm::vec3 rot{0.0f};   // x=pitch, y=yaw, z=roll (degrees)
    glm::vec3 scale{1.0f};

    glm::vec3 forward() const {
        const float cosPitch = cos(glm::radians(rot.x));
        const float sinPitch = sin(glm::radians(rot.x));
        const float cosYaw   = cos(glm::radians(rot.y));
        const float sinYaw   = sin(glm::radians(rot.y));

        return {
            cosPitch * sinYaw,
            -sinPitch,
            cosPitch * cosYaw
        };
    }
};

// Transform in the camera space, used for rendering
struct GlobalTransform : Transform {};
// For a grid, used to determine which grid cell an entity is in, and for spatial queries
struct LocalFloatingOriginTransform : Transform {};

// Tags
struct FloatingOrigin {};

// Spatial grid
struct Grid {
    double cellSize = 10'000.0; // in meter
};