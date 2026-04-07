#include "DebugDrawRenderer.h"

#include "../../core/debug/DebugDrawModule.h"
#include "core/debug/DebugDraw.h"

using namespace vp;
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "renderer/Renderer.h"

DebugDrawRenderer::~DebugDrawRenderer() {
    destroy();
}

void DebugDrawRenderer::render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) {
    render_lines(cmd, camera, backend);
    render_points(cmd, camera, backend);
}

void DebugDrawRenderer::init_gpu() {
    std::shared_ptr<ShaderResource> vertexRes = m_resourceSystem->load<ShaderResource>("debug_draw.vert", ResourceType::SHADER);
    std::shared_ptr<ShaderResource> pixelRes = m_resourceSystem->load<ShaderResource>("debug_draw.frag", ResourceType::SHADER);

    auto vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertexRes->get_data(), vertexRes->get_data_size());
    auto pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        pixelRes->get_data(), pixelRes->get_data_size());

    auto pushConstantLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex)
            .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(DebugDrawPushConstants)));
    m_pushConstantLayout = m_backend->device->createBindingLayout(pushConstantLayoutDesc);

    /////////////////////// LINES PIPELINE ///////////////////////
    // lines vertex buffer
    auto lineBufferDesc = nvrhi::BufferDesc()
            .setByteSize(1024 * 1024 * 10)
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
    auto depthStencilState = nvrhi::DepthStencilState()
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);

    auto framebufferInfo = nvrhi::FramebufferInfo()
            .addColorFormat(m_backend->get_swapchain_format())
            .setDepthFormat(m_backend->get_depth_format());

    nvrhi::VertexAttributeDesc vertexDecs[2] = {
        nvrhi::VertexAttributeDesc()
        .setName("POSITION")
        .setFormat(nvrhi::Format::RGB32_FLOAT)
        .setOffset(offsetof(DebugVertex, position))
        .setBufferIndex(0)
        .setElementStride(sizeof(DebugVertex)),
        nvrhi::VertexAttributeDesc()
        .setName("COLOR")
        .setFormat(nvrhi::Format::RGBA32_FLOAT)
        .setOffset(offsetof(DebugVertex, color))
        .setBufferIndex(0)
        .setElementStride(sizeof(DebugVertex)),
    };
    m_lineInputLayout = m_backend->device->createInputLayout(
        vertexDecs, 2,
        vertexShader
    );

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

    // std::shared_ptr<ShaderResource> pointVertRes = m_resourceSystem->load<ShaderResource>("debug_draw.vert", ResourceType::SHADER);
    // auto pointVertexShader = m_backend->device->createShader(
        // nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        // pointVertRes->get_data(), pointVertRes->get_data_size());

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

void DebugDrawRenderer::destroy() {
}

void DebugDrawRenderer::render_lines(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) {
    auto &lines = vp::DebugDraw::GetLines();
    if (lines.empty()) return;

    // upload CPU -> GPU
    cmd->writeBuffer(m_lineBuffer, lines.data(), lines.size() * sizeof(DebugVertex));

    auto extent = m_backend->get_swapchain_extent();

    DebugDrawPushConstants pc{camera.projectionMatrix * camera.viewMatrix};

    auto state = nvrhi::GraphicsState()
            .setPipeline(m_linePipeline)
            .setFramebuffer(m_backend->get_current_framebuffer())
            .setViewport(nvrhi::ViewportState()
                .addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
            .addVertexBuffer({m_lineBuffer, 0, 0});
    cmd->setGraphicsState(state);

    cmd->setPushConstants(&pc, sizeof(pc));

    nvrhi::DrawArguments drawArgs;
    drawArgs.vertexCount = lines.size();
    cmd->draw(drawArgs);

    cmd->clearState();
}

void DebugDrawRenderer::render_points(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) {
    auto &points = DebugDraw::GetPoints();
    if (points.empty()) return;

    cmd->writeBuffer(m_pointBuffer, points.data(), points.size() * sizeof(DebugVertex));

    auto extent = m_backend->get_swapchain_extent();
    DebugDrawPushConstants pc{camera.projectionMatrix * camera.viewMatrix};

    auto state = nvrhi::GraphicsState()
            .setPipeline(m_pointPipeline)
            .setFramebuffer(m_backend->get_current_framebuffer())
            .setViewport(nvrhi::ViewportState()
                .addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
            .addVertexBuffer({m_pointBuffer, 0, 0});
    cmd->setGraphicsState(state);

    cmd->setPushConstants(&pc, sizeof(pc));

    nvrhi::DrawArguments drawArgs;
    drawArgs.vertexCount = points.size();
    cmd->draw(drawArgs);

    cmd->clearState();
}
