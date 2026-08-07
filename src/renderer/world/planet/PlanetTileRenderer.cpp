#include "PlanetTileRenderer.h"

#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
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
            const uint16_t i0 = uint16_t(y * GRID_RES + x);
            const uint16_t i1 = uint16_t(i0 + 1);
            const uint16_t i2 = uint16_t(i0 + GRID_RES);
            const uint16_t i3 = uint16_t(i2 + 1);

            indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    m_indexCount = uint32_t(indices.size());

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
            .setByteSize(sizeof(PlanetTileDrawInstance) * MAX_INSTANCES)
            .setStructStride(sizeof(PlanetTileDrawInstance))
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

    const auto layoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::All)
            .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PlanetTilePushConstants)))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0));
    m_bindingLayout = m_backend->device->createBindingLayout(layoutDesc);

    const auto setDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(PlanetTilePushConstants)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_instanceBuffer));
    m_bindingSet = m_backend->device->createBindingSet(setDesc, m_bindingLayout);

    const auto rasterState = nvrhi::RasterState()
            .setCullMode(nvrhi::RasterCullMode::Back)
            .setFillMode(nvrhi::RasterFillMode::Solid)
            .setDepthClipEnable(true);

    const auto depthStencilState = nvrhi::DepthStencilState()
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);

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

    ecs.each([&](flecs::entity e, const PlanetTileDrawList &drawList,
                 const PlanetComp &planet, const GlobalTransform &transform) {
        if (drawList.drawInstances.empty()) return;

        if (m_instanceScratch.size() + drawList.drawInstances.size() > MAX_INSTANCES) {
            LOG_WARN("PlanetTileRenderer", "Instance budget reached, dropping planet '{}'", e.name().c_str());
            return;
        }

        PlanetBatch batch;
        batch.firstInstance = uint32_t(m_instanceScratch.size());
        batch.instanceCount = uint32_t(drawList.drawInstances.size());
        batch.radius = planet.radius;
        batch.camPosPlanet = -transform.pos;
        m_batches.push_back(batch);

        m_instanceScratch.insert(m_instanceScratch.end(),
                                 drawList.drawInstances.begin(), drawList.drawInstances.end());
    });

    m_lastInstanceCount = uint32_t(m_instanceScratch.size());
    if (m_instanceScratch.empty()) return;

    cmd->writeBuffer(m_instanceBuffer, m_instanceScratch.data(),
                     m_instanceScratch.size() * sizeof(PlanetTileDrawInstance));

    const VkExtent2D extent = m_backend->get_swapchain_extent();

    const auto state = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setFramebuffer(m_backend->get_current_framebuffer())
            .setViewport(nvrhi::ViewportState()
                .addViewportAndScissorRect(nvrhi::Viewport(
                    0.f, float(extent.width), 0.f, float(extent.height), 0.f, 1.f)))
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
