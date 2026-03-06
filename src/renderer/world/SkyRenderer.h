#pragma once

#include <flecs.h>
#include <nvrhi/nvrhi.h>
#include <glm/glm.hpp>

#include "renderer/IRenderPass.h"
#include "core/resource/ResourceSystem.h"

class VulkanBackend;
struct Camera3d;

struct alignas(16) SkyUBO {
    glm::mat4 inverseView;
    glm::mat4 inverseProjection;
    float time;
};

class SkyRenderer : public IRenderPass {
public:
    SkyRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem);
    ~SkyRenderer() override;

    void render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) override;

    static void Register(flecs::world& ecs);

private:
    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;

    SkyUBO m_ubo;
    nvrhi::BufferHandle m_uboBuffer;

    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;

    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;
    nvrhi::GraphicsPipelineHandle m_pipeline;

    void init();
    void destroy();
};
