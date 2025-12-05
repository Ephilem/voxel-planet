//
// Created by raph on 25/11/25.
//

#include "VoxelTerrainRenderer.h"

#include <memory>

#include "RenderingComponents.h"
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

    // Create Pipeline
    auto framebufferInfo = nvrhi::FramebufferInfo()
    .addColorFormat(nvrhi::Format::RGBA8_UNORM);

    auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
        .setInputLayout(inputLayout)
        .setVertexShader(m_vertexShader)
        .setPixelShader(m_pixelShader)
        .setPrimType(nvrhi::PrimitiveType::TriangleList);
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
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

    // ecs.component<MeshDirty>();
    // ecs.component<MeshUploaded>();

    ecs.system<VoxelChunk, VoxelChunkMesh>("BuildVoxelChunkMeshSystem")
        .kind(flecs::PostUpdate)
        .with<MeshDirty>()
        .each([voxelRenderer](VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->build_voxel_chunk_mesh_system(chunk, mesh);
        });

    ecs.system<VoxelChunk, VoxelChunkMesh>("UploadVoxelChunkMeshSystem")
        .kind(flecs::PreStore)
        .without<MeshDirty>()
        .without<MeshUploaded>()
        .each([voxelRenderer](VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->upload_chunk_mesh_system(chunk, mesh);
        });

    ecs.system<Renderer>("RenderTerrainSystem")
        .kind(flecs::OnStore)
        .each([voxelRenderer](Renderer& renderer) {
            voxelRenderer->render_terrain_system(renderer);
        });
}

void VoxelTerrainRenderer::build_voxel_chunk_mesh_system(VoxelChunk &chunk,
                                                         VoxelChunkMesh &mesh) {
}

void VoxelTerrainRenderer::upload_chunk_mesh_system(const VoxelChunk &chunk,
                                                    VoxelChunkMesh &mesh) {
}

void VoxelTerrainRenderer::render_terrain_system(Renderer &renderer) {
}
