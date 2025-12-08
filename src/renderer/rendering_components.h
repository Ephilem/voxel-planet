#pragma once

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

struct Dirty {};
struct DirtyGpu {};

struct VoxelChunkMesh {
    // GPU side info
    uint32_t vertexRegionStart = UINT32_MAX;
    uint32_t vertexRegionCount = 0;

    uint32_t indexRegionStart = UINT32_MAX;
    uint32_t indexRegionCount = 0;

    uint32_t indirectRegionIndex = UINT32_MAX;

    // CPU side info
    std::vector<Vertex3d> vertices;
    std::vector<uint32_t> indices;

    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;

    bool is_allocated() const {
        return vertexRegionStart != UINT32_MAX &&
               indexRegionStart != UINT32_MAX &&
               indirectRegionIndex != UINT32_MAX;
    }
};