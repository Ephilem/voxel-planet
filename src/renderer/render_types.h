#pragma once

#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} Vertex3d;

struct TerrainVertex3d {
    uint32_t x : 10;
    uint32_t y : 10;
    uint32_t z : 10;
    uint32_t padding : 2;
};