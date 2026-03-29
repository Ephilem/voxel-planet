#include "SkyRenderer.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "core/GameState.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"

SkyRenderer::SkyRenderer(VulkanBackend* backend, ResourceSystem* resourceSystem) {
    m_backend = backend;
    m_resourceSystem = resourceSystem;
    init();
}

SkyRenderer::~SkyRenderer() {
    destroy();
}

void SkyRenderer::init() {
    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setSamplerOffset(128)
            .setConstantBufferOffset(0)
            .setUnorderedAccessViewOffset(384);

    std::shared_ptr<ShaderResource> vertRes, fragRes;
    try {
        vertRes = m_resourceSystem->load<ShaderResource>("sky.vert", ResourceType::SHADER);
        fragRes = m_resourceSystem->load<ShaderResource>("sky.frag", ResourceType::SHADER);
    } catch (const std::exception& e) {
        LOG_FATAL("SkyRenderer", "Failed to load sky shaders: {}", e.what());
        throw;
    }

    m_vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertRes->get_data(), vertRes->get_data_size());
    m_pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        fragRes->get_data(), fragRes->get_data_size());

    auto uboDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(SkyUBO))
            .setDebugName("SkyUBO")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
            .setKeepInitialState(true)
            .setIsConstantBuffer(true)
            .setIsVolatile(true)
            .setMaxVersions(8);
    m_uboBuffer = m_backend->device->createBuffer(uboDesc);

    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Pixel)
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
            .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);

    auto bindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer));
    m_bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_bindingLayout);

    auto framebufferInfo = nvrhi::FramebufferInfo()
            .addColorFormat(m_backend->get_swapchain_format())
            .setDepthFormat(m_backend->get_depth_format());

    nvrhi::RenderState renderState;
    renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    // Depth write is off: sky doesn't pollute the depth buffer
    renderState.depthStencilState.depthTestEnable = true;
    renderState.depthStencilState.depthWriteEnable = false;
    renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;

    auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
            .setVertexShader(m_vertexShader)
            .setPixelShader(m_pixelShader)
            .setPrimType(nvrhi::PrimitiveType::TriangleList)
            .setRenderState(renderState)
            .addBindingLayout(m_bindingLayout);
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
}

void SkyRenderer::destroy() {
    m_backend->device->waitForIdle();
    m_pipeline = nullptr;
    m_pixelShader = nullptr;
    m_vertexShader = nullptr;
}

void SkyRenderer::render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) {
    m_ubo.inverseView       = glm::inverse(camera.viewMatrix);
    m_ubo.inverseProjection = glm::inverse(camera.projectionMatrix);

    cmd->writeBuffer(m_uboBuffer, &m_ubo, sizeof(SkyUBO));

    auto extent = m_backend->get_swapchain_extent();

    auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
            .setFramebuffer(m_backend->get_current_framebuffer())
            .addBindingSet(m_bindingSet);
    cmd->setGraphicsState(graphicsState);

    cmd->draw(nvrhi::DrawArguments().setVertexCount(3));

    cmd->clearState();
}

void SkyRenderer::Register(flecs::world& ecs) {
    auto* renderer  = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();

    if (!renderer || !gameState) {
        LOG_ERROR("SkyRenderer", "Cannot register: missing Renderer or GameState");
        return;
    }

    auto pass = std::make_unique<SkyRenderer>(
        renderer->backend.get(),
        gameState->resourceSystem.get()
    );
    auto* skyPass = pass.get();
    renderer->renderPasses.push_back(std::move(pass));

    ecs.system<Renderer>("SkyRenderer-RenderSky")
            .kind(flecs::OnStore)
            .each([skyPass](flecs::entity e, Renderer& renderer) {
                if (!renderer.frameContext.frameActive) return;
                e.world().each<Camera3d>([&](flecs::entity, Camera3d& camera) {
                    skyPass->render(renderer.frameContext.commandList, camera, *renderer.backend);
                });
            });
}
