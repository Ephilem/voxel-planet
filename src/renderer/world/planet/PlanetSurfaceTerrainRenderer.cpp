#include "PlanetSurfaceTerrainRenderer.h"

#include <cmath>
#include <unordered_map>

#include <imgui.h>

#include "PlanetChunkMesher.h"
#include "client/player/player_components.h"
#include "client/world/planet/planet_client_components.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "core/debug/DebugDraw.h"
#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"
#include "core/world/spatial/spatial_utils.h"
#include "renderer/Renderer.h"
#include "renderer/TracyVulkanIntegration.h"
#include "renderer/world/VoxelTextureManager.h"

using namespace vp;

namespace {
    /// Workgroup size of planet_lod_emit_draws.comp.
    constexpr uint32_t EMIT_DRAWS_GROUP_SIZE = 64;
}

PlanetSurfaceTerrainRenderer::~PlanetSurfaceTerrainRenderer() {
    if (m_backend)
        destroy();
}

void PlanetSurfaceTerrainRenderer::init(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();

    m_backend = renderer->backend.get();
    m_resourceSystem = gameState->resourceSystem.get();
    m_textureManager = ecs.get_mut<VoxelTextureManager>();
    m_chunkBuffers.reserve(MAX_CHUNK_BUFFERS);

    // The worker pools live as ECS singletons, which is what the ECS is good at here: one
    // instance, found by type, owning threads nobody else should duplicate. The generator has a
    // semaphore and threads inside it, so it has to be constructed in place rather than set().
    m_mesher = ecs.get_mut<PlanetChunkMesher>();
    if (!ecs.has<PlanetChunkGenerator>()) ecs.emplace<PlanetChunkGenerator>();
    m_generator = ecs.get_mut<PlanetChunkGenerator>();

    init_gpu();
    init_lod();

    // The node grid shares the spatial grid's frame, and the view matrix is camera relative so
    // its translation is zero. This system is the only thing that tells the LOD where the
    // player actually stands, which drives both the root disc and the vertex offset.
    ecs.system<const Camera3d>("PlanetSurface-FeedCameraPosition")
            .kind(flecs::PreStore)
            .with<PlayerClient>()
            .each([this](flecs::entity e, const Camera3d &) {
                cameraWorldPos = glm::vec3(get_hp_position(e));
            });

    // The planet entity does not exist yet when the render module is imported, so the config is
    // picked up every frame instead of once at init. Editing it live takes effect too.
    ecs.system<const PlanetGenerationConfig>("PlanetSurface-SyncGenerationConfig")
            .kind(flecs::PreStore)
            .each([this](const PlanetGenerationConfig &config) {
                m_genConfig = config;
            });

    ecs.system("PlanetSurface-LodDebugUI")
            .kind(flecs::PostUpdate)
            .run([this](flecs::iter &) {
                debug_ui();
            });


    // draw a cube of 1x1 a the player feet
    ecs.system<const PlanetUpVector>("PlanetSurface-CubeTemoin")
            .kind(flecs::OnStore)
            .with<PlayerClient>()
            .with<Camera3d>()
            .each([](flecs::entity e, const PlanetUpVector &upVector) {
                // Build orientation from anchor if available, else use PlanetUpVector
                glm::vec3 up = glm::normalize(upVector.up);
                glm::vec3 absUp = glm::abs(up);
                glm::vec3 helper = (absUp.x < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 fwd = glm::normalize(glm::cross(helper, up));
                glm::vec3 right = glm::normalize(glm::cross(up, fwd));

                // Center: 2 units below player (in planet-up direction), 3 units in front
                glm::vec3 center = -up * 2.f + fwd * 3.f;

                // Half-extents along each axis
                glm::vec3 hu = up * 0.5f;
                glm::vec3 hf = fwd * 0.5f;
                glm::vec3 hr = right * 0.5f;

                // 8 corners
                glm::vec3 c[8] = {
                    center - hr - hf - hu,
                    center + hr - hf - hu,
                    center + hr + hf - hu,
                    center - hr + hf - hu,
                    center - hr - hf + hu,
                    center + hr - hf + hu,
                    center + hr + hf + hu,
                    center - hr + hf + hu,
                };

                glm::vec4 col = {1, 0, 1, 1};
                // Bottom face
                DebugDraw::Line(c[0], c[1], col);
                DebugDraw::Line(c[1], c[2], col);
                DebugDraw::Line(c[2], c[3], col);
                DebugDraw::Line(c[3], c[0], col);
                // Top face
                DebugDraw::Line(c[4], c[5], col);
                DebugDraw::Line(c[5], c[6], col);
                DebugDraw::Line(c[6], c[7], col);
                DebugDraw::Line(c[7], c[4], col);
                // Verticals
                DebugDraw::Line(c[0], c[4], col);
                DebugDraw::Line(c[1], c[5], col);
                DebugDraw::Line(c[2], c[6], col);
                DebugDraw::Line(c[3], c[7], col);
            });
}

void PlanetSurfaceTerrainRenderer::init_gpu() {
    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setSamplerOffset(128)
            .setConstantBufferOffset(0)
            .setUnorderedAccessViewOffset(384);

    // Load shaders
    auto vertRes = m_resourceSystem->load<ShaderResource>("planet_surface.vert", ResourceType::SHADER);
    auto fragRes = m_resourceSystem->load<ShaderResource>("simple.frag", ResourceType::SHADER);

    m_vertexShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
        vertRes->get_data(), vertRes->get_data_size());
    m_pixelShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
        fragRes->get_data(), fragRes->get_data_size());

    // UBO buffer
    m_uboBuffer = m_backend->device->createBuffer(
        nvrhi::BufferDesc()
        .setByteSize(sizeof(SurfaceSurfaceUBO))
        .setDebugName("PlanetSurfaceUBO")
        .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
        .setKeepInitialState(true)
        .setIsConstantBuffer(true)
        .setIsVolatile(true)
        .setMaxVersions(8));

    // Set 0: per-frame UBO
    m_frameBindingLayout = m_backend->device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
        .setBindingOffsets(bindingOffsets));
    m_frameBindingSet = m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer)),
        m_frameBindingLayout);

    // Set 1: per-chunk OUB (chunk coords)
    m_oubBindingLayout = m_backend->device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex)
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
        .setBindingOffsets(bindingOffsets));

    // Set 2: face buffer
    m_faceBindingLayout = m_backend->device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex)
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
        .setBindingOffsets(bindingOffsets));

    // Pipeline
    auto framebufferInfo = nvrhi::FramebufferInfo()
            .addColorFormat(m_backend->get_swapchain_format())
            .setDepthFormat(m_backend->get_depth_format());

    nvrhi::RenderState renderState;
    renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    renderState.rasterState.fillMode = nvrhi::RasterFillMode::Fill;
    renderState.depthStencilState.depthTestEnable = true;
    renderState.depthStencilState.depthWriteEnable = true;
    renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;

    m_pipeline = m_backend->device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
        .setVertexShader(m_vertexShader)
        .setPixelShader(m_pixelShader)
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setRenderState(renderState)
        .addBindingLayout(m_frameBindingLayout) // Set 0
        .addBindingLayout(m_oubBindingLayout) // Set 1
        .addBindingLayout(m_faceBindingLayout) // Set 2
        .addBindingLayout(m_textureManager->get_binding_layout()), // Set 3
        framebufferInfo);

    m_meshUploader.init(m_backend);

    // The LOD path needs the geometry arena to exist up front, since the draw emission binds its
    // buffers once and for all.
    create_buffer();
}

void PlanetSurfaceTerrainRenderer::init_lod() {
    m_lodBuffers = std::make_unique<PlanetLodGpuBuffers>(m_backend, LOD_MAX_NODES);

    PlanetLodTree::Config config{};
    config.maxNodes = LOD_MAX_NODES;
    m_lodTree.init(config);

    // Geometry release. The tree only knows a draw slot, so the mesh it belongs to is looked up
    // in the slot table, which is where meshes live now that chunks are not ECS entities.
    m_lodTree.set_release_mesh([this](uint32_t meshId) {
        if (meshId >= m_meshBySlot.size()) return;

        VoxelChunkMesh &mesh = m_meshBySlot[meshId];
        if (!mesh.is_allocated()) return;

        // enqueue_free() used to write a null draw into the slot so a stale entry could not be
        // drawn. The LOD path never reaches a slot no node points at, and the node is zeroed
        // right here, so the write is redundant. Worse, it collides: the slot goes back to the
        // free list immediately, and a reallocation next frame would queue an upload for the
        // same destination as this pending free, two overlapping regions in one copy command.
        //
        // m_meshUploader.enqueue_free(mesh.drawSlotIndex, &m_chunkBuffers[0]);

        m_chunkBuffers[0].free(mesh);

        // Releases happen in update_lod(), after the batcher has been flushed, so no pending
        // upload is still pointing at these faces
        mesh.faces.clear();
        mesh.faces.shrink_to_fit();
        mesh.faceCount = 0;
        mesh.bufferIndex = UINT32_MAX;
    });

    // Work submission. The tree hands out a node index, the generator only ever sees an opaque
    // token: nothing in the worker pools knows the octree exists.
    m_lodTree.set_submit_job([this](uint32_t nodeIndex, const PlanetNodeCoord &coord, uint32_t priority) {
        ChunkGenInput input{};
        input.jobId = m_lodJobs.open(nodeIndex, coord);
        input.coord = coord;
        input.config = m_genConfig;
        input.priority = static_cast<float>(priority);

        m_generator->enqueue(input);
    });

    m_lodTraverser.init(m_backend, m_resourceSystem, m_lodBuffers.get(), LOD_MAX_NODES);

    init_emit_draws_pipeline();
}

void PlanetSurfaceTerrainRenderer::init_emit_draws_pipeline() {
    auto shaderRes = m_resourceSystem->load<ShaderResource>("planet_lod_emit_draws.comp", ResourceType::SHADER);
    m_emitDrawsShader = m_backend->device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute),
        shaderRes->get_data(), shaderRes->get_data_size());

    // Every offset at zero so the slots below match the binding numbers in the GLSL.
    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setUnorderedAccessViewOffset(0)
            .setSamplerOffset(0)
            .setConstantBufferOffset(0);

    VoxelBuffer &buffer = m_chunkBuffers[0];

    // The chunk cull data buffer has no UAV flag and lives in ShaderResource, so it binds as an
    // SRV. Everything else already lives in UnorderedAccess, and binding it as a UAV avoids
    // bouncing those buffers between states every frame.
    m_emitDrawsBindingLayout = m_backend->device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)) // nodes
        .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(1)) // counters
        .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(2)) // render queue
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3)) // chunk cull data
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4)) // culled indirect commands
        .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(5)) // culled draw count
        .setBindingOffsets(bindingOffsets));

    m_emitDrawsBindingSet = m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_lodBuffers->nodes()))
        .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(1, m_lodBuffers->counters()))
        .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(2, m_lodBuffers->render_queue()))
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, buffer.get_chunk_cull_data_buffer()))
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(4, buffer.get_culled_indirect_buffer()))
        .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(5, buffer.get_culled_draw_count_buffer())),
        m_emitDrawsBindingLayout);

    m_emitDrawsPipeline = m_backend->device->createComputePipeline(
        nvrhi::ComputePipelineDesc()
        .setComputeShader(m_emitDrawsShader)
        .addBindingLayout(m_emitDrawsBindingLayout));
}

void PlanetSurfaceTerrainRenderer::destroy() {
    m_backend->device->waitForIdle();
    m_emitDrawsPipeline = nullptr;
    m_emitDrawsBindingSet = nullptr;
    m_emitDrawsBindingLayout = nullptr;
    m_emitDrawsShader = nullptr;
    m_lodBuffers.reset();
    m_chunkBuffers.clear();
    m_oubBindingSets.clear();
    m_faceBindingSets.clear();
    m_meshUploader.destroy();
    m_pipeline = nullptr;
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
}

VoxelBuffer &PlanetSurfaceTerrainRenderer::create_buffer() {
    VoxelBuffer &buf = m_chunkBuffers.emplace_back(m_backend);

    m_oubBindingSets.push_back(m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buf.get_oub_buffer())),
        m_oubBindingLayout));

    m_faceBindingSets.push_back(m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buf.get_faces_buffer())),
        m_faceBindingLayout));

    return buf;
}

VoxelChunkMesh &PlanetSurfaceTerrainRenderer::remember_mesh(VoxelChunkMesh &&mesh) {
    const uint32_t slot = mesh.drawSlotIndex;

    if (slot >= m_meshBySlot.size()) {
        // Growing moves the stored meshes, but a vector move carries its heap buffer along, so
        // the face pointers the upload batcher is holding survive it
        m_meshBySlot.resize(slot + 1);
    }

    m_meshBySlot[slot] = std::move(mesh);
    return m_meshBySlot[slot];
}

uint32_t PlanetSurfaceTerrainRenderer::upload_mesh(VoxelChunkMesh &&mesh, const PlanetNodeCoord &coord) {
    VOXEL_ZONE_N("PlanetLod-UploadMesh");
    VoxelBuffer &buffer = m_chunkBuffers[0];

    if (!buffer.allocate(mesh)) {
        // Only one buffer is allowed for now, so a full arena is a hard failure rather than a
        // reason to grow. Eviction driven by the LOD system is what should keep us under.
        LOG_WARN("PlanetSurfaceTerrainRenderer", "Voxel buffer full, dropping a chunk mesh");
        return NODE_INVALID_MESH;
    }
    mesh.bufferIndex = 0;

    TerrainOUB oub{};
    SurfaceChunkOUB chunkOUB{};
    chunkOUB.coord = {coord.u, coord.v, coord.alt, static_cast<int32_t>(coord.level)};

    static_assert(sizeof(SurfaceChunkOUB) <= sizeof(TerrainOUB));
    std::memcpy(&oub, &chunkOUB, sizeof(SurfaceChunkOUB));

    // The batcher only keeps a pointer into the face vector until it is flushed, so the mesh
    // has to reach its final home before being enqueued
    const VoxelChunkMesh &stored = remember_mesh(std::move(mesh));
    m_meshUploader.enqueue(stored, oub, &buffer);

    return stored.drawSlotIndex;
}

void PlanetSurfaceTerrainRenderer::poll_jobs() {
    VOXEL_ZONE_N("PlanetLod-PollJobs");

    // --- Generated voxels: on to the mesher, or straight back to the tree when uniform ---
    for (ChunkGenOutput &result: m_generator->poll_results()) {
        PlanetLodJobs::Job job;

        if (!result.success || result.empty) {
            if (m_lodJobs.close(result.jobId, job)) {
                m_lodTree.on_job_done({job.nodeIndex, job.coord, true, NODE_INVALID_MESH});
            }
            continue;
        }

        // The token stays open across the meshing hop, so this only fails if the job was
        // already closed, which means the tree has heard about it. Nothing left to account for.
        if (!m_lodJobs.peek(result.jobId, job)) continue;

        std::unordered_map<uint8_t, uint16_t> textureSlots;
        if (m_textureManager) {
            for (const auto &[assetId, localId]: result.chunk.textureIDs) {
                textureSlots[localId] = m_textureManager->request_texture_slot(assetId);
            }
        }

        m_mesher->enqueue(result.jobId, result.chunk.voxels, std::move(textureSlots), 0.0f);
    }

    // --- Meshed faces: into the geometry arena, then published to the tree ---
    for (PlanetChunkMesher::MesherTaskOutput &result: m_mesher->poll_results(MAX_MESH_RESULTS_PER_FRAME)) {
        PlanetLodJobs::Job job;
        if (!m_lodJobs.close(result.jobId, job)) continue;

        if (!result.success || result.faces.empty()) {
            m_lodTree.on_job_done({job.nodeIndex, job.coord, true, NODE_INVALID_MESH});
            continue;
        }

        VoxelChunkMesh mesh{};
        mesh.faces = std::move(result.faces);
        mesh.faceCount = mesh.faces.size();

        const uint32_t meshId = upload_mesh(std::move(mesh), job.coord);

        // A full arena reports the node as uniform. That is a lie, but it is a stable one: the
        // GPU stops asking, and the node comes back the next time its parent is rebuilt.
        m_lodTree.on_job_done({job.nodeIndex, job.coord, meshId == NODE_INVALID_MESH, meshId});
    }
}

void PlanetSurfaceTerrainRenderer::debug_ui() {
    ImGui::Begin("PlanetLod Debug");

    ImGui::Text("Camera world pos: %.1f %.1f %.1f", cameraWorldPos.x, cameraWorldPos.y, cameraWorldPos.z);
    ImGui::SliderFloat("Subdivision threshold (px)", &lodSubdivisionThreshold, 16.0f, 512.0f);

    ImGui::Separator();
    ImGui::Text("Roots: %zu", m_lodTree.root_indices().size());
    ImGui::Text("Node blocks used: %u / %u",
                m_lodTree.store().used_blocks(), m_lodTree.store().capacity() / PlanetLodStore::BLOCK_SIZE);
    ImGui::Text("Jobs in flight: %u (tokens open: %zu)", m_lodTree.jobs_in_flight(), m_lodJobs.open_count());

    if (m_mesher) ImGui::Text("Mesher queue: %zu", m_mesher->queued_task_count());

    ImGui::Separator();
    const VoxelBuffer &buffer = m_chunkBuffers[0];
    ImGui::Text("Draw slots used: %u", buffer.get_unculled_draw_count());
    ImGui::Text("Face regions used: %u / %u", buffer.get_used_face_regions(), MAX_FACES_REGIONS);
    ImGui::Text("Largest free face block: %u", buffer.get_largest_free_face_block());

    ImGui::End();
}

// --- ECS ---

void PlanetSurfaceTerrainRenderer::Register(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    auto* gameState = ecs.get_mut<GameState>();
    if (!renderer || !gameState) {
        LOG_ERROR("PlanetSurfaceTerrainRenderer", "Missing Renderer or GameState");
        return;
    }

    VoxelTextureManager::Register(ecs);
    PlanetChunkMesher::Register(ecs);

    ecs.set<PlanetSurfaceTerrainRenderer>({});
    ecs.get_mut<PlanetSurfaceTerrainRenderer>()->init(ecs);
}

void PlanetSurfaceTerrainRenderer::update_lod(nvrhi::CommandListHandle cmd, const Camera3d &camera) {
    VOXEL_ZONE_N("PlanetLod-Update");

    // 1. What the GPU asked for MAX_FRAMES_IN_FLIGHT frames ago.
    m_lodTree.ingest_requests(m_lodBuffers->read_requests(m_frameIndex));

    // 2. Keep the root disc centred on the player. The face is hardcoded for the flat terrain
    //    phase: a single face is all there is, and crossing a cube edge is a later problem.
    const float rootSize = static_cast<float>(CHUNK_SIZE) * static_cast<float>(1u << PLANET_MAX_LOD);
    const auto rootU = static_cast<int32_t>(std::floor(cameraWorldPos.x / rootSize));
    const auto rootV = static_cast<int32_t>(std::floor(cameraWorldPos.z / rootSize));
    m_lodTree.update_roots(PosX, rootU, rootV);

    // 3. Publish the node changes the tree just made, then run the walk.
    m_lodBuffers->upload_dirty(cmd, m_lodTree.store());
    m_lodTree.store().clear_dirty();

    m_lodBuffers->reset_counters(cmd);
    m_lodBuffers->seed_roots(cmd, m_lodTree.root_indices());

    const auto extent = m_backend->get_swapchain_extent();

    PlanetLodUBO lodUbo{};
    lodUbo.viewProj = camera.projectionMatrix * camera.viewMatrix;
    lodUbo.cameraWorldPos = cameraWorldPos;
    lodUbo.viewportSize = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    lodUbo.subdivisionThreshold = lodSubdivisionThreshold;
    lodUbo.maxRenderDistance = camera.farClip;

    m_lodTraverser.traverse(cmd, lodUbo, static_cast<uint32_t>(m_lodTree.root_indices().size()));

    m_lodBuffers->snapshot_requests(cmd, m_frameIndex);

    // 4. Turn the render queue into compacted draw commands. The dispatch covers the whole queue
    //    capacity, since only the GPU knows how many nodes were actually selected.
    constexpr uint32_t zero = 0;
    cmd->writeBuffer(m_chunkBuffers[0].get_culled_draw_count_buffer(), &zero, sizeof(zero));

    cmd->setComputeState(nvrhi::ComputeState()
        .setPipeline(m_emitDrawsPipeline)
        .addBindingSet(m_emitDrawsBindingSet));
    cmd->dispatch((PlanetLodGpuBuffers::MAX_RENDER + EMIT_DRAWS_GROUP_SIZE - 1) / EMIT_DRAWS_GROUP_SIZE);
}

void PlanetSurfaceTerrainRenderer::render(nvrhi::CommandListHandle commandList,
                                          Camera3d &camera,
                                          VulkanBackend &backend) {
    m_ubo.view = camera.viewMatrix;
    m_ubo.projection = camera.projectionMatrix;
    m_ubo.farPlane = camera.farClip;
    m_ubo.cameraWorldPos = glm::vec4(cameraWorldPos, 0.0f);

    auto* vkCmd = static_cast<VkCommandBuffer>(
        commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));

    // Order matters here. poll_jobs() allocates draw slots and queues their faces, the flush
    // sends them, and only then does update_lod() tell the GPU those nodes have geometry. Any
    // other order publishes a node whose faces are still a frame away.
    poll_jobs();
    m_meshUploader.flush(vkCmd);

    commandList->writeBuffer(m_uboBuffer, &m_ubo, sizeof(SurfaceSurfaceUBO));

    update_lod(commandList, camera);

    auto extent = m_backend->get_swapchain_extent();

    VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "Render Planet Buffer");
    VoxelBuffer &buf = m_chunkBuffers[0];

    auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(extent.width, extent.height)))
            .setFramebuffer(m_backend->get_current_framebuffer())
            .addBindingSet(m_frameBindingSet) // Set 0
            .addBindingSet(m_oubBindingSets[0]) // Set 1
            .addBindingSet(m_faceBindingSets[0]) // Set 2
            .addBindingSet(m_textureManager->get_binding_set()) // Set 3
            .setIndirectParams(buf.get_culled_indirect_buffer())
            .setIndirectCountBuffer(buf.get_culled_draw_count_buffer());
    commandList->setGraphicsState(graphicsState);

    // The visible count lives on the GPU, so the draw reads it straight from the count buffer
    // the emission pass filled. MAX_RENDER is only the upper bound.
    auto* vkCulled = static_cast<VkBuffer>(
        buf.get_culled_indirect_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer));
    auto* vkCount = static_cast<VkBuffer>(
        buf.get_culled_draw_count_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer));
    vkCmdDrawIndirectCount(
        vkCmd,
        vkCulled,
        0,
        vkCount,
        0,
        PlanetLodGpuBuffers::MAX_RENDER,
        sizeof(nvrhi::DrawIndirectArguments));

    ++m_frameIndex;
}
