//
// Created by raph on 25/11/25.
//

#include "VoxelTerrainRenderer.h"

#include <memory>

#include "../rendering_components.h"
#include "core/GameState.h"
#include "core/main_components.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"
#include <glm/glm.hpp>

#include "VoxelChunkMesher.h"
#include "VoxelTextureManager.h"

VoxelTerrainRenderer::VoxelTerrainRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem, VoxelTextureManager* textureManager) {
    m_textureManager = textureManager;
    m_resourceSystem = resourceSystem;
    m_backend = backend;
    init();
}

VoxelTerrainRenderer::~VoxelTerrainRenderer() {
    LOG_DEBUG("VoxelTerrainRenderer", "Destroying VoxelTerrainRenderer");
    destroy();
}

void VoxelTerrainRenderer::init() {
    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setSamplerOffset(128)
            .setConstantBufferOffset(0)
            .setUnorderedAccessViewOffset(384);

    // Load shaders
    std::shared_ptr<ShaderResource> vertexRes, pixelRes;
    try {
        vertexRes = m_resourceSystem->load<ShaderResource>("simple.vert", ResourceType::SHADER);
        pixelRes = m_resourceSystem->load<ShaderResource>("simple.frag", ResourceType::SHADER);
    } catch (const std::exception &e) {
        LOG_FATAL("VoxelTerrainRenderer", "Error when reading voxel terrain shaders: {}", e.what());
        throw e;
    }

    m_vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertexRes->get_data(), vertexRes->get_data_size());
    m_pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        pixelRes->get_data(), pixelRes->get_data_size());

    auto uboBufferDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(TerrainUBO))
            .setDebugName("TerrainUBO")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
            .setKeepInitialState(true)
            .setIsConstantBuffer(true)
            .setIsVolatile(true)
            .setMaxVersions(8);
    m_uboBuffer = m_backend->device->createBuffer(uboBufferDesc);

    // Set 0: Per-frame bindings (camera/view data)
    auto frameBindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)) // UBO for view/projection matrices
            .setBindingOffsets(bindingOffsets);
    m_frameBindingLayout = m_backend->device->createBindingLayout(frameBindingLayoutDesc);

    auto frameBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer));
    m_frameBindingSet = m_backend->device->createBindingSet(frameBindingSetDesc, m_frameBindingLayout);

    // Set 1: Per-buffer bindings (chunk data)
    auto bufferBindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)) // OUB buffer for per-chunk data
            .setBindingOffsets(bindingOffsets);
    m_bufferBindingLayout = m_backend->device->createBindingLayout(bufferBindingLayoutDesc);

    // Set 2: Face buffer
    auto faceBindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
            .setBindingOffsets(bindingOffsets);
    m_faceBufferBindingLayout = m_backend->device->createBindingLayout(faceBindingLayoutDesc);

    nvrhi::Format swapchainNvrhiFormat = m_backend->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM
                                             ? nvrhi::Format::BGRA8_UNORM
                                             : nvrhi::Format::RGBA8_UNORM;

    // Create Pipeline
    auto framebufferInfo = nvrhi::FramebufferInfo()
            .addColorFormat(swapchainNvrhiFormat)
            .setDepthFormat(nvrhi::Format::D24S8);

    // Configure render state
    nvrhi::RenderState renderState;
    renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
    renderState.rasterState.fillMode = nvrhi::RasterFillMode::Fill;
    renderState.depthStencilState.depthTestEnable = true;
    renderState.depthStencilState.depthWriteEnable = true;
    renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    renderState.depthStencilState.stencilEnable = false;

    auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
            .setVertexShader(m_vertexShader)
            .setPixelShader(m_pixelShader)
            .setPrimType(nvrhi::PrimitiveType::TriangleList)
            .setRenderState(renderState)
            .addBindingLayout(m_frameBindingLayout)  // Set 0
            .addBindingLayout(m_bufferBindingLayout) // Set 1
            .addBindingLayout(m_faceBufferBindingLayout) // Set 2 - face buffer
            .addBindingLayout(m_textureManager->get_binding_layout()); // Set 3 - texture array
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
}

void VoxelTerrainRenderer::destroy() {
    m_backend->device->waitForIdle();
    m_chunkBuffers.clear();
    m_chunkBufferBindingSets.clear();
    m_pipeline = nullptr;
    m_pixelShader = nullptr;
    m_vertexShader = nullptr;
}

VoxelBuffer& VoxelTerrainRenderer::create_buffer() {
    // create initial chunk buffer
    VoxelBuffer& buffer = m_chunkBuffers.emplace_back(m_backend);

    auto initialBufferBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer.get_oub_buffer()));
    m_chunkBufferBindingSets.push_back(
        m_backend->device->createBindingSet(initialBufferBindingSetDesc, m_bufferBindingLayout));

    auto faceBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer.get_faces_buffer()));
    m_chunkFaceBindingSets.push_back(
        m_backend->device->createBindingSet(faceBindingSetDesc, m_faceBufferBindingLayout));


    // return index of the created buffer
    return buffer;
}

// --- ECS ---

void VoxelTerrainRenderer::Register(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();

    if (!renderer || !gameState) {
        LOG_ERROR("VoxelTerrainRenderer", "Cannot register: missing Renderer or GameState");
        return;
    }

    VoxelChunkMesher::Register(ecs);
    VoxelTextureManager::Register(ecs);

    renderer->voxelTerrainRenderer = std::make_unique<VoxelTerrainRenderer>(
        renderer->backend.get(),
        gameState->resourceSystem.get(),
        ecs.get_mut<VoxelTextureManager>()
    );
    auto* voxelRenderer = renderer->voxelTerrainRenderer.get();

    ecs.component<VoxelChunkMesh>();

    ecs.system<VoxelChunkMesh, const Position>("VoxelTerrainRenderer-UploadVoxelChunkMesh")
            .kind(flecs::PreStore)
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>()
            .each([voxelRenderer](flecs::entity e, VoxelChunkMesh &mesh, const Position& pos) {
                const auto *renderer = e.world().get<Renderer>();
                if (!renderer) {
                    LOG_ERROR("VoxelTerrainRenderer", "Can't upload chunk mesh, Renderer not found in ECS");
                    return;
                }
                auto &commandList = renderer->frameContext.commandList;
                voxelRenderer->upload_chunk_mesh_system(commandList, mesh, pos);
                e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Clean>();
            });

    ecs.system<Renderer>("VoxelTerrainRenderer-RenderTerrain")
            .kind(flecs::OnStore)
            .each([voxelRenderer](flecs::entity e, Renderer &renderer) {
                if (!renderer.frameContext.frameActive) return;
                e.world().each<Camera3d>([&](flecs::entity cam_entity, Camera3d &camera) {
                    voxelRenderer->render_terrain_system(renderer, camera);
                });
            });


    ecs.observer<VoxelChunkMesh>("VoxelTerrainRenderer-CleanupVoxelChunkMesh")
            .event(flecs::OnRemove)
            .each([voxelRenderer](flecs::entity e, VoxelChunkMesh &mesh) {
                if (mesh.is_allocated() && !voxelRenderer->m_chunkBuffers.empty()) {
                    int bufferIndex = mesh.bufferIndex;
                    voxelRenderer->m_chunkBuffers[bufferIndex].free(mesh);
                }
            });

}

bool VoxelTerrainRenderer::upload_chunk_mesh_system(nvrhi::CommandListHandle cmd, VoxelChunkMesh &mesh, const Position &pos) {
    // TODO use the buffer with the position
    bool uploaded = false;
    if (m_chunkBuffers.empty()) {
        create_buffer();
    }

    for (size_t i = 0; i < m_chunkBuffers.size() && !uploaded; i++) {
        VoxelBuffer& buffer = m_chunkBuffers[i];

        if (!buffer.allocate(mesh)) {
            continue;
        }
        mesh.bufferIndex = i;

        TerrainOUB oub = {
            .model = {
                1.0f, 0.0f, 0.0f, 0.0f,  // column 0
                0.0f, 1.0f, 0.0f, 0.0f,  // column 1
                0.0f, 0.0f, 1.0f, 0.0f,  // column 2
                pos.x, pos.y, pos.z, 1.0f  // column 3 (translation)
            }
        };
        buffer.write(cmd, mesh, oub);
        uploaded = true;
    }

    if (!uploaded) {
        LOG_WARN("VoxelTerrainRenderer", "Can't upload chunk mesh, creating new buffer");
        create_buffer();
        upload_chunk_mesh_system(cmd, mesh, pos);
    }

    return true;
}

void VoxelTerrainRenderer::render_terrain_system(Renderer &renderer, Camera3d &camera) {
    auto &commandList = renderer.frameContext.commandList;


    if (camera.viewMatrix != m_ubo.view)
        m_ubo.view = camera.viewMatrix;
    if (camera.projectionMatrix != m_ubo.projection)
        m_ubo.projection = camera.projectionMatrix;


    // Before rendering, clean up freed draw slots
    for (auto& chunkBuffer : m_chunkBuffers) {
        chunkBuffer.cleanup_freed_draw_slots(commandList);
    }

    commandList->writeBuffer(
        m_uboBuffer,
        &m_ubo, sizeof(TerrainUBO));

    auto extent = m_backend->get_swapchain_extent();

    int i = 0;
    for (auto& chunkBuffer : m_chunkBuffers) {
        auto& bufferBindingSet = m_chunkBufferBindingSets[i];

        auto graphicsState = nvrhi::GraphicsState()
                .setPipeline(m_pipeline)
                .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
                .setFramebuffer(m_backend->get_current_framebuffer())
                .addBindingSet(m_frameBindingSet)    // Set 0: Per-frame data (camera)
                .addBindingSet(bufferBindingSet)     // Set 1: Per-buffer data (chunks)
                .addBindingSet(m_chunkFaceBindingSets[i]) // Set 2: face buffer
                .addBindingSet(m_textureManager->get_binding_set()) // Set 3: texture array
                .setIndirectParams(chunkBuffer.get_indirect_buffer());
        commandList->setGraphicsState(graphicsState);

        uint32_t drawCount = chunkBuffer.get_draw_count();
        if (drawCount > 0) {
            commandList->drawIndirect(0, drawCount);
        }
        i++;
    }

    commandList->clearState();
}
