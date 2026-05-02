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
    struct alignas(16) PlanetChunkOUB {
        glm::ivec4 coord; // x=face, y=chunkX, z=chunkY, w=altitude
    };

    struct alignas(16) PlanetSurfaceUBO {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 foPositionInPlanet;
        float planetRadius;
        float farPlane;
        float _padding[3];
    };

    class PlanetSurfaceTerrainRenderer : public IRenderPass {
    public:
        PlanetSurfaceTerrainRenderer() = default;
        ~PlanetSurfaceTerrainRenderer() override;

        void render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) override;

        static void Register(flecs::world &ecs);

        void set_fo_position(glm::vec3 foPos, float radius) {
            m_ubo.foPositionInPlanet = foPos;
            m_ubo.planetRadius = radius;
        }

    private:
        VulkanBackend *m_backend = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;
        VoxelTextureManager *m_textureManager = nullptr;

        PlanetSurfaceUBO m_ubo{};
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

        void system_upload_chunk_mesh(const Renderer *renderer, VoxelChunkMesh &mesh, const PlanetChunkCoord &coord);
        void system_initialize_chunk_mesh(flecs::entity e, const VoxelChunk &mesh, const PlanetChunkCoord &coord);
    };
}