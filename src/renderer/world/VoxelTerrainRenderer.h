#pragma once

#include <flecs.h>
#include <vector>

#include "VoxelBuffer.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/WorldComponents.h"
#include "glm/vec3.hpp"
#include "nvrhi/nvrhi.h"

class VulkanBackend;
struct Renderer;


class VoxelTerrainRenderer {
public:
    VoxelTerrainRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem);
    ~VoxelTerrainRenderer();

    // create instance and register components/systems in the ECS
    static void Register(flecs::world& ecs);

private:
    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;

    std::vector<VoxelBuffer> m_chunkBuffers;
    glm::vec3 m_cameraPosition{0.0f};

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;

    nvrhi::GraphicsPipelineHandle m_pipeline;

    void init();
    void destroy();

    void build_voxel_chunk_mesh_system(
        VoxelChunk &chunk,
        VoxelChunkMesh &mesh);

    void upload_chunk_mesh_system(
        const VoxelChunk &chunk,
        VoxelChunkMesh &mesh);

    void render_terrain_system(
        Renderer &renderer);

public:
    // Debug accessor for buffer visualization
    const std::vector<VoxelBuffer>& get_voxel_buffers() const { return m_chunkBuffers; }
};