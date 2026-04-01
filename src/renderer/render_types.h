#pragma once

#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} Vertex3d;

struct TerrainFace3d {
    // packed1 layout: x:5 | y:9 | z:5 | faceIndex:3 | padding:10
    //
    // x, z : voxel position (0-31)
    // y    : sub-voxel position (0-511), y / 16.0 = world-space voxel units
    //   +Y face  -> y = voxelY * 16 + blockHeight  (exact top-face position)
    //   all else -> y = voxelY * 16
    uint32_t x : 5{};
    uint32_t y : 9{};
    uint32_t z : 5{};
    uint32_t faceIndex : 3{};
    uint32_t padding : 10 = 0;

    // packed2 layout: width:9 | height:9 | textureSlot:14
    //
    // Width and height in sub-voxel units (stored as value - 1).
    // actual_voxel_size = (stored + 1) / 16.0
    //
    // For side faces, the Y-direction field encodes the partial block height:
    //   partial block (blkH < 15) -> stored = blkH  (= (blkH+1)/16 voxels tall)
    //   full / merged blocks      -> stored = mergedVoxels * 16 - 1
    // Max value: 511 -> 512/16 = 32 voxels (full chunk side)
    uint32_t width : 9{};
    uint32_t height : 9{};
    uint32_t textureSlot : 14{};
};