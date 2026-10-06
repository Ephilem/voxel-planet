#pragma once

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "core/resource/ResourceSystem.h"
#include "renderer/rendering_components.h"

namespace vp::renderer {
class VulkanBackend;

class DebugDrawRenderer {
public:
    DebugDrawRenderer(VulkanBackend* backend, core::ResourceSystem* resourceSystem)
        : m_backend(backend), m_resourceSystem(resourceSystem) {
        init_gpu();
    }

    void render(nvrhi::CommandListHandle cmd, RenderView& renderView);

private:
    struct DebugDrawPushConstants {
        glm::mat4 viewProj;
    };

    void init_gpu();

    /// Uploads then draws the vertices, clamped to the buffer capacity
    void draw_vertices(nvrhi::CommandListHandle cmd, RenderView& renderView, nvrhi::IGraphicsPipeline* pipeline,
                       nvrhi::IBuffer* buffer, const void* vertices, size_t vertexCount);

    VulkanBackend* m_backend;
    core::ResourceSystem* m_resourceSystem;

    // PushConstants layout
    nvrhi::BindingLayoutHandle m_pushConstantLayout;

    nvrhi::InputLayoutHandle m_lineInputLayout;
    nvrhi::BufferHandle m_lineBuffer;
    nvrhi::GraphicsPipelineHandle m_linePipeline;

    nvrhi::BufferHandle m_pointBuffer;
    nvrhi::GraphicsPipelineHandle m_pointPipeline;
};
}