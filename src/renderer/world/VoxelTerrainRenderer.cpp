//
// Created by raph on 25/11/25.
//

#include "VoxelTerrainRenderer.h"

#include <memory>
#include <vulkan/vulkan.h>

#include "../rendering_components.h"
#include "core/GameState.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"
#include "renderer/render_types.h"

VoxelTerrainRenderer::VoxelTerrainRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem) {
    m_resourceSystem = resourceSystem;
    m_backend = backend;
    init();
}

VoxelTerrainRenderer::~VoxelTerrainRenderer() {
    LOG_DEBUG("VoxelTerrainRenderer", "Destroying VoxelTerrainRenderer");
    destroy();
}

void VoxelTerrainRenderer::init() {
    // Load shaders
    std::shared_ptr<ShaderResource> vertexRes, pixelRes;
    try {
        vertexRes = m_resourceSystem->load<ShaderResource>("simple.vert", ResourceType::SHADER);
        pixelRes = m_resourceSystem->load<ShaderResource>("simple.frag", ResourceType::SHADER);
    } catch (const std::exception& e) {
        LOG_FATAL("VoxelTerrainRenderer", "Error when reading voxel terrain shaders: {}", e.what());
        throw e;
    }

    m_vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertexRes->getData(), vertexRes->get_data_size());
    m_pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        pixelRes->getData(), pixelRes->get_data_size());

    //// Initialize UBO
    // Buffer
    auto uboBufferDesc = nvrhi::BufferDesc()
        .setByteSize(sizeof(TerrainUBO))
        .setDebugName("TerrainUBO")
        .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
        .setKeepInitialState(true)
        .setIsConstantBuffer(true)
        .setIsVolatile(true)  // view can change every frame or not
        .setMaxVersions(8);
    m_uboBuffer = m_backend->device->createBuffer(uboBufferDesc);

    // Binding
    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setSamplerOffset(128)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(384);

    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
        .setBindingOffsets(bindingOffsets);
    m_uboBindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);
    auto bindingSetDesc = nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer));
    m_uboBindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_uboBindingLayout);
    //
    ////

    // Create Vertex Attribute Input
    nvrhi::VertexAttributeDesc vertexAttrs[] = {
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(Vertex3d, position))
            .setElementStride(sizeof(Vertex3d)),
    };

    nvrhi::InputLayoutHandle inputLayout = m_backend->device->createInputLayout(
        vertexAttrs, std::size(vertexAttrs), m_vertexShader);

    nvrhi::Format swapchainNvrhiFormat = m_backend->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM
        ? nvrhi::Format::BGRA8_UNORM
        : nvrhi::Format::RGBA8_UNORM;

    // Create Pipeline
    auto framebufferInfo = nvrhi::FramebufferInfo()
        .addColorFormat(swapchainNvrhiFormat)
        .setDepthFormat(nvrhi::Format::D24S8);

    // Configure render state
    nvrhi::RenderState renderState;
    renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;
    renderState.depthStencilState.depthTestEnable = true;
    renderState.depthStencilState.depthWriteEnable = true;
    renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    renderState.depthStencilState.stencilEnable = false;

    auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
        .setInputLayout(inputLayout)
        .setVertexShader(m_vertexShader)
        .setPixelShader(m_pixelShader)
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setRenderState(renderState)
        .addBindingLayout(m_uboBindingLayout);
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);


    // create initial chunk buffer
    m_chunkBuffers.emplace_back(m_backend);
}

void VoxelTerrainRenderer::destroy() {
    m_backend->device->waitForIdle();
    m_pipeline = nullptr;
    m_pixelShader = nullptr;
    m_vertexShader = nullptr;
}

// --- ECS ---

void VoxelTerrainRenderer::Register(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();
    auto* voxelRenderer = new VoxelTerrainRenderer(renderer->backend.get(), gameState->resourceSystem.get());
    renderer->voxelTerrainRenderer.reset(voxelRenderer);

    ecs.system<VoxelChunk, VoxelChunkMesh>("BuildVoxelChunkMeshSystem")
        .kind(flecs::PostUpdate)
        .with<Dirty>()
        .each([voxelRenderer](VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->build_voxel_chunk_mesh_system(chunk, mesh);
        });

    ecs.system<VoxelChunk, VoxelChunkMesh>("UploadVoxelChunkMeshSystem")
        .kind(flecs::PreStore)
        .without<Dirty>()
        .without<MeshUploaded>()
        .each([voxelRenderer](VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->upload_chunk_mesh_system(chunk, mesh);
        });

    ecs.system<Renderer>("RenderTerrainSystem")
      .kind(flecs::OnStore)
      .each([voxelRenderer](flecs::entity e, Renderer& renderer) {
          if (!renderer.frameContext.frameActive) return;
          e.world().each<Camera3d>([&](flecs::entity cam_entity, Camera3d& camera) {
              voxelRenderer->render_terrain_system(renderer, camera);
          });
      });



}

void VoxelTerrainRenderer::build_voxel_chunk_mesh_system(VoxelChunk &chunk,
                                                         VoxelChunkMesh &mesh) {
}

void VoxelTerrainRenderer::upload_chunk_mesh_system(const VoxelChunk &chunk,
                                                    VoxelChunkMesh &mesh) {
}

void VoxelTerrainRenderer::render_terrain_system(Renderer &renderer, Camera3d& camera) {
    auto& commandList = renderer.frameContext.commandList;

    if (camera.viewMatrix != m_ubo.view)
        m_ubo.view = camera.viewMatrix;
    if (camera.projectionMatrix != m_ubo.projection)
        m_ubo.projection = camera.projectionMatrix;

    commandList->writeBuffer(
        m_uboBuffer,
        &m_ubo, sizeof(TerrainUBO));

    auto extent = m_backend->get_swapchain_extent();
    auto graphicsState = nvrhi::GraphicsState()
        .setPipeline(m_pipeline)
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
        .setFramebuffer(m_backend->get_current_framebuffer())
        .addBindingSet(m_uboBindingSet);
    commandList->setGraphicsState(graphicsState);

    auto drawArguments = nvrhi::DrawArguments().setVertexCount(3);
    commandList->draw(drawArguments);

    commandList->clearState();
}
