#pragma once
#include <glm/glm.hpp>

#define CHUNK_SIZE 16

struct VoxelChunk {
    uint8_t data[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]; // voxel data
    std::unordered_map<AssetID, uint8_t> textureIDs; // textures, used to reduce space by mapping texture asset IDs to small indices
};




