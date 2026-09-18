#pragma once

#include "core/resource/ResourceSystem.h"
#include "PlanetSurfaceChunkBuffer.h"
#include "renderer/IRenderPass.h"
#include "renderer/world/PlanetVoxelTextureManager.h"
#include <nvrhi/nvrhi.h>

namespace vp {
class PlanetSurfaceChunkRenderer : public IRenderPass {
public:
    PlanetSurfaceChunkRenderer(VulkanBackend* backend, ResourceSystem* res, PlanetVoxelTextureManager* textures)
        : m_backend(backend), m_resourceSystem(res), m_textures(textures) {
        init_gpu();
    }

    ~PlanetSurfaceChunkRenderer() = default;

    void render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) override;

private:
    struct PushConstants {
        glm::mat4 viewProj;
        glm::vec3 camPosPlanet;
        float radius = 0.f;
        float voxelSize = 1.f;
        float bendStrength = 1.f;
        float _pad[2]{};
    };

    void init_gpu();

    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;
    PlanetVoxelTextureManager* m_textures = nullptr;

    std::unique_ptr<PlanetSurfaceChunkBuffer> m_chunkBuffer;

    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;
};
} // namespace vp
