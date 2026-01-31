#pragma once

#include <vector>
#include "render_types.h"

struct Camera3dParameters {
    glm::float32 fov = 45.0f;
};

struct Camera3d {
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::float32 nearClip = 0.1f;
    glm::float32 farClip = 1000.0f;
    glm::float32 aspect_ratio = 16.0f / 9.0f;
};

namespace voxel_chunk_mesh_state {
    struct Clean {};
    struct Dirty {};
    struct Meshing {};
    struct ReadyForUpload {};
}
struct VoxelChunkMeshState {};

struct VoxelChunkMesh {
    // GPU side info
    uint32_t bufferIndex = UINT32_MAX;

    uint32_t faceRegionStart = UINT32_MAX;
    uint32_t faceRegionCount = 0;

    uint32_t drawSlotIndex = UINT32_MAX;

    // CPU side info
    std::vector<TerrainFace3d> faces;

    uint32_t faceCount = 0;

    bool is_allocated() const {
        return faceRegionStart != UINT32_MAX &&
               drawSlotIndex != UINT32_MAX &&
               bufferIndex != UINT32_MAX;
    }
};