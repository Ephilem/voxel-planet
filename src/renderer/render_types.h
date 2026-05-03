#pragma once

#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} Vertex3d;

struct TerrainFace3d {
    // packed1 layout: x:5 | y:5 | z:9 | faceIndex:3 | padding:10
    //
    // x, y : voxel position on surface plane (0-31)
    // z    : sub-voxel altitude (0-511), z / 16.0 = world-space voxel units
    //   +Z face  -> z = voxelZ * 16 + blockHeight  (exact top-face position)
    //   side faces -> z = voxelZ * 16 + nbH         (base + neighbor height offset)
    //   -Z face  -> z = voxelZ * 16
    uint32_t x : 5{};
    uint32_t y : 5{};
    uint32_t z : 9{};
    uint32_t faceIndex : 3{};
    uint32_t padding : 10 = 0;

    // packed2 layout: width:9 | height:9 | textureSlot:14
    //
    // Width and height in sub-voxel units (stored as value - 1).
    // actual_voxel_size = (stored + 1) / 16.0
    //
    // For side faces, the height field encodes the partial block height along Z:
    //   partial block (blkH < 15) -> stored = blkH - nbH
    //   full / merged blocks      -> stored = mergedVoxels * 16 - 1
    // Max value: 511 -> 512/16 = 32 voxels (full chunk side)
    uint32_t width : 9{};
    uint32_t height : 9{};
    uint32_t textureSlot : 14{};
};