#pragma once
#include <flecs.h>

#include "nvrhi/nvrhi.h"
#include "renderer/IRenderPass.h"
#include "renderer/world/VoxelBuffer.h"
#include "renderer/world/VoxelMeshUploadBatcher.h"
#include "renderer/world/VoxelTextureManager.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/world_components.h"
#include "core/world/planet/planet_components.h"

namespace vp {
    struct alignas(16) SurfaceChunkOUB {
        glm::ivec4 coord; // Representation of SurfaceChunkCoord. x = u, y = v, z = alt
    };

    struct alignas(16) SurfaceSurfaceUBO {
        glm::mat4 view;
        glm::mat4 projection;
        float planetRadius;
        float farPlane;
        float _pad0;
        float _pad1;

        // Anchor frame in world (planet) space
        glm::vec4 anchorX;             // tangent right
        glm::vec4 anchorY;             // up (radial). always at alt = 0
        glm::vec4 anchorZ;             // tangent forward
        glm::vec4 anchorCameraPos;     // anchor position relative to camera (FO), computed CPU-side in double

        // .x = anchorFaceU
        // .y = anchorFaceV
        // .z = face index
        // .w = planet altitude
        glm::vec4 anchorFacePos;
    };

    class PlanetSurfaceTerrainRenderer : public IRenderPass {
    public:
        PlanetSurfaceTerrainRenderer() = default;
        ~PlanetSurfaceTerrainRenderer() override;

        void render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) override;

        static void Register(flecs::world &ecs);


    private:
        VulkanBackend *m_backend = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;
        VoxelTextureManager *m_textureManager = nullptr;

        SurfaceSurfaceUBO m_ubo{};
        nvrhi::BufferHandle m_uboBuffer;

        // Set 0: per-frame (UBO)
        nvrhi::BindingLayoutHandle m_frameBindingLayout;
        nvrhi::BindingSetHandle m_frameBindingSet;

        // Set 1: per-chunk OUB (chunk coords)
        nvrhi::BindingLayoutHandle m_oubBindingLayout;
        std::vector<nvrhi::BindingSetHandle> m_oubBindingSets;

        // Set 2: face buffer
        nvrhi::BindingLayoutHandle m_faceBindingLayout;
        std::vector<nvrhi::BindingSetHandle> m_faceBindingSets;

        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::GraphicsPipelineHandle m_pipeline;

        std::vector<VoxelBuffer> m_chunkBuffers;
        VoxelMeshUploadBatcher m_meshUploader;

        void init(flecs::world &ecs);
        void init_gpu();
        void destroy();

        VoxelBuffer &create_buffer();

        void system_upload_chunk_mesh(const Renderer *renderer, VoxelChunkMesh &mesh, const PlanetNodeCoord &coord);
        void system_initialize_chunk_mesh(flecs::entity e, const VoxelChunk &mesh, const PlanetNodeCoord &coord);
    };
}