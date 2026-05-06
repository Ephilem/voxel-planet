#pragma once

#include <glm/glm.hpp>

#include "planet_components.h"
#include "core/world/world_components.h"
#include "renderer/world/planet/PlanetQuadtree.h"

namespace vp {
    inline static float equiangular(float s) {
        return std::tan(s * (static_cast<float>(M_PI) / 4.0f));
    }

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
     * Convert a 2D position on a cube face to a point
     * on the sphere, using equiangular mapping.
     * @param face The cube face
     * @param pos Position in face-plane space, in meters (range: [-radius, radius])
     * @param radius Planet radius
     */
    inline glm::vec3 face_pos_to_sphere(CubeFace face, glm::vec2 pos, float radius) {
        float u = equiangular(pos.x / radius);
        float v = equiangular(pos.y / radius);
        return glm::normalize(glm::vec3(face_to_cube_dir(face, u, v))) * radius;
    }

    inline glm::vec3 planet_chunk_to_sphere_pos(const PlanetChunkCoord &c, float radius) {
        return face_pos_to_sphere(c.face, glm::vec2(c.x * CHUNK_SIZE, c.y * CHUNK_SIZE), radius);
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

    /**
     * Convert a planet-relative world position to a chunk coordinate.
     * @param planetRelPos Position relative to the planet center (in meters)
     * @param radius Planet radius (in meters)
     */
    inline PlanetChunkCoord world_pos_to_chunk_coord(const glm::dvec3& planetRelPos, double radius) {
        CubeFace face = dominant_face(planetRelPos);
        glm::dvec2 uv = dir_to_face_uv(face, planetRelPos);
        return {
            .face     = face,
            .x        = static_cast<int>(std::floor(uv.x * radius / CHUNK_SIZE)),
            .y        = static_cast<int>(std::floor(uv.y * radius / CHUNK_SIZE)),
            .altitude = static_cast<int>(std::floor((glm::length(planetRelPos) - radius) / CHUNK_SIZE)),
        };
    }
}
