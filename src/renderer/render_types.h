#pragma once

#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} Vertex3d;

struct TerrainFace3d {
    // 6 bit for position of the face. 64 possible position each axis (32x32x32 chunk)
    uint32_t x : 6;
    uint32_t y : 6;
    uint32_t z : 6;

    // Direction of the face (so in the gpu, we can compute normal, uv's, etc)
    uint32_t faceIndex : 3; // 0-5 for the 6 cube faces

    uint32_t padding : 11 = 0;

    ////////

    uint16_t textureSlot : 16;
};