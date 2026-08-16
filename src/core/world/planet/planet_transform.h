#pragma once
#include <cmath>

#include <glm/glm.hpp>

#include "planet_types.h"

namespace vp {
/**
 * Tangent warp
 * @param s face coordinate in [-1, 1]
 */
inline double face_warp(double s) {
    return std::tan(s * 0.78539816339744830961); // s * pi/4
}

/// Inverse of face_warp
inline double face_warp_inv(double t) {
    return std::atan(t) * 1.27323954473516268615; // * 4/pi
}

/**
 * Face coordinates -> unit direction
 * @param u,v in [-1, 1], centered on the face
 */
inline glm::dvec3 face_uv_to_direction(CubemapFace face, double u, double v) {
    const double a = face_warp(u);
    const double b = face_warp(v);

    glm::dvec3 dir;
    switch (face) {
    case FACE_POS_X:
        dir = {1.0, b, -a};
        break;
    case FACE_NEG_X:
        dir = {-1.0, b, a};
        break;
    case FACE_POS_Y:
        dir = {a, 1.0, -b};
        break;
    case FACE_NEG_Y:
        dir = {a, -1.0, b};
        break;
    case FACE_POS_Z:
        dir = {a, b, 1.0};
        break;
    case FACE_NEG_Z:
        dir = {-a, b, -1.0};
        break;
    default:
        return {0.0, 0.0, 1.0};
    }

    return glm::normalize(dir);
}

/**
 * Unit direction -> face coordinates
 * @param out_u,out_v in [-1, 1]
 */
inline void direction_to_face_uv(const glm::dvec3& dir, CubemapFace& out_face, double& out_u, double& out_v) {
    const double ax = std::abs(dir.x);
    const double ay = std::abs(dir.y);
    const double az = std::abs(dir.z);

    double a, b;

    if (ax >= ay && ax >= az) {
        if (dir.x > 0.0) {
            out_face = FACE_POS_X;
            a = -dir.z / ax;
            b = dir.y / ax;
        } else {
            out_face = FACE_NEG_X;
            a = dir.z / ax;
            b = dir.y / ax;
        }
    } else if (ay >= az) {
        if (dir.y > 0.0) {
            out_face = FACE_POS_Y;
            a = dir.x / ay;
            b = -dir.z / ay;
        } else {
            out_face = FACE_NEG_Y;
            a = dir.x / ay;
            b = dir.z / ay;
        }
    } else {
        if (dir.z > 0.0) {
            out_face = FACE_POS_Z;
            a = dir.x / az;
            b = dir.y / az;
        } else {
            out_face = FACE_NEG_Z;
            a = -dir.x / az;
            b = dir.y / az;
        }
    }

    out_u = face_warp_inv(a);
    out_v = face_warp_inv(b);
}

/**
 * Arc length of a face edge, measured on the sphere.
 *
 * The tangent warp is what makes this exact: face_warp is tan(s * pi/4) and the
 * projection back to an angle is atan, so the two cancel and the angle is linear
 * in the face coordinate. A face therefore covers pi/2 of arc, split evenly,
 * which is the whole point of warping instead of projecting a flat cube
 */
inline double planet_face_size(double radius) {
    return radius * 1.5707963267948966192; // R * pi/2
}

inline double planet_node_size(double radius, uint8_t level) {
    return planet_face_size(radius) / double(1u << level);
}

inline double planet_voxel_size(double radius, uint8_t maxLevel, uint32_t chunkSize = CHUNK_SIZE) {
    return planet_node_size(radius, maxLevel) / double(chunkSize);
}

inline double planet_radius_for_voxel_size(double voxelSize, uint8_t maxLevel, uint32_t chunkSize = CHUNK_SIZE) {
    return voxelSize * double(chunkSize) * double(1u << maxLevel) / 1.5707963267948966192; // / (pi/2)
}

inline double planet_snap_radius(double radius, uint8_t maxLevel, uint32_t chunkSize = CHUNK_SIZE) {
    const double voxels = std::round(planet_voxel_size(radius, maxLevel, chunkSize));
    return planet_radius_for_voxel_size(voxels < 1.0 ? 1.0 : voxels, maxLevel, chunkSize);
}

/**
 * Whether the voxel size is a whole number of meters, within a millimeter.
 *
 * A radius rounded to the meter cannot hit the target exactly, since the pi/2
 * factor is irrational, so an exact test would reject every usable value
 */
inline bool planet_is_voxel_aligned(double radius, uint8_t maxLevel, uint32_t chunkSize = CHUNK_SIZE) {
    const double voxel = planet_voxel_size(radius, maxLevel, chunkSize);
    return std::abs(voxel - std::round(voxel)) < 1e-3;
}

inline double planet_chunk_voxel_size(uint8_t level) {
    return PLANET_VOXEL_SIZE_LOD0 * double(1U << level);
}

inline double planet_voxels_per_face_side(double radius, uint8_t level) {
    return planet_face_size(radius) / planet_chunk_voxel_size(level);
}

inline PlanetVoxelCoord planet_pos_to_voxel(const glm::dvec3& posPlanet, double radius, uint8_t level) {
    const double r = glm::length(posPlanet);
    PlanetVoxelCoord out;
    if (r < 1e-9)
        return out;

    double u;
    double v;
    direction_to_face_uv(posPlanet / r, out.face, u, v);

    const double N = planet_voxels_per_face_side(radius, level);
    const double voxelSize = planet_chunk_voxel_size(level);

    out.voxel.x = int64_t(std::floor(((u * 0.5) + 0.5) * N));
    out.voxel.y = int64_t(std::floor(((v * 0.5) + 0.5) * N));
    out.voxel.z = int64_t(std::floor((r - radius) / voxelSize));
    return out;
}

} // namespace vp
