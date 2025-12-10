#pragma once

#include <flecs.h>
#include <vector>

#include "VoxelBuffer.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/world_components.h"
#include "nvrhi/nvrhi.h"

struct Position;
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

    // Draw command and chunk additionnal data
    nvrhi::BufferHandle m_chunkOUBBuffer; // 8Mb
    nvrhi::BufferHandle m_indirectBuffer; // m_chunkOUBBuffer / sizeof(TerrainOUB) * sizeof(nvrhi::DrawIndexedIndirectArguments) = 2.5Mb

    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;

    std::vector<VoxelBuffer> m_chunkBuffers;

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;

    nvrhi::GraphicsPipelineHandle m_pipeline;

    void init();
    void destroy();

    /**
     * This will store in the indirect buffer a draw command for the given mesh.
     * It will also prepare the model matrix and store it in the chunk OUB buffer.
     * @param mesh The mesh to create the draw command for. The buffer index will be set in this struct
     * @param pos Position of the chunk in world space
     */
    void add_draw_command(VoxelChunkMesh& mesh, const Position& pos);

    bool build_voxel_chunk_mesh_system(
        const VoxelChunk &chunk,
        VoxelChunkMesh &mesh, const Position& pos);

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