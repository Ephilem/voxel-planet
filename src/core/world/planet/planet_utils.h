#pragma once

#include <glm/glm.hpp>

#include "planet_components.h"
#include "core/world/world_components.h"
#include "renderer/world/planet/PlanetQuadtree.h"

namespace vp {
    /**
     * Convert a cube face + 2D UV coordinates to a 3D direction vector (not normalized).
     * UV coordinates are in cube-space where 1.0 = planetRadius world units.
     * The returned vector lies on the unit cube face (one component is ±1).
     * Use glm::normalize() on the result to get the sphere surface direction.
     * @param face The cube face
     * @param u First axis coordinate in [-1, 1]
     * @param v Second axis coordinate in [-1, 1]
     * @return Un-normalized direction vector on the cube face
     */
    inline glm::dvec3 face_to_cube_dir(vp::CubeFace face, double u, double v) {
        switch (face) {
            case vp::CubeFace::PosX: return {1.0, v, u};
            case vp::CubeFace::NegX: return {-1.0, v, -u};
            case vp::CubeFace::PosY: return {u, 1.0, -v};
            case vp::CubeFace::NegY: return {u, -1.0, v};
            case vp::CubeFace::PosZ: return {u, v, 1.0};
            case vp::CubeFace::NegZ: return {-u, v, -1.0};
        }
        return {};
    }

    /**
     * Convert a planet chunk coordinate to a planet-relative world position.
     * The returned position is the center of the chunk's base voxel (lx=0, ly=0, lz=0)
     * projected onto the sphere surface at the chunk's altitude.
     * @param c    Chunk coordinate (face, x, y, altitude)
     * @param radius Planet radius in world units (meters)
     * @return Planet-relative world position of the chunk origin
     */
    inline glm::dvec3 planet_chunk_to_world(const PlanetChunkCoord &c, double radius) {
        double u = c.x * CHUNK_SIZE / radius;
        double v = c.y * CHUNK_SIZE / radius;
        glm::dvec3 cubeDir = face_to_cube_dir(c.face, u, v);
        glm::dvec3 dir = glm::normalize(cubeDir);
        double r = radius + static_cast<double>(c.altitude) * CHUNK_SIZE;
        return dir * r;
    }

    /**
     * Determine which cube face a direction vector points toward.
     * The face is determined by the axis with the largest absolute component.
     * The input does not need to be normalized.
     * @param dir Direction vector from the planet center (can be a world-space position
     *            relative to the planet center)
     * @return The cube face closest to the direction
     */
    inline CubeFace dominant_face(const glm::dvec3 &dir) {
        glm::dvec3 a = glm::abs(dir);
        if (a.x >= a.y && a.x >= a.z)
            return dir.x >= 0 ? CubeFace::PosX : CubeFace::NegX;
        if (a.y >= a.x && a.y >= a.z)
            return dir.y >= 0 ? CubeFace::PosY : CubeFace::NegY;
        return dir.z >= 0 ? CubeFace::PosZ : CubeFace::NegZ;
    }

    /**
     * Get the u, v coordinate on a face from a direction vector.
     * The direction is assumed to be on the correct face (validate with dominant_face).
     * Returns u, v in cube-space [-1, 1], where 1.0 corresponds to planetRadius world units.
     * To convert to chunk coordinates: cx = floor(u * radius / CHUNK_SIZE)
     * @param face Face of the coordinate
     * @param dir coordinate (does not need to be normalized)
     * @return u, v face coordinate in [-1, 1]
     */
    inline glm::dvec2 dir_to_face_uv(CubeFace face, const glm::dvec3 &dir) {
        switch (face) {
            case CubeFace::PosX:
            case CubeFace::NegX:
                return {dir.z / dir.x, dir.y / dir.x};

            case CubeFace::PosY:
            case CubeFace::NegY:
                return {dir.x / dir.y, -dir.z / dir.y};

            case CubeFace::PosZ:
            case CubeFace::NegZ:
                return {dir.x / dir.z, dir.y / dir.z};
        }
        return {};
    }
}
