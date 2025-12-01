#pragma once

struct MeshDirty {};
struct MeshUploaded {};

struct VoxelChunkMesh {
    uint32_t vertexRegionStart = UINT32_MAX;
    uint32_t vertexRegionCount = 0;

    uint32_t indexRegionStart = UINT32_MAX;
    uint32_t indexRegionCount = 0;

    uint32_t indirectRegionIndex = UINT32_MAX;

    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;

    bool isAllocated() const {
        return vertexRegionStart != UINT32_MAX &&
               indexRegionStart != UINT32_MAX &&
               indirectRegionIndex != UINT32_MAX;
    }
};
