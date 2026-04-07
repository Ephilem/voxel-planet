#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Position in a grid
struct CellCoord : glm::i64vec3 {
    using glm::i64vec3::i64vec3;

    CellCoord(const glm::i64vec3& v) : glm::i64vec3(v) {}
    CellCoord(const glm::ivec3& v) : glm::i64vec3(v) {}

    glm::dvec3 operator*(const double other) const {
        return {
            static_cast<double>(x) * other,
            static_cast<double>(y) * other,
            static_cast<double>(z) * other
        };
    }
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

// Not a component, but contain information about the local floating origin from the grid.
struct LocalFloatingOrigin {
    CellCoord cell;
    glm::vec3 translation;
    glm::dquat rotation;

    glm::dmat4 transform;
    bool is_unchanged;
};

// Tags
struct FloatingOrigin {};

// Spatial grid
struct Grid {
    double cellSize = 10'000.0; // in meter
    LocalFloatingOrigin localOrigin{};
};

struct SpatialRoot {
    flecs::entity floatingOrigin;
};