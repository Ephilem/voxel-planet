#pragma once

#include "core/resource/ResourceSystem.h"
#include "renderer/IRenderPass.h"
#include <glm/glm.hpp>

class DebugDrawRenderer : public IRenderPass {
public:
    DebugDrawRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem) : m_backend(backend), m_resourceSystem(resourceSystem) {
        init_gpu();
    }

    ~DebugDrawRenderer() override;

    void render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) override;
private:
    struct DebugDrawPushConstants {
        glm::mat4 viewProj;
    };

    void init_gpu();
    void destroy();

    // render steps
    void render_lines(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend);
    void render_points(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend);

    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;

    // PushConstants layout
    nvrhi::BindingLayoutHandle m_pushConstantLayout;

    nvrhi::InputLayoutHandle m_lineInputLayout;
    nvrhi::BufferHandle m_lineBuffer;
    nvrhi::GraphicsPipelineHandle m_linePipeline;

    nvrhi::BufferHandle m_pointBuffer;
    nvrhi::GraphicsPipelineHandle m_pointPipeline;
};