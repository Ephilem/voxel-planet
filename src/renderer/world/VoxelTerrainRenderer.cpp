//
// Created by raph on 25/11/25.
//

#include "VoxelTerrainRenderer.h"

#include <memory>

#include "RenderingComponents.h"
#include "core/GameState.h"
#include "renderer/Renderer.h"
#include "renderer/render_types.h"

VoxelTerrainRenderer::VoxelTerrainRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem) {
    m_resourceSystem = resourceSystem;
    m_backend = backend;
    init();
}

VoxelTerrainRenderer::~VoxelTerrainRenderer() {
    destroy();
}

void VoxelTerrainRenderer::init() {
    // Load shaders
    auto vertexRes = m_resourceSystem->load<ShaderResource>("simple.vert", ResourceType::SHADER);
    auto pixelRes = m_resourceSystem->load<ShaderResource>("simple.frag", ResourceType::SHADER);

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
}

// --- ECS ---

void VoxelTerrainRenderer::Register(flecs::world &ecs, VulkanBackend *backend) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();
    auto* voxelRenderer = new VoxelTerrainRenderer(backend, gameState->resourceSystem.get());
    renderer->voxelTerrainRenderer.reset(voxelRenderer);

    ecs.component<MeshDirty>();
    ecs.component<MeshUploaded>();

    ecs.system<VoxelChunk, VoxelChunkMesh>("BuildVoxelChunkMeshSystem")
        .kind(flecs::PostUpdate)
        .with<MeshDirty>()
        .each([voxelRenderer](flecs::entity e, VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->buildVoxelChunkMesh(e, chunk, mesh);
        });

    ecs.system<VoxelChunk, VoxelChunkMesh>("UploadVoxelChunkMeshSystem")
        .kind(flecs::PreStore)
        .without<MeshDirty>()
        .without<MeshUploaded>()
        .each([voxelRenderer](flecs::entity e, VoxelChunk& chunk, VoxelChunkMesh& mesh) {
            voxelRenderer->uploadChunkMesh(e, chunk, mesh);
        });

    ecs.system<Renderer>("RenderTerrainSystem")
        .kind(flecs::OnStore)
        .each([voxelRenderer](flecs::entity e, Renderer& renderer) {
            voxelRenderer->renderTerrain(e, renderer);
        });
}
