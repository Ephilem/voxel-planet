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
    glm::vec3 rot{0.0f};
    glm::vec3 scale{1.0f};
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

///////////////////////////////////////////////////////////

struct Player {};

struct Position : glm::vec3 { using glm::vec3::vec3; };
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