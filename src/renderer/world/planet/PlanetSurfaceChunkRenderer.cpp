#include "PlanetSurfaceChunkRenderer.h"
#include "core/world/planet/planet_transform.h"

namespace vp {

void PlanetSurfaceChunkRenderer::init_gpu() {
    // Buffers
    m_chunkBuffer = std::make_unique<PlanetSurfaceChunkBuffer>(m_backend);

    const auto anchorBufferDesc = nvrhi::BufferDesc()
                                      .setByteSize(sizeof(PlanetSurfaceChunkAnchor))
                                      .setIsConstantBuffer(true)
                                      .setIsVolatile(true)
                                      .setMaxVersions(16)
                                      .setDebugName("PlanetSurfaceChunkRenderer::anchor");
    m_anchorBuffer = m_backend->device->createBuffer(anchorBufferDesc);

    const std::shared_ptr<ShaderResource> vertexRes =
        m_resourceSystem->load<ShaderResource>("planet_surface_chunk.vert", ResourceType::SHADER);
    const std::shared_ptr<ShaderResource> pixelRes =
        m_resourceSystem->load<ShaderResource>("planet_surface_chunk.frag", ResourceType::SHADER);

    const auto vertexShader =
        m_backend->device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
                                        vertexRes->get_data(), vertexRes->get_data_size());
    const auto pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel), pixelRes->get_data(), pixelRes->get_data_size());

    const auto bindingOffsets = nvrhi::VulkanBindingOffsets()
                                    .setShaderResourceOffset(0)
                                    .setSamplerOffset(0)
                                    .setConstantBufferOffset(0)
                                    .setUnorderedAccessViewOffset(0);

    // Set 0: geometry pulled by the vertex shader
    const auto layoutDesc = nvrhi::BindingLayoutDesc()
                                .setVisibility(nvrhi::ShaderType::All)
                                .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PushConstants)))
                                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))   // vertices
                                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2))   // chunk instances
                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)) // anchor
                                .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(layoutDesc);

    const auto setDesc = nvrhi::BindingSetDesc()
                             .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(PushConstants)))
                             .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_chunkBuffer->vertices_buffer()))
                             .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_chunkBuffer->instance_buffer()))
                             .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_anchorBuffer));
    m_bindingSet = m_backend->device->createBindingSet(setDesc, m_bindingLayout);

    const auto rasterState = nvrhi::RasterState()
                                 .setCullMode(nvrhi::RasterCullMode::Back)
                                 .setFrontCounterClockwise(false)
                                 .setFillMode(nvrhi::RasterFillMode::Solid)
                                 .setDepthClipEnable(true);

    const auto depthStencilState =
        nvrhi::DepthStencilState().setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(
            nvrhi::ComparisonFunc::GreaterOrEqual); // reverse z

    const auto framebufferInfo = nvrhi::FramebufferInfo()
                                     .addColorFormat(m_backend->get_swapchain_format())
                                     .setDepthFormat(m_backend->get_depth_format());

    const auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
                                  .setVertexShader(vertexShader)
                                  .setPixelShader(pixelShader)
                                  .setPrimType(nvrhi::PrimitiveType::TriangleList)
                                  .setRenderState({
                                      .depthStencilState = depthStencilState,
                                      .rasterState = rasterState,
                                  })
                                  .addBindingLayout(m_bindingLayout)
                                  .addBindingLayout(m_textures->get_binding_layout());
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
}

PlanetSurfaceChunkAnchor PlanetSurfaceChunkRenderer::compute_anchor(const PlanetSurfaceChunkKey& anchorKey,
                                                                    double radius, const glm::dvec3& cameraRelPlanet) {
    const double N = planet_voxels_per_face_side(radius, 0);
    const double s = 2.0 / N; // face coordinate step per voxel
    const double i0 = double(anchorKey.x) * CHUNK_SIZE;
    const double j0 = double(anchorKey.y) * CHUNK_SIZE;
    const double Ra = radius + (double(anchorKey.alt) * CHUNK_SIZE * PLANET_VOXEL_SIZE_LOD0);

    const auto D = [&](double di, double dj) {
        return face_uv_to_direction(anchorKey.face, ((i0 + di) * s) - 1.0, ((j0 + dj) * s) - 1.0);
    };

    constexpr double q = 16.0; // finite difference step, in voxels
    const glm::dvec3 up = D(0, 0);
    const glm::dvec3 du = (D(q, 0) - D(-q, 0)) / (2.0 * q);
    const glm::dvec3 dv = (D(0, q) - D(0, -q)) / (2.0 * q);
    const glm::dvec3 duu = (D(q, 0) - 2.0 * up + D(-q, 0)) / (q * q);
    const glm::dvec3 dvv = (D(0, q) - 2.0 * up + D(0, -q)) / (q * q);
    const glm::dvec3 duv = (D(q, q) - D(q, -q) - D(-q, q) + D(-q, -q)) / (4.0 * q * q);

    PlanetSurfaceChunkAnchor g{};
    g.chunk = {anchorKey.x, anchorKey.y, anchorKey.alt, int(anchorKey.face)};
    g.tangentU = glm::vec4(glm::vec3(Ra * du), 0.f);
    g.tangentV = glm::vec4(glm::vec3(Ra * dv), 0.f);
    g.up = glm::vec4(glm::vec3(up), float(1.0 / Ra));
    g.quu = glm::vec4(glm::vec3(0.5 * Ra * duu), 0.f);
    g.quv = glm::vec4(glm::vec3(Ra * duv), 0.f);
    g.qvv = glm::vec4(glm::vec3(0.5 * Ra * dvv), 0.f);
    g.camRelAnchor = glm::vec4(glm::vec3(cameraRelPlanet - up * Ra), 0.f);
    return g;
}

void PlanetSurfaceChunkRenderer::render(nvrhi::CommandListHandle cmd, Camera3d& camera,
                                        const GlobalTransform& planetCamTransform, const Planet& playerPlanet) {
    const VkExtent2D extent = m_backend->get_swapchain_extent();

    const double radius = playerPlanet.radius;
    const glm::dvec3 camPlanet = -planetCamTransform.pos;
    const PlanetVoxelCoord camVoxel = planet_pos_to_voxel(camPlanet, radius, 0);
    const PlanetSurfaceChunkKey anchorKey(camVoxel.face, 0, glm::ivec3(camVoxel.voxel));
    if (!anchorKey.valid()) {
        return;
    }

    const auto anchor = compute_anchor(anchorKey, radius, camPlanet);

    // upload anchor
    cmd->writeBuffer(m_anchorBuffer, &anchor, sizeof(PlanetSurfaceChunkAnchor));

    if (m_chunkBuffer->draw_count() <= 0) {
        return;
    }

    const auto graphicState =
        nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setFramebuffer(m_backend->get_current_framebuffer())
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(
                0.f, static_cast<float>(extent.width), 0.f, static_cast<float>(extent.height), 0.f, 1.f)))
            .addBindingSet(m_bindingSet)
            .addBindingSet(m_textures->get_binding_set())
            .setIndirectParams(m_chunkBuffer->draw_buffer());
    cmd->setGraphicsState(graphicState);

    PushConstants pc = {};
    pc.viewProj = camera.projectionMatrix * camera.viewMatrix;
    cmd->setPushConstants(&pc, sizeof(PushConstants));

    cmd->drawIndirect(0, m_chunkBuffer->draw_count());
    cmd->clearState();
}

void PlanetSurfaceChunkRenderer::unload_chunks(std::span<const PlanetSurfaceChunkKey> chunks) {
    for (const auto key : chunks) {
        m_chunkBuffer->release(key);
    }
}

void PlanetSurfaceChunkRenderer::load_chunks(std::span<const PlanetSurfaceChunkMeshUpload> meshes) {
    for (const auto meshUpload : meshes) {
        m_chunkBuffer->allocate(meshUpload.mesh, meshUpload.key);
    }
}

void PlanetSurfaceChunkRenderer::upload_chunk_to_gpu(nvrhi::ICommandList* cmd) {
    m_chunkBuffer->upload_pending_chunks(cmd);
}

} // namespace vp
