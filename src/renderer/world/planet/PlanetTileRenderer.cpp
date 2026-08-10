#include "PlanetTileRenderer.h"

#include <algorithm>

#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_transform.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/Renderer.h"
#include "renderer/TracyVulkanIntegration.h"
#include "renderer/vulkan/VulkanBackend.h"

using namespace vp;

void PlanetTileRenderer::generate_mesh() {
    std::vector<uint16_t> indices;
    indices.reserve((GRID_RES - 1) * (GRID_RES - 1) * 6);

    for (uint32_t y = 0; y < GRID_RES - 1; ++y) {
        for (uint32_t x = 0; x < GRID_RES - 1; ++x) {
            const uint16_t i0 = static_cast<uint16_t>(y * GRID_RES + x);
            const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            const uint16_t i2 = static_cast<uint16_t>(i0 + GRID_RES);
            const uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    m_indexCount = static_cast<uint32_t>(indices.size());

    const auto desc = nvrhi::BufferDesc()
            .setByteSize(indices.size() * sizeof(uint16_t))
            .setIsIndexBuffer(true)
            .setInitialState(nvrhi::ResourceStates::IndexBuffer)
            .setKeepInitialState(true)
            .setDebugName("PlanetTileIndices");
    m_indexBuffer = m_backend->device->createBuffer(desc);

    const auto cmd = m_backend->device->createCommandList();
    cmd->open();
    cmd->writeBuffer(m_indexBuffer, indices.data(), indices.size() * sizeof(uint16_t));
    cmd->close();
    m_backend->device->executeCommandList(cmd);
}

void PlanetTileRenderer::init_gpu() {
    generate_mesh();

    m_instanceScratch.reserve(MAX_INSTANCES);

    const auto instanceDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(GpuPlanetTileDrawInstance) * MAX_INSTANCES)
            .setStructStride(sizeof(GpuPlanetTileDrawInstance))
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("PlanetTileInstances");
    m_instanceBuffer = m_backend->device->createBuffer(instanceDesc);

    const std::shared_ptr<ShaderResource> vertexRes =
            m_resourceSystem->load<ShaderResource>("planet_tile.vert", ResourceType::SHADER);
    const std::shared_ptr<ShaderResource> pixelRes =
            m_resourceSystem->load<ShaderResource>("planet_tile.frag", ResourceType::SHADER);

    const auto vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertexRes->get_data(), vertexRes->get_data_size());
    const auto pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        pixelRes->get_data(), pixelRes->get_data_size());

    const auto bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setSamplerOffset(0)
            .setConstantBufferOffset(0)
            .setUnorderedAccessViewOffset(0);

    const auto layoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::All)
            .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PlanetTilePushConstants)))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
            .addItem(nvrhi::BindingLayoutItem::Sampler(2))
            .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(layoutDesc);

    const auto setDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(PlanetTilePushConstants)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_instanceBuffer))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_atlas->texture()))
            .addItem(nvrhi::BindingSetItem::Sampler(2, m_atlas->sampler()));
    m_bindingSet = m_backend->device->createBindingSet(setDesc, m_bindingLayout);

    const auto rasterState = nvrhi::RasterState()
            .setCullMode(nvrhi::RasterCullMode::Back)
            .setFillMode(nvrhi::RasterFillMode::Solid)
            .setDepthClipEnable(true);

    const auto depthStencilState = nvrhi::DepthStencilState()
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual); // reverse z depth buffer

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
            .addBindingLayout(m_bindingLayout);
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
}

void PlanetTileRenderer::render_planets(nvrhi::CommandListHandle cmd, Camera3d &camera, flecs::world &ecs) {
    m_instanceScratch.clear();
    m_batches.clear();
    m_stats = Stats{};

    ecs.each([&](flecs::entity e, const PlanetTileDrawListComp &drawList,
                 PlanetTileStreamComp &stream,
                 const PlanetComp &planet, const PlanetTerrainParams &terrain,
                 const GlobalTransform &transform) {
        VOXEL_ZONE_N("PlanetTileRenderer::render_planets-Planet");
        if (drawList.drawItems.empty()) return;

        if (!stream.generator) {
            stream.generator = std::make_unique<PlanetTileGenerator>(
                terrain, PLANET_TILE_ATLAS_RESOLUTION, planet.radius);
        }

        stream.generator->begin_frame();

        stream.drainScratch.clear();
        stream.generator->drain(stream.drainScratch, MAX_TILE_UPLOADS_PER_FRAME);
        for (const auto &result: stream.drainScratch) {
            m_atlas->upload(cmd, result.key, result.data);
        }
        m_stats.uploadsThisFrame += static_cast<uint32_t>(stream.drainScratch.size());

        if (m_instanceScratch.size() + drawList.drawItems.size() > MAX_INSTANCES) {
            LOG_WARN("PlanetTileRenderer", "Instance budget reached, dropping planet '{}'", e.name().c_str());
            ++m_stats.droppedPlanets;
            return;
        }
        ++m_stats.planetsDrawn;

        PlanetBatch batch;
        batch.firstInstance = static_cast<uint32_t>(m_instanceScratch.size());
        batch.instanceCount = static_cast<uint32_t>(drawList.drawItems.size());
        batch.radius = planet.radius;
        batch.camPosPlanet = -transform.pos;
        m_batches.push_back(batch);

        for (const PlanetTileDrawItem &item: drawList.drawItems) {
            GpuPlanetTileDrawInstance inst{};
            inst.originSpacePos = item.originSpacePos;
            inst.extent = planet_tile_extent(item.key.level());
            inst.nodeFaceOrigin = planet_tile_face_origin(item.key);
            inst.packed = planet_tile_pack(item.key.face(), item.key.level());
            inst.morph = item.morph;

            PlanetTileAtlasKey slot = INVALID_ATLAS_SLOT;
            const uint32_t fallbackDepth =
                    resolve_atlas_slot(item.key, slot, inst.uvScale, inst.uvOffset);
            inst.atlasSlot = slot;

            if (slot == INVALID_ATLAS_SLOT) {
                ++m_stats.missingSlots;
                stream.generator->request(item.key, item.distance);
            } else if (fallbackDepth > 0) {
                ++m_stats.fallbackSlots;
                m_stats.deepestFallback = std::max(m_stats.deepestFallback, fallbackDepth);
                // The tile is drawn from an ancestor: keep asking until its own slice lands
                stream.generator->request(item.key, item.distance);
            } else {
                ++m_stats.exactSlots;
            }

            m_instanceScratch.push_back(inst);

#ifndef NDEBUG
            // Guards against the key and the position drifting apart, which silently
            // draws a tile with another one's heightmap. Debug only: it is a double
            // precision face projection per tile per frame
            {
                const double ex = 2.0 / double(1u << item.key.level());
                const double u0 = -1.0 + double(item.key.x()) * ex;
                const double v0 = -1.0 + double(item.key.y()) * ex;
                const glm::dvec3 expected =
                    face_uv_to_direction(item.key.face(), u0, v0) * double(planet.radius)
                    - glm::dvec3(-transform.pos);

                const float err = glm::length(glm::vec3(expected) - item.originSpacePos);
                if (err > 1.f) {
                    LOG_ERROR("PlanetTileRenderer",
                              "Key/pos mismatch L{} f{} x{} y{} err={:.1f}m",
                              item.key.level(), int(item.key.face()), item.key.x(), item.key.y(), err);
                }
            }
#endif
        }

        stream.generator->submit_pending();
    });

    m_atlas->finish_uploads(cmd);

    m_lastInstanceCount = static_cast<uint32_t>(m_instanceScratch.size());
    if (m_instanceScratch.empty()) return;

    cmd->writeBuffer(m_instanceBuffer, m_instanceScratch.data(),
                     m_instanceScratch.size() * sizeof(GpuPlanetTileDrawInstance));

    const VkExtent2D extent = m_backend->get_swapchain_extent();

    const auto state = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setFramebuffer(m_backend->get_current_framebuffer())
            .setViewport(nvrhi::ViewportState()
                .addViewportAndScissorRect(nvrhi::Viewport(
                    0.f, static_cast<float>(extent.width), 0.f, static_cast<float>(extent.height), 0.f, 1.f)))
            .addBindingSet(m_bindingSet)
            .setIndexBuffer({m_indexBuffer, nvrhi::Format::R16_UINT, 0});
    cmd->setGraphicsState(state);

    for (const PlanetBatch &batch: m_batches) {
        VOXEL_VK_NVRHI_ZONE(m_backend->tracyVkCtx, cmd, "PlanetTileRenderer-DrawPlanet");
        PlanetTilePushConstants pc;
        pc.viewProj = camera.projectionMatrix * camera.viewMatrix;
        pc.camPosPlanet = batch.camPosPlanet;
        pc.radius = batch.radius;
        cmd->setPushConstants(&pc, sizeof(pc));

        cmd->drawIndexed(nvrhi::DrawArguments()
            .setVertexCount(m_indexCount)
            .setInstanceCount(batch.instanceCount)
            .setStartInstanceLocation(batch.firstInstance));
    }

    cmd->clearState();
}

uint32_t PlanetTileRenderer::resolve_atlas_slot(const PlanetTileKey &key, PlanetTileAtlasKey &outSlot, float &outScale,
                                                glm::vec2 &outOffset) {
    outScale = 1.f;
    outOffset = {0.f, 0.f};

    PlanetTileKey probe = key;
    PlanetTileAtlasKey slot = m_atlas->find(probe);
    uint32_t depth = 0;

    while (slot == INVALID_ATLAS_SLOT && probe.level() > 0) {
        outOffset = glm::vec2(probe.parent_offset_x(), probe.parent_offset_y())
                    + outOffset * 0.5f;
        outScale *= 0.5f;
        probe = probe.parent();
        slot = m_atlas->find(probe);
        ++depth;
    }

    outSlot = slot;
    return depth;
}
