#pragma once

#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} Vertex3d;

struct TerrainVertex3d {
    // Up to 1024 in each axis for a chunk (16x16x16 voxels, each voxel 64 units)
    uint32_t x : 10;
    uint32_t y : 10;
    uint32_t z : 10;

    // Corner
    // {0,0} bottom left, {1,0} bottom right, {1,1} top right, {0,1} top left. A vertice can only have one UV coord, so we encode it in 2 bits.
    uint32_t u : 1;
    uint32_t v : 1;

    ////////

    uint16_t textureSlot : 13; // up to 8192 texture slots
    uint16_t faceIndex : 3;   // 0-5 for the 6 cube faces

    ////////

    uint16_t padding = 0;
};