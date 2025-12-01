#pragma once

#include <flecs.h>
#include <vector>

#include "core/resource/ResourceSystem.h"
#include "glm/vec3.hpp"
#include "nvrhi/nvrhi.h"

class VoxelBuffer;
class VulkanBackend;


class VoxelTerrainRenderer {
public:
    VoxelTerrainRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem);
    ~VoxelTerrainRenderer();

    // create instance and register components/systems in the ECS
    static void Register(flecs::world& ecs, VulkanBackend* backend);

private:
    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;

    std::vector<VoxelBuffer> m_voxelBuffers;
    glm::vec3 m_cameraPosition{0.0f};

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;

    nvrhi::GraphicsPipelineHandle m_pipeline;

    void init();
    void destroy();

    void build_voxel_chunk_mesh_system(flecs::iter& it,
                                       struct VoxelChunk& chunk,
                                       struct VoxelChunkMesh& mesh);

    void upload_chunk_mesh_system(flecs::iter& it,
                                  const struct VoxelChunk& chunk,
                                  struct VoxelChunkMesh& mesh);

    void render_terrain_system(flecs::iter& it,
                               struct Renderer& renderer);
};