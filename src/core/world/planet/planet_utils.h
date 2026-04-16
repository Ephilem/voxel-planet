#pragma once

#include <glm/glm.hpp>

#include "planet_components.h"
#include "renderer/world/planet/PlanetQuadtree.h"

namespace vp {

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
     * Convert a planet chunk (Spherical coordinate on a sphere face) to a planet relative world coordinate)
     * @param c Chunk coord
     * @param radius the radius of the planet
     * @return coordinate
     */
    inline glm::dvec3 planet_chunk_to_world(const vp::PlanetChunkCoord &c, double radius) {
        double u = c.x * CHUNK_SIZE / radius;
        double v = c.y * CHUNK_SIZE / radius;
        glm::dvec3 cubeDir = face_to_cube_dir(c.face, u, v);
        glm::dvec3 dir = glm::normalize(cubeDir);
        double r = radius + static_cast<double>(c.altitude) * CHUNK_SIZE;
        return dir * r;
    }

}
