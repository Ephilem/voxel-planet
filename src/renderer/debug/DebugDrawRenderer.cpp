#include "DebugDrawRenderer.h"

#include <algorithm>

#include "core/debug/DebugDraw.h"
#include "core/debug/DebugDrawModule.h"
#include "core/log/Logger.h"
#include "renderer/vulkan/VulkanBackend.h"

namespace vp::renderer {
void DebugDrawRenderer::render(nvrhi::CommandListHandle cmd, RenderView& renderView) {
    const auto& lines = core::DebugDraw::GetLines();
    draw_vertices(cmd, renderView, m_linePipeline, m_lineBuffer, lines.data(), lines.size());

    const auto& points = core::DebugDraw::GetPoints();
    draw_vertices(cmd, renderView, m_pointPipeline, m_pointBuffer, points.data(), points.size());
}

void DebugDrawRenderer::init_gpu() {
    std::shared_ptr<core::ShaderResource> vertexRes =
        m_resourceSystem->load<core::ShaderResource>("debug_draw.vert", core::ResourceType::SHADER);
    std::shared_ptr<core::ShaderResource> pixelRes =
        m_resourceSystem->load<core::ShaderResource>("debug_draw.frag", core::ResourceType::SHADER);

    auto vertexShader = m_backend->device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
                                                        vertexRes->get_data(), vertexRes->get_data_size());
    auto pixelShader = m_backend->device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
                                                       pixelRes->get_data(), pixelRes->get_data_size());

    auto pushConstantLayoutDesc =
        nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex)
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(DebugDrawPushConstants)));
    m_pushConstantLayout = m_backend->device->createBindingLayout(pushConstantLayoutDesc);

    /////////////////////// LINES PIPELINE ///////////////////////
    // lines vertex buffer
    auto lineBufferDesc = nvrhi::BufferDesc()
                          .setByteSize(16 * 1024 * 1024) // ~600k vertices
                          .setDebugName("DebugDrawLines")
                          .setIsVertexBuffer(true)
                          .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                          .setKeepInitialState(true);
    m_lineBuffer = m_backend->device->createBuffer(lineBufferDesc);

    // Rasterizer
    auto rasterizerState = nvrhi::RasterState()
                           .setCullMode(nvrhi::RasterCullMode::None)
                           .setFillMode(nvrhi::RasterFillMode::Solid)
                           .setDepthClipEnable(true);

    // Depth Stencil
    auto depthStencilState = nvrhi::DepthStencilState().setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(
        nvrhi::ComparisonFunc::GreaterOrEqual); // reverse z depth buffer

    auto framebufferInfo = nvrhi::FramebufferInfo()
                           .addColorFormat(m_backend->get_swapchain_format())
                           .setDepthFormat(m_backend->get_depth_format());

    nvrhi::VertexAttributeDesc vertexDecs[2] = {
        nvrhi::VertexAttributeDesc()
        .setName("POSITION")
        .setFormat(nvrhi::Format::RGB32_FLOAT)
        .setOffset(offsetof(core::DebugVertex, position))
        .setBufferIndex(0)
        .setElementStride(sizeof(core::DebugVertex)),
        nvrhi::VertexAttributeDesc()
        .setName("COLOR")
        .setFormat(nvrhi::Format::RGBA32_FLOAT)
        .setOffset(offsetof(core::DebugVertex, color))
        .setBufferIndex(0)
        .setElementStride(sizeof(core::DebugVertex)),
    };
    m_lineInputLayout = m_backend->device->createInputLayout(vertexDecs, 2, vertexShader);

    auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
                        .setVertexShader(vertexShader)
                        .setPixelShader(pixelShader)
                        .setInputLayout(m_lineInputLayout)
                        .setPrimType(nvrhi::PrimitiveType::LineList)
                        .setRenderState({
                            .depthStencilState = depthStencilState,
                            .rasterState = rasterizerState,
                        })
                        .addBindingLayout(m_pushConstantLayout);
    m_linePipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);

    /////////////////////// POINTS PIPELINE ///////////////////////
    auto pointBufferDesc = nvrhi::BufferDesc()
                           .setByteSize(1024 * 1024)
                           .setDebugName("DebugDrawPoints")
                           .setIsVertexBuffer(true)
                           .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                           .setKeepInitialState(true);
    m_pointBuffer = m_backend->device->createBuffer(pointBufferDesc);

    auto pointPipelineDesc = nvrhi::GraphicsPipelineDesc()
                             .setVertexShader(vertexShader)
                             .setPixelShader(pixelShader)
                             .setInputLayout(m_lineInputLayout)
                             .setPrimType(nvrhi::PrimitiveType::PointList)
                             .setRenderState({
                                 .depthStencilState = depthStencilState,
                                 .rasterState = rasterizerState,
                             })
                             .addBindingLayout(m_pushConstantLayout);
    m_pointPipeline = m_backend->device->createGraphicsPipeline(pointPipelineDesc, framebufferInfo);
}

void DebugDrawRenderer::draw_vertices(nvrhi::CommandListHandle cmd, RenderView& renderView,
                                      nvrhi::IGraphicsPipeline* pipeline, nvrhi::IBuffer* buffer,
                                      const void* vertices, size_t vertexCount) {
    if (vertexCount == 0)
        return;

    const size_t capacity = buffer->getDesc().byteSize / sizeof(core::DebugVertex);
    if (vertexCount > capacity) {
        LOG_WARN("DebugDrawRenderer", "{} debug vertices, only the first {} are drawn", vertexCount, capacity);
        vertexCount = capacity;
    }

    // upload CPU -> GPU
    cmd->writeBuffer(buffer, vertices, vertexCount * sizeof(core::DebugVertex));

    const auto extent = m_backend->get_swapchain_extent();
    DebugDrawPushConstants pc{renderView.projectionMatrix * renderView.viewMatrix};

    auto state = nvrhi::GraphicsState()
                 .setPipeline(pipeline)
                 .setFramebuffer(m_backend->get_current_framebuffer())
                 .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                     nvrhi::Viewport(0.f, float(extent.width), 0.f, float(extent.height), 0.f, 1.f)))
                 .addVertexBuffer({buffer, 0, 0});
    cmd->setGraphicsState(state);

    cmd->setPushConstants(&pc, sizeof(pc));

    nvrhi::DrawArguments drawArgs;
    drawArgs.vertexCount = uint32_t(vertexCount);
    cmd->draw(drawArgs);

    cmd->clearState();
}
}