#pragma once

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Position in a grid
namespace vp {

struct CellCoord : glm::i64vec3 {
    using glm::i64vec3::i64vec3;

    CellCoord(const glm::i64vec3& v) : glm::i64vec3(v) {}

    CellCoord(const glm::ivec3& v) : glm::i64vec3(v) {}

    glm::dvec3 operator*(const double other) const {
        return {static_cast<double>(x) * other, static_cast<double>(y) * other, static_cast<double>(z) * other};
    }
};

struct Transform {
    glm::vec3 pos{0.0f};
    glm::quat rot{1.0f, 0.f, 0.f, 0.f};
    glm::vec3 scale{1.0f};

    glm::vec3 forward() const { return rot * glm::vec3(0.0f, 0.0f, -1.0f); }
};

/**
 * Look angles of an entity, in degrees, written by the input or the AI
 */
// TODO maybe in another module
struct LookAngles {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

// Transform in the camera space, used for rendering
struct GlobalTransform {
    glm::dvec3 pos{0.0};
    glm::quat rot{1.0f, 0.f, 0.f, 0.f};
    glm::vec3 scale{1.0f};

    glm::vec3 forward() const { return rot * glm::vec3(0.0f, 0.0f, -1.0f); }
};

// Not a component, but contain information about the local floating origin from the grid.
struct LocalFloatingOrigin {
    CellCoord cell;
    glm::vec3 translation{0.f};
    glm::dquat rotation{1.0, 0.0, 0.0, 0.0};
};

// Tags
struct FloatingOrigin {};

// Spatial grid
struct Grid {
    double cellSize = 10'000.0; // in meter
    LocalFloatingOrigin localOrigin{};

    /**
     * Get the local grid position based on the cell coordinate and the local position within the cell.
     * @param cell CellCoords
     * @param localPos Local position within the cell, usually found in Transform component, as position
     * @return double precision coordinate relative to the grid origin
     */
    inline glm::dvec3 get_hp_grid_pos(const CellCoord& cell, const glm::vec3& localPos) const {
        return glm::dvec3(cell * cellSize) + glm::dvec3(localPos);
    }
};

struct SpatialRoot {
    flecs::entity floatingOrigin;
};

} // namespace vp
