#pragma once

#include <flecs.h>
#include <vector>

#include "VoxelBuffer.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/world_components.h"
#include "nvrhi/nvrhi.h"

struct Camera3d;
class VulkanBackend;
struct Renderer;

// UBO to store in the GPU for terrain rendering
struct alignas(16) TerrainUBO {
    glm::mat4 view;
    glm::mat4 projection{1};
    float time;
};

class VoxelTerrainRenderer {
public:
    VoxelTerrainRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem);
    ~VoxelTerrainRenderer();

    // create instance and register components/systems in the ECS
    static void Register(flecs::world& ecs);

private:
    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;

    // UBO
    nvrhi::BufferHandle m_uboBuffer;
    TerrainUBO m_ubo; // CPU-side copy of the UBO. Will be use to know when to update the GPU UBO. (compare the camera entity value with this one)
    nvrhi::BindingLayoutHandle m_uboBindingLayout;
    nvrhi::BindingSetHandle m_uboBindingSet;

    std::vector<VoxelBuffer> m_chunkBuffers;

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;

    nvrhi::GraphicsPipelineHandle m_pipeline;

    void init();
    void destroy();

    bool build_voxel_chunk_mesh_system(
        const VoxelChunk &chunk,
        VoxelChunkMesh &mesh);

    bool upload_chunk_mesh_system(
        nvrhi::CommandListHandle cmd,
        VoxelChunkMesh &mesh);

    void render_terrain_system(
        Renderer &renderer,
        Camera3d &camera);

public:
    // Debug accessor for buffer visualization
    const std::vector<VoxelBuffer>& get_voxel_buffers() const { return m_chunkBuffers; }
};