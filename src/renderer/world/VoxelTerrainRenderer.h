#pragma once

#include <flecs.h>
#include <vector>

#include "VoxelBuffer.h"
#include "VoxelMeshUploadBatcher.h"
#include "VoxelTextureManager.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/world_components.h"
#include "nvrhi/nvrhi.h"
#include "renderer/IRenderPass.h"

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

class VoxelTerrainRenderer : public IRenderPass {
public:
    VoxelTerrainRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem, VoxelTextureManager* textureManager);
    ~VoxelTerrainRenderer() override;

    void render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) override;
    void on_resize(uint32_t newWidth, uint32_t newHeight) override;

    // create instance and register components/systems in the ECS
    static void Register(flecs::world& ecs);

private:
    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;
    VoxelTextureManager* m_textureManager;

    // UBO
    nvrhi::BufferHandle m_uboBuffer;
    TerrainUBO m_ubo; // CPU-side copy of the UBO. Will be use to know when to update the GPU UBO. (compare the camera entity value with this one)

    nvrhi::TextureHandle m_hzbTexture; // Hierarchical Z-Buffer for occlusion culling
    uint32_t m_hzbMipCount = 0;
    uint32_t m_hzbWidth = 0;
    uint32_t m_hzbHeight = 0;

    nvrhi::ShaderHandle m_hzbGenerationShader;
    nvrhi::ComputePipelineHandle m_hzbGenComputePipeline;
    nvrhi::BindingLayoutHandle m_hzbGenBindingLayout;

    nvrhi::SamplerHandle m_hzbSampler; // for terrain culling shader to sample the HZB


    // Set 0: Per-frame bindings (camera/view data)
    nvrhi::BindingLayoutHandle m_frameBindingLayout;
    nvrhi::BindingSetHandle m_frameBindingSet;

    // Set 1: Per-buffer bindings (chunk data)
    nvrhi::BindingLayoutHandle m_bufferBindingLayout;
    std::vector<VoxelBuffer> m_chunkBuffers;
    std::vector<nvrhi::BindingSetHandle> m_chunkBufferBindingSets; // One binding set per VoxelBuffer

    // Set 2: Face information buffer (mesh data for chunks)
    nvrhi::BindingLayoutHandle m_faceBufferBindingLayout;
    std::vector<nvrhi::BindingSetHandle> m_chunkFaceBindingSets; // One binding set per VoxelBuffer

    // Compute: GPU culling pipeline
    nvrhi::ShaderHandle m_cullingShader;
    nvrhi::ComputePipelineHandle m_cullPipeline;

    // Set 0 compute: UBO
    nvrhi::BindingLayoutHandle m_computeFrameBindingLayout;
    nvrhi::BindingSetHandle m_computeFrameBindingSet;

    // Set 1 compute: for each VoxelBuffer (cullData + culledIndirect + culledCount)
    nvrhi::BindingLayoutHandle m_cullBindingLayout;
    std::vector<nvrhi::BindingSetHandle> m_cullBindingSets;

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;

    nvrhi::GraphicsPipelineHandle m_pipeline;

    // Used to manage the upload of batches. To flush before rendering
    // Flush will execute the vulkan command to copy data for each buffers
    VoxelMeshUploadBatcher m_meshUploader;

    void init();
    void init_render_pipeline();
    void init_culling_pipeline();
    void init_hzb();

    void destroy();

    bool upload_chunk_mesh_system(
        const Renderer *renderer,
        VoxelChunkMesh &mesh, const Position &pos);

    VoxelBuffer &create_buffer();

public:
    // Debug accessor for buffer visualization
    const std::vector<VoxelBuffer>& get_voxel_buffers() const { return m_chunkBuffers; }
};