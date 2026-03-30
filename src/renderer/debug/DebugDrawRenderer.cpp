#include "DebugDrawRenderer.h"

#include "core/DebugDrawManager.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"

DebugDrawRenderer::~DebugDrawRenderer() {
    destroy();
}

void DebugDrawRenderer::Register(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();
    if (!renderer) {
        LOG_ERROR("DebugDrawRenderer", "Cannot register: missing Renderer");
        return;
    }

    auto pass = std::make_unique<DebugDrawRenderer>(renderer->backend.get(), gameState->resourceSystem.get());
    auto* debugPass = pass.get();
    renderer->renderPasses.push_back(std::move(pass));

    ecs.system<Renderer>("DebugDrawRenderer-Render")
            .kind(flecs::OnStore)
            .each([debugPass](flecs::entity e, Renderer &renderer) {
                if (!renderer.frameContext.frameActive) return;
                VOXEL_ZONE_N("DebugDrawRenderer-Render");
                e.world().each<Camera3d>([&](flecs::entity cam_entity, Camera3d &camera) {
                    debugPass->render(renderer.frameContext.commandList, camera, *renderer.backend);
                });
            });
}

void DebugDrawRenderer::render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) {
    render_lines(cmd, camera, backend);
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
            .setByteSize(1024 * 1024)
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
}

void DebugDrawRenderer::destroy() {
}

void DebugDrawRenderer::render_lines(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) {
    auto &lines = DebugDrawManager::GetLines();
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
    drawArgs.vertexCount = lines.size(); // 2 vertices par ligne
    cmd->draw(drawArgs);

    cmd->clearState();
}
