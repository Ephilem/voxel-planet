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
    } catch (const std::exception &e) {
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
            .setIsVolatile(true) // view can change every frame or not
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
    renderState.rasterState.fillMode = nvrhi::RasterFillMode::Fill;
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
    m_chunkBuffers.clear();
    m_pipeline = nullptr;
    m_pixelShader = nullptr;
    m_vertexShader = nullptr;
}

// --- ECS ---

void VoxelTerrainRenderer::Register(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();
    auto *voxelRenderer = new VoxelTerrainRenderer(renderer->backend.get(), gameState->resourceSystem.get());
    renderer->voxelTerrainRenderer.reset(voxelRenderer);

    ecs.system<const VoxelChunk, VoxelChunkMesh>("BuildVoxelChunkMeshSystem")
            .kind(flecs::PostUpdate)
            .with<Dirty>()
            .each([voxelRenderer](flecs::entity e, const VoxelChunk &chunk, VoxelChunkMesh &mesh) {
                voxelRenderer->build_voxel_chunk_mesh_system(chunk, mesh);
                e.remove<Dirty>()
                        .add<DirtyGpu>();
            });

    ecs.system<VoxelChunkMesh>("UploadVoxelChunkMeshSystem")
            .kind(flecs::PreStore)
            .with<DirtyGpu>()
            .each([voxelRenderer](flecs::entity e, VoxelChunkMesh &mesh) {
                const auto *renderer = e.world().get<Renderer>();
                if (!renderer) {
                    LOG_ERROR("VoxelTerrainRenderer", "Can't upload chunk mesh, Renderer not found in ECS");
                    return;
                }
                auto &commandList = renderer->frameContext.commandList;
                voxelRenderer->upload_chunk_mesh_system(commandList, mesh);
                e.remove<DirtyGpu>();
            });

    ecs.system<Renderer>("RenderTerrainSystem")
            .kind(flecs::OnStore)
            .each([voxelRenderer](flecs::entity e, Renderer &renderer) {
                if (!renderer.frameContext.frameActive) return;
                e.world().each<Camera3d>([&](flecs::entity cam_entity, Camera3d &camera) {
                    voxelRenderer->render_terrain_system(renderer, camera);
                });
            });
}

bool VoxelTerrainRenderer::build_voxel_chunk_mesh_system(const VoxelChunk &chunk, VoxelChunkMesh &mesh) {
    // for each voxel
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 0; y < CHUNK_SIZE; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                uint8_t voxel = chunk.data[x][y][z];
                if (voxel == 0) continue;

                for (int face = 0; face < 6; face++) {
                    int nx = x + ((face == 0) ? -1 : (face == 1) ? 1 : 0);
                    int ny = y + ((face == 2) ? -1 : (face == 3) ? 1 : 0);
                    int nz = z + ((face == 4) ? -1 : (face == 5) ? 1 : 0);
                    bool isVisible = (nx < 0 || nx >= CHUNK_SIZE ||
                                      ny < 0 || ny >= CHUNK_SIZE ||
                                      nz < 0 || nz >= CHUNK_SIZE ||
                                      chunk.data[nx][ny][nz] == 0);
                    if (!isVisible) continue;

                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(z);

                    float verts[6][4][3] = {
                        // Face 0: -X (left)
                        {{fx, fy, fz}, {fx, fy+1.0f, fz}, {fx, fy+1.0f, fz+1.0f}, {fx, fy, fz+1.0f}},
                        // Face 1: +X (right)
                        {{fx+1.0f, fy, fz}, {fx+1.0f, fy, fz+1.0f}, {fx+1.0f, fy+1.0f, fz+1.0f}, {fx+1.0f, fy+1.0f, fz}},
                        // Face 2: -Y (bottom)
                        {{fx, fy, fz}, {fx, fy, fz+1.0f}, {fx+1.0f, fy, fz+1.0f}, {fx+1.0f, fy, fz}},
                        // Face 3: +Y (top)
                        {{fx, fy+1.0f, fz}, {fx+1.0f, fy+1.0f, fz}, {fx+1.0f, fy+1.0f, fz+1.0f}, {fx, fy+1.0f, fz+1.0f}},
                        // Face 4: -Z (back)
                        {{fx, fy, fz}, {fx+1.0f, fy, fz}, {fx+1.0f, fy+1.0f, fz}, {fx, fy+1.0f, fz}},
                        // Face 5: +Z (front)
                        {{fx, fy, fz+1.0f}, {fx, fy+1.0f, fz+1.0f}, {fx+1.0f, fy+1.0f, fz+1.0f}, {fx+1.0f, fy, fz+1.0f}}
                    };

                    uint32_t baseIdx = mesh.vertices.size();
                    for (int i = 0; i < 4; i++) {
                        mesh.vertices.emplace_back(Vertex3d{
                            .position = {verts[face][i][0], verts[face][i][1], verts[face][i][2]}
                        });
                    }

                    // Add two triangles (6 indices) for this quad face
                    mesh.indices.push_back(baseIdx);
                    mesh.indices.push_back(baseIdx + 1);
                    mesh.indices.push_back(baseIdx + 2);
                    mesh.indices.push_back(baseIdx);
                    mesh.indices.push_back(baseIdx + 2);
                    mesh.indices.push_back(baseIdx + 3);
                }
            }
        }
    }

    mesh.vertexCount = mesh.vertices.size();
    mesh.indexCount = mesh.indices.size();
    return true;
}

bool VoxelTerrainRenderer::upload_chunk_mesh_system(nvrhi::CommandListHandle cmd, VoxelChunkMesh &mesh) {
    // TODO use the buffer with the position
    auto& chunkBuffer = m_chunkBuffers.back();

    if (!chunkBuffer.can_allocate(mesh.vertexCount, mesh.indexCount)) {
        LOG_ERROR("VoxelTerrainRenderer", "Cannot allocate space for a chunk (vertices = {} | indices = {})",
                  mesh.vertexCount, mesh.indexCount);
        return false;
    }

    if (!chunkBuffer.allocate(mesh)) {
        LOG_ERROR("VoxelTerrainRenderer", "Failed to allocate the chunk (vertices = {} | indices = {})",
                  mesh.vertexCount, mesh.indexCount);
        return false;
    }

    chunkBuffer.write(cmd, mesh);

    return true;
}

void VoxelTerrainRenderer::render_terrain_system(Renderer &renderer, Camera3d &camera) {
    auto &commandList = renderer.frameContext.commandList;

    if (camera.viewMatrix != m_ubo.view)
        m_ubo.view = camera.viewMatrix;
    if (camera.projectionMatrix != m_ubo.projection)
        m_ubo.projection = camera.projectionMatrix;

    commandList->writeBuffer(
        m_uboBuffer,
        &m_ubo, sizeof(TerrainUBO));

    auto extent = m_backend->get_swapchain_extent();

    auto& chunkBuffer = m_chunkBuffers.back(); // TODO select proper buffer

    auto vertexBinding = nvrhi::VertexBufferBinding()
            .setSlot(0)
            .setBuffer(chunkBuffer.get_buffer())
            .setOffset(0);
    auto indexBinding = nvrhi::IndexBufferBinding()
            .setOffset(INDEX_SECTION_OFFSET)
            .setBuffer(chunkBuffer.get_buffer())
            .setFormat(nvrhi::Format::R32_UINT);

    auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
            .setFramebuffer(m_backend->get_current_framebuffer())
            .addBindingSet(m_uboBindingSet)
            .addVertexBuffer(vertexBinding)
            .setIndirectParams(chunkBuffer.get_buffer())
            .setIndexBuffer(indexBinding);
    commandList->setGraphicsState(graphicsState);

    uint32_t drawCount = chunkBuffer.get_used_indirect_regions();
    if (drawCount > 0) {
        commandList->drawIndexedIndirect(INDIRECT_SECTION_OFFSET, drawCount);
    }

    commandList->clearState();
}
