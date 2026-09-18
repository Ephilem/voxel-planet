#include "PlanetSurfaceChunkRenderer.h"

namespace vp {

void PlanetSurfaceChunkRenderer::init_gpu() {
    m_chunkBuffer = std::make_unique<PlanetSurfaceChunkBuffer>(m_backend);

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
                                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1)) // vertices
                                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2)) // chunk instances
                                .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(layoutDesc);

    const auto setDesc = nvrhi::BindingSetDesc()
                             .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(PushConstants)))
                             .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_chunkBuffer->vertices_buffer()))
                             .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_chunkBuffer->instance_buffer()));
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

void PlanetSurfaceChunkRenderer::render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) {}

} // namespace vp
