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
#include "renderer/TracyVulkanIntegration.h"

VoxelTerrainRenderer::VoxelTerrainRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem,
                                           VoxelTextureManager *textureManager) {
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
    init_render_pipeline();
    init_hzb();
    init_culling_pipeline();

    m_meshUploader.init(m_backend);
}

void VoxelTerrainRenderer::init_render_pipeline() {
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
            .addBindingLayout(m_frameBindingLayout) // Set 0
            .addBindingLayout(m_bufferBindingLayout) // Set 1
            .addBindingLayout(m_faceBufferBindingLayout) // Set 2 - face buffer
            .addBindingLayout(m_textureManager->get_binding_layout()); // Set 3 - texture array
    m_pipeline = m_backend->device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
}

void VoxelTerrainRenderer::init_culling_pipeline() {
    // culling compute shader
    auto cullBindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setUnorderedAccessViewOffset(1)
        .setSamplerOffset(2)
        .setConstantBufferOffset(0);
    auto computeRes = m_resourceSystem->load<ShaderResource>("TerrainCulling.comp", ResourceType::SHADER);
    m_cullingShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute),
        computeRes->get_data(), computeRes->get_data_size());

    // Set 0 compute: UBO + HZB texture + HZB sampler
    auto computeFrameLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)) // binding 0 : UBO
            .addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(uint32_t))) // chunkCount
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1)) // binding 1 : HZB texture (SRV_offset=0 + 1)
            .addItem(nvrhi::BindingLayoutItem::Sampler(0))     // binding 2 : HZB sampler (Sampler_offset=2 + 0)
            .setBindingOffsets(cullBindingOffsets);
    m_computeFrameBindingLayout = m_backend->device->createBindingLayout(computeFrameLayoutDesc);

    m_computeFrameBindingSet = m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_hzbTexture))
            .addItem(nvrhi::BindingSetItem::Sampler(0, m_hzbSampler)),
        m_computeFrameBindingLayout);


    auto cullBindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(1))
            .setBindingOffsets(cullBindingOffsets);
    m_cullBindingLayout = m_backend->device->createBindingLayout(cullBindingLayoutDesc);

    auto computePipelineDesc = nvrhi::ComputePipelineDesc()
            .setComputeShader(m_cullingShader)
            .addBindingLayout(m_computeFrameBindingLayout) // Set 0
            .addBindingLayout(m_cullBindingLayout); // Set 1
    m_cullPipeline = m_backend->device->createComputePipeline(computePipelineDesc);
}

void VoxelTerrainRenderer::init_hzb() {
    auto extent = m_backend->get_swapchain_extent();

    // HZB mip 0 = half resolution of depth buffer, each subsequent mip is half of the previous
    m_hzbWidth = std::max(1u, extent.width  / 2);
    m_hzbHeight = std::max(1u, extent.height / 2);
    m_hzbMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(m_hzbWidth, m_hzbHeight)))) + 1;

    auto hzbDesc = nvrhi::TextureDesc()
        .setWidth(m_hzbWidth)
        .setHeight(m_hzbHeight)
        .setMipLevels(m_hzbMipCount)
        .setArraySize(1)
        .setFormat(nvrhi::Format::R32_FLOAT)
        .setIsUAV(true)
        .setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        .setIsRenderTarget(false)
        .setDebugName("HZB Texture");
    m_hzbTexture = m_backend->device->createTexture(hzbDesc);

    auto samplerDesc = nvrhi::SamplerDesc()
        .setMinFilter(false)
        .setMagFilter(false)
        .setMipFilter(false)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_hzbSampler = m_backend->device->createSampler(samplerDesc);

    auto hzbBindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setUnorderedAccessViewOffset(1)
        .setSamplerOffset(2)
        .setConstantBufferOffset(0);

    auto hzbGenRes = m_resourceSystem->load<ShaderResource>("HZBGeneration.comp", ResourceType::SHADER);
    m_hzbGenerationShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute),
        hzbGenRes->get_data(), hzbGenRes->get_data_size());

    auto hzbGenLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))           // binding 0 : source
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))           // binding 1 : dest
        .addItem(nvrhi::BindingLayoutItem::Sampler(0))               // binding 2 : sampler
        .addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(glm::uvec2)))
        .setBindingOffsets(hzbBindingOffsets);
    m_hzbGenBindingLayout = m_backend->device->createBindingLayout(hzbGenLayoutDesc);

    m_hzbGenComputePipeline = m_backend->device->createComputePipeline(
        nvrhi::ComputePipelineDesc()
            .setComputeShader(m_hzbGenerationShader)
            .addBindingLayout(m_hzbGenBindingLayout));
}

void VoxelTerrainRenderer::destroy() {
    m_backend->device->waitForIdle();
    m_chunkBuffers.clear();
    m_chunkBufferBindingSets.clear();
    m_meshUploader.destroy();
    m_pipeline = nullptr;
    m_pixelShader = nullptr;
    m_vertexShader = nullptr;
}

VoxelBuffer &VoxelTerrainRenderer::create_buffer() {
    // create initial chunk buffer
    VoxelBuffer &buffer = m_chunkBuffers.emplace_back(m_backend);

    auto initialBufferBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer.get_oub_buffer()));
    m_chunkBufferBindingSets.push_back(
        m_backend->device->createBindingSet(initialBufferBindingSetDesc, m_bufferBindingLayout));

    auto faceBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer.get_faces_buffer()));
    m_chunkFaceBindingSets.push_back(
        m_backend->device->createBindingSet(faceBindingSetDesc, m_faceBufferBindingLayout));

    // culling compute binding set
    auto cullBindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer.get_chunk_cull_data_buffer()))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, buffer.get_culled_indirect_buffer()))
            .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(1, buffer.get_culled_draw_count_buffer()));
    m_cullBindingSets.push_back(
        m_backend->device->createBindingSet(cullBindingSetDesc, m_cullBindingLayout));


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

    auto pass = std::make_unique<VoxelTerrainRenderer>(
        renderer->backend.get(),
        gameState->resourceSystem.get(),
        ecs.get_mut<VoxelTextureManager>()
    );
    auto *voxelRenderer = pass.get();
    renderer->renderPasses.push_back(std::move(pass));

    ecs.component<VoxelChunkMesh>();

    ecs.system<VoxelChunkMesh, const Position>("VoxelTerrainRenderer-UploadVoxelChunkMesh")
            .kind(flecs::PreStore)
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>()
            .each([voxelRenderer](flecs::entity e, VoxelChunkMesh &mesh, const Position &pos) {
                VOXEL_ZONE_N("VoxelTerrainRenderer-UploadChunkMesh");
                const auto *renderer = e.world().get<Renderer>();
                if (!renderer) {
                    LOG_ERROR("VoxelTerrainRenderer", "Can't upload chunk mesh, Renderer not found in ECS");
                    return;
                }
                voxelRenderer->upload_chunk_mesh_system(renderer, mesh, pos);
                e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Clean>();
            });

    ecs.system<Renderer>("VoxelTerrainRenderer-RenderTerrain")
            .kind(flecs::OnStore)
            .each([voxelRenderer](flecs::entity e, Renderer &renderer) {
                if (!renderer.frameContext.frameActive) return;
                VOXEL_ZONE_N("VoxelTerrainRenderer-Render");
                e.world().each<Camera3d>([&](flecs::entity cam_entity, Camera3d &camera) {
                    voxelRenderer->render(renderer.frameContext.commandList, camera, *renderer.backend);
                });
            });

    ecs.observer<VoxelChunkMesh>("VoxelTerrainRenderer-CleanupVoxelChunkMesh")
            .event(flecs::OnRemove)
            .each([voxelRenderer](flecs::entity e, VoxelChunkMesh &mesh) {
                VOXEL_ZONE_N("TerrainRenderer-CleanupChunkMesh")
                if (mesh.is_allocated() && !voxelRenderer->m_chunkBuffers.empty()) {
                    int bufferIndex = mesh.bufferIndex;
                    voxelRenderer->m_meshUploader.enqueue_free(mesh.drawSlotIndex,
                                                               &voxelRenderer->m_chunkBuffers[bufferIndex]);
                    voxelRenderer->m_chunkBuffers[bufferIndex].free(mesh);
                }
            });
}

bool VoxelTerrainRenderer::upload_chunk_mesh_system(const Renderer *renderer, VoxelChunkMesh &mesh,
                                                    const Position &pos) {
    auto &commandList = renderer->frameContext.commandList;
    VOXEL_VK_NVRHI_ZONE(renderer->backend->tracyVkCtx, commandList, "GPU Upload Chunk Meshes");
    // TODO use the buffer with the position
    bool uploaded = false;
    if (m_chunkBuffers.empty()) {
        create_buffer();
    }

    // Mesh already has a draw slot, just reallocate face regions in the same buffer
    if (mesh.is_allocated()) {
        int oldBufferIndex = mesh.bufferIndex;
        if (oldBufferIndex >= 0 && oldBufferIndex < static_cast<int>(m_chunkBuffers.size())) {
            VoxelBuffer &buffer = m_chunkBuffers[oldBufferIndex];
            uint32_t oldRegionStart = mesh.faceRegionStart;
            if (buffer.reallocate(mesh)) {
                // Stage-4 probe: log remesh uploads
                // LOG_DEBUG("VoxelTerrainRenderer", "[UPLOAD remesh] ({:.0f},{:.0f},{:.0f}) faces={} slot={} region:{}->{} ",
                // pos.x, pos.y, pos.z, mesh.faceCount, mesh.drawSlotIndex, oldRegionStart, mesh.faceRegionStart);
                TerrainOUB oub = {
                    .model = {
                        1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        pos.x, pos.y, pos.z, 1.0f
                    }
                };
                m_meshUploader.enqueue(mesh, oub, &buffer);
                return true;
            }
            LOG_WARN("VoxelTerrainRenderer",
                     "[UPLOAD remesh FALLBACK] ({:.0f},{:.0f},{:.0f}) reallocate failed, doing free+alloc",
                     pos.x, pos.y, pos.z);
            // If reallocate failed (fragmentation), fall through to try other buffers
            // But first free the draw slot since we'll allocate fresh
            buffer.free(mesh);
            // TODO check if we need to deallocate in the gpu also... (double draw?)
        }
    }

    for (size_t i = 0; i < m_chunkBuffers.size() && !uploaded; i++) {
        VoxelBuffer &buffer = m_chunkBuffers[i];

        if (!buffer.allocate(mesh)) {
            continue;
        }
        mesh.bufferIndex = i;

        TerrainOUB oub = {
            .model = {
                1.0f, 0.0f, 0.0f, 0.0f, // column 0
                0.0f, 1.0f, 0.0f, 0.0f, // column 1
                0.0f, 0.0f, 1.0f, 0.0f, // column 2
                pos.x, pos.y, pos.z, 1.0f // column 3 (translation)
            }
        };
        m_meshUploader.enqueue(mesh, oub, &buffer);
        uploaded = true;
    }

    if (!uploaded) {
        LOG_WARN("VoxelTerrainRenderer", "Can't upload chunk mesh, creating new buffer");
        create_buffer();
        upload_chunk_mesh_system(renderer, mesh, pos);
    }

    return true;
}

void VoxelTerrainRenderer::render(nvrhi::CommandListHandle commandList, Camera3d &camera, VulkanBackend &backend) {
    if (camera.viewMatrix != m_ubo.view)
        m_ubo.view = camera.viewMatrix;
    if (camera.projectionMatrix != m_ubo.projection)
        m_ubo.projection = camera.projectionMatrix;


    // Before rendering, flush upload batcher to ensure all pending uploads are executed
    auto *vkCmdBuf = static_cast<VkCommandBuffer>(commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
    m_meshUploader.flush(vkCmdBuf); // execute upload commands

    commandList->writeBuffer(
        m_uboBuffer,
        &m_ubo, sizeof(TerrainUBO));

    auto extent = m_backend->get_swapchain_extent();

    // Culling pass
    {
        VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "GPU Cull Terrain");
        int i = 0;
        for (auto &chunkBuffer: m_chunkBuffers) {
            uint32_t chunkCount = chunkBuffer.get_unculled_draw_count();
            if (chunkCount == 0) {
                i++;
                continue;
            }

            // Reset culled draw count to 0 before dispatching the compute shader
            uint32_t zero = 0;
            commandList->writeBuffer(chunkBuffer.get_culled_draw_count_buffer(), &zero, sizeof(uint32_t));

            auto computeState = nvrhi::ComputeState()
                    .setPipeline(m_cullPipeline)
                    .addBindingSet(m_computeFrameBindingSet) // Set 0
                    .addBindingSet(m_cullBindingSets[i]); // Set 1
            commandList->setComputeState(computeState);

            // Push constant : chunkCount
            commandList->setPushConstants(&chunkCount, sizeof(uint32_t));

            uint32_t groups = (chunkCount + 63) / 64;
            commandList->dispatch(groups, 1, 1);
            i++;
        }
    }

    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    };
    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(vkCmdBuf, &dep);

    {
        VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "GPU Draw Terrain Buffer");
        int i = 0;
        for (auto &chunkBuffer: m_chunkBuffers) {
            auto &bufferBindingSet = m_chunkBufferBindingSets[i];

            auto graphicsState = nvrhi::GraphicsState()
                    .setPipeline(m_pipeline)
                    .setViewport(
                        nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(extent.width, extent.height)))
                    .setFramebuffer(m_backend->get_current_framebuffer())
                    .addBindingSet(m_frameBindingSet) // Set 0: Per-frame data (camera)
                    .addBindingSet(bufferBindingSet) // Set 1: Per-buffer data (chunks)
                    .addBindingSet(m_chunkFaceBindingSets[i]) // Set 2: face buffer
                    .addBindingSet(m_textureManager->get_binding_set()) // Set 3: texture array
                    .setIndirectParams(chunkBuffer.get_culled_indirect_buffer());
            commandList->setGraphicsState(graphicsState);

            // uint32_t drawCount = chunkBuffer.get_draw_count();
            // if (drawCount > 0) {
            //     commandList->drawIndirect(0, drawCount);
            // }

            auto *vkIndirectBuf = static_cast<VkBuffer>(
                chunkBuffer.get_culled_indirect_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer));

            auto *vkCountBuf = static_cast<VkBuffer>(
                chunkBuffer.get_culled_draw_count_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer));

            vkCmdDrawIndirectCount(
                vkCmdBuf,
                vkIndirectBuf, 0, // buffer + offset
                vkCountBuf, 0, // count buffer + offset
                chunkBuffer.get_unculled_draw_count(), // max draws
                sizeof(VkDrawIndirectCommand));

            i++;
        }
    }

    {
        VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "Generate HZB");

        commandList->setTextureState(m_backend->depthBuffer,
            nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);

        for (uint32_t mip = 0; mip < m_hzbMipCount; mip++) {
            uint32_t dstW = std::max(1u, m_hzbWidth  >> mip);
            uint32_t dstH = std::max(1u, m_hzbHeight >> mip);

            auto dstSub = nvrhi::TextureSubresourceSet()
                .setBaseMipLevel(mip)
                .setNumMipLevels(1);

            commandList->setTextureState(m_hzbTexture, dstSub,
                nvrhi::ResourceStates::UnorderedAccess);

            nvrhi::BindingSetDesc bsDesc;
            if (mip == 0) {
                // Mip 0: source is the full-res depth buffer
                bsDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_backend->depthBuffer));
            } else {
                // Mip N: source is previous HZB mip
                auto srcSub = nvrhi::TextureSubresourceSet()
                    .setBaseMipLevel(mip - 1)
                    .setNumMipLevels(1);
                commandList->setTextureState(m_hzbTexture, srcSub,
                    nvrhi::ResourceStates::ShaderResource);
                bsDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_hzbTexture,
                    nvrhi::Format::UNKNOWN, srcSub));
            }
            bsDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(0, m_hzbTexture,
                nvrhi::Format::R32_FLOAT, dstSub));
            bsDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_hzbSampler));

            auto bs = m_backend->device->createBindingSet(bsDesc, m_hzbGenBindingLayout);

            glm::uvec2 dstSize(dstW, dstH);
            commandList->setComputeState(
                nvrhi::ComputeState().setPipeline(m_hzbGenComputePipeline).addBindingSet(bs));
            commandList->setPushConstants(&dstSize, sizeof(glm::uvec2));
            commandList->dispatch((dstW + 7) / 8, (dstH + 7) / 8, 1);
        }

        // Restore states for next frame
        commandList->setTextureState(m_hzbTexture,
            nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        commandList->setTextureState(m_backend->depthBuffer,
            nvrhi::AllSubresources, nvrhi::ResourceStates::DepthWrite);
    }

    commandList->clearState();
}

void VoxelTerrainRenderer::on_resize(uint32_t newWidth, uint32_t newHeight) {
    m_hzbWidth = std::max(1u, newWidth  / 2);
    m_hzbHeight = std::max(1u, newHeight / 2);
    m_hzbMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(m_hzbWidth, m_hzbHeight)))) + 1;

    auto hzbDesc = nvrhi::TextureDesc()
        .setWidth(m_hzbWidth)
        .setHeight(m_hzbHeight)
        .setMipLevels(m_hzbMipCount)
        .setArraySize(1)
        .setFormat(nvrhi::Format::R32_FLOAT)
        .setIsUAV(true)
        .setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        .setIsRenderTarget(false)
        .setDebugName("HZB Texture");
    m_hzbTexture = m_backend->device->createTexture(hzbDesc);
}
