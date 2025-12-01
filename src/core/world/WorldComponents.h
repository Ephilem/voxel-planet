#pragma once
#include "glm/fwd.hpp"
#include "glm/vec3.hpp"

#define CHUNK_SIZE 16

struct VoxelChunk {
    glm::ivec3 position; // in chunk position
    uint8_t data[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]; // voxel data
};




