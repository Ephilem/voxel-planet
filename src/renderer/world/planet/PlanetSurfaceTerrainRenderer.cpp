#include "PlanetSurfaceTerrainRenderer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

#include <glm/gtc/constants.hpp>
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

    /// One colour per LOD level, so the subdivision rings are readable at a glance.
    constexpr glm::vec4 LOD_LEVEL_COLORS[] = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // 0, finest
        {1.0f, 0.6f, 0.1f, 1.0f}, // 1
        {1.0f, 1.0f, 0.2f, 1.0f}, // 2
        {0.3f, 1.0f, 0.3f, 1.0f}, // 3
        {0.2f, 1.0f, 1.0f, 1.0f}, // 4
        {0.3f, 0.5f, 1.0f, 1.0f}, // 5
        {0.7f, 0.3f, 1.0f, 1.0f}, // 6
        {1.0f, 0.3f, 0.8f, 1.0f}, // 7
        {1.0f, 1.0f, 1.0f, 1.0f}, // 8, roots
    };
    constexpr int LOD_LEVEL_COLOR_COUNT = std::size(LOD_LEVEL_COLORS);
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

    // PreStore, so the lines are in the buffer before DebugDrawRenderer consumes it in OnStore,
    // and after PlanetSurface-FeedCameraPosition has placed the camera
    ecs.system("PlanetSurface-LodDebugDraw")
            .kind(flecs::PreStore)
            .run([this](flecs::iter &) {
                debug_draw();
            });

    // Renderer::renderPasses is disabled, so the pass drives itself the way DebugDrawRenderer
    // does. It has to run while the frame's command list is open.
    ecs.system<const Renderer, Camera3d>("PlanetSurface-Render")
            .term_at(0).singleton()
            .kind(flecs::OnStore)
            .each([this](const Renderer &renderer, Camera3d &camera) {
                if (!renderer.frameContext.frameActive || !renderer.frameContext.commandList) return;
                VOXEL_ZONE_N("PlanetSurfaceTerrainRenderer-Render");
                render(renderer.frameContext.commandList, camera, *renderer.backend);
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
    config.rootRadius = lodRootRadius;
    config.rootLevel = LOD_ROOT_LEVEL;
    config.maxJobInFlight = LOD_MAX_JOBS_IN_FLIGHT;
    m_lodTree.init(config);

    // Geometry release. The tree only knows a draw slot, so the mesh it belongs to is looked up
    // in the slot table, which is where meshes live now that chunks are not ECS entities.
    m_lodTree.set_release_mesh([this](uint32_t meshId) { retire_geometry(meshId); });

    // Work submission. The tree hands out a node index, the generator only ever sees an opaque
    // token: nothing in the worker pools knows the octree exists.
    m_lodTree.set_submit_job([this](uint32_t nodeIndex, const PlanetNodeCoord &coord,
                                    uint8_t generation, uint32_t priority) {
        ChunkGenInput input{};
        input.jobId = m_lodJobs.open(nodeIndex, coord, generation);
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

    VoxelBuffer &buffer = *m_chunkBuffer;

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
    m_oubBindingSet = nullptr;
    m_faceBindingSet = nullptr;
    m_chunkBuffer.reset();
    m_meshUploader.destroy();
    m_pipeline = nullptr;
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
}

void PlanetSurfaceTerrainRenderer::create_buffer() {
    m_chunkBuffer = std::make_unique<VoxelBuffer>(m_backend);

    m_oubBindingSet = m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_chunkBuffer->get_oub_buffer())),
        m_oubBindingLayout);

    m_faceBindingSet = m_backend->device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_chunkBuffer->get_faces_buffer())),
        m_faceBindingLayout);
}

VoxelChunkMesh &PlanetSurfaceTerrainRenderer::remember_mesh(VoxelChunkMesh &&mesh, const PlanetNodeCoord &coord) {
    const uint32_t slot = mesh.drawSlotIndex;

    if (slot >= m_meshBySlot.size()) {
        // Growing moves the stored meshes, but a vector move carries its heap buffer along, so
        // the face pointers the upload batcher is holding survive it
        m_meshBySlot.resize(slot + 1);
        m_coordBySlot.resize(slot + 1);
    }

    m_meshBySlot[slot] = std::move(mesh);
    m_coordBySlot[slot] = coord;
    return m_meshBySlot[slot];
}

uint32_t PlanetSurfaceTerrainRenderer::upload_mesh(VoxelChunkMesh &&mesh, const PlanetNodeCoord &coord) {
    VOXEL_ZONE_N("PlanetLod-UploadMesh");
    VoxelBuffer &buffer = *m_chunkBuffer;

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

    // World bounds of the node, the same mapping as lod_node_corner() in planet_lod_common.glsl
    // and the positioning block of planet_surface.vert: u to x, alt to y, v to z, all scaled by
    // the node size at this level. The batcher cannot work this out on its own, since the OUB
    // holds a packed coordinate rather than a model matrix.
    const float nodeSize = static_cast<float>(CHUNK_SIZE) * static_cast<float>(1u << coord.level);
    const glm::vec3 aabbMin =
            glm::vec3(static_cast<float>(coord.u), static_cast<float>(coord.alt), static_cast<float>(coord.v))
            * nodeSize;

    // The batcher only keeps a pointer into the face vector until it is flushed, so the mesh
    // has to reach its final home before being enqueued
    const VoxelChunkMesh &stored = remember_mesh(std::move(mesh), coord);
    m_meshUploader.enqueue(stored, oub, aabbMin, aabbMin + glm::vec3(nodeSize), &buffer);

    return stored.drawSlotIndex;
}

void PlanetSurfaceTerrainRenderer::retire_geometry(uint32_t meshId) {
    if (meshId >= m_meshBySlot.size()) return;

    VoxelChunkMesh &mesh = m_meshBySlot[meshId];
    if (!mesh.is_allocated()) return;

    // Write a null draw into the slot, so that even if something does reach it the draw is a no
    // op rather than a stale vertex range. This was removed once on the grounds that the LOD path
    // never reaches a slot no node points at, which is only true as long as nothing acts on a
    // recycled node: it is the same class of bug as the stale requests the node generation now
    // catches, and the cost of being wrong here is drawing arbitrary geometry.
    //
    // It cannot collide with a reallocation of the slot: the arena only hands it back out after
    // GEOMETRY_RETIRE_SLOTS frames, and the batcher is flushed every frame.
    m_meshUploader.enqueue_free(mesh.drawSlotIndex, m_chunkBuffer.get());

    VoxelChunkMesh retired = std::move(mesh);

    // The slot table entry has to read as empty right now: the tree considers the geometry gone,
    // and the debugger walks this table
    mesh = VoxelChunkMesh{};
    if (meshId < m_coordBySlot.size()) m_coordBySlot[meshId] = {};

    // Retiring happens after the batcher has been flushed, so nothing points at these faces any
    // more and the CPU copy is dead weight. Only the allocation indices have to survive
    retired.faces.clear();
    retired.faces.shrink_to_fit();

    m_retiringMeshes[m_frameIndex % GEOMETRY_RETIRE_SLOTS].push_back(std::move(retired));
}

void PlanetSurfaceTerrainRenderer::reclaim_retired_geometry() {
    // The bucket this frame is about to write into is the one it last used a full rotation ago,
    // so everything in it predates every frame the GPU could still be executing
    if (!m_chunkBuffer) return;

    std::vector<VoxelChunkMesh> &bucket = m_retiringMeshes[m_frameIndex % GEOMETRY_RETIRE_SLOTS];

    for (VoxelChunkMesh &mesh: bucket) {
        m_chunkBuffer->free(mesh);
    }
    bucket.clear();
}

void PlanetSurfaceTerrainRenderer::poll_jobs() {
    VOXEL_ZONE_N("PlanetLod-PollJobs");

    m_mesherRejectsLastFrame = 0;

    // --- Generated voxels: on to the mesher, or straight back to the tree when uniform ---
    //
    // Bounded, and the mesher end is bounded too. Draining this one without a limit while the
    // other has one only moves the backlog into the meshing queue, where it is invisible and
    // holds the job budget saturated for as long as it takes to work off.
    for (ChunkGenOutput &result: m_generator->poll_results(MAX_GEN_RESULTS_PER_FRAME)) {
        PlanetLodJobs::Job job;

        if (!result.success || result.empty) {
            if (m_lodJobs.close(result.jobId, job)) {
                m_lodTree.on_job_done({job.nodeIndex, job.coord, job.generation, true, NODE_INVALID_MESH});
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

        // A refused task has to be accounted for right here. Letting it disappear leaves the node
        // flagged as having a job in flight for good: the tree never hears back, the in flight
        // budget never comes down, and a subdivision waiting on this child can never publish,
        // which is a hole that nothing repairs.
        if (m_mesher->enqueue(result.jobId, result.chunk.voxels, std::move(textureSlots), result.priority)) {
            continue;
        }

        ++m_mesherRejectsLastFrame;
        if (m_lodJobs.close(result.jobId, job)) {
            m_lodTree.on_job_done({job.nodeIndex, job.coord, job.generation, true, NODE_INVALID_MESH});
        }
    }

    // --- Meshed faces: into the geometry arena, then published to the tree ---
    for (PlanetChunkMesher::MesherTaskOutput &result: m_mesher->poll_results(MAX_MESH_RESULTS_PER_FRAME)) {
        PlanetLodJobs::Job job;
        if (!m_lodJobs.close(result.jobId, job)) continue;

        if (!result.success || result.faces.empty()) {
            m_lodTree.on_job_done({job.nodeIndex, job.coord, job.generation, true, NODE_INVALID_MESH});
            continue;
        }

        VoxelChunkMesh mesh{};
        mesh.faces = std::move(result.faces);
        mesh.faceCount = mesh.faces.size();

        const uint32_t meshId = upload_mesh(std::move(mesh), job.coord);

        // A full arena reports the node as uniform. That is a lie, but it is a stable one: the
        // GPU stops asking, and the node comes back the next time its parent is rebuilt.
        m_lodTree.on_job_done({job.nodeIndex, job.coord, job.generation,
                               meshId == NODE_INVALID_MESH, meshId});
    }
}

void PlanetSurfaceTerrainRenderer::debug_ui() {
    ImGui::Begin("PlanetLod Debug");

    ImGui::Text("Camera world pos: %.1f %.1f %.1f", cameraWorldPos.x, cameraWorldPos.y, cameraWorldPos.z);

    // Bring up ladder: freeze on with radius 0 gives exactly one root, one mesh, one draw call
    ImGui::Checkbox("Freeze subdivision (roots only)", &lodFreezeSubdivision);
    ImGui::BeginDisabled(lodFreezeSubdivision);
    ImGui::SliderFloat("Subdivision threshold (px)", &lodSubdivisionThreshold, 16.0f, 512.0f);
    ImGui::EndDisabled();

    // The threshold is the whole quality knob, but it reads as an abstract pixel count. A node is
    // CHUNK_SIZE voxels across, so it also fixes the voxel size on screen, and dividing it out of
    // the projection gives the distance at which each level takes over. Neither has anything to
    // do with how far the terrain goes: that is the render distance and the root disc below
    if (!lodFreezeSubdivision && m_pixelScale > 0.0f) {
        const float splitFactor = m_pixelScale / lodSubdivisionThreshold;
        ImGui::TextDisabled("  %.1f px per voxel | splits within %.1f x node size",
                            lodSubdivisionThreshold / static_cast<float>(CHUNK_SIZE), splitFactor);
        ImGui::TextDisabled("  level 0 (%d m) under %.0f m, level %d (%d m) under %.1f km",
                            CHUNK_SIZE, static_cast<float>(CHUNK_SIZE) * splitFactor,
                            PLANET_MAX_LOD, CHUNK_SIZE << PLANET_MAX_LOD,
                            static_cast<float>(CHUNK_SIZE << PLANET_MAX_LOD) * splitFactor * 0.001f);
    }

    if (ImGui::SliderInt("Root disc radius", &lodRootRadius, 0, 12)) {
        m_lodTree.set_root_radius(lodRootRadius);
    }

    // Roots outside the frustum cost one node each, so widening the disc is close to free. What
    // it buys is the only thing that lets the far clip actually reach: past this the terrain
    // simply is not loaded, and the horizon ends on a straight edge
    {
        const float discHalfExtent =
                static_cast<float>(lodRootRadius) * static_cast<float>(CHUNK_SIZE << LOD_ROOT_LEVEL);
        ImGui::TextDisabled("  terrain loaded out to %.1f km", discHalfExtent * 0.001f);
    }

    // Both are multiples of the same split distance, which is what makes them comparable. The
    // shader keeps children out to the hysteresis, the sweep destroys them past the margin
    ImGui::SliderFloat("Merge hysteresis", &lodMergeHysteresis, 1.0f, 4.0f);
    ImGui::SliderFloat("Backstop margin", &lodCollapseBackstop, 1.0f, 8.0f);
    if (effective_backstop() > lodCollapseBackstop) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "  raised to %.2f: below %.1fx the hysteresis the sweep and the shader fight",
                           effective_backstop(), BACKSTOP_MIN_RATIO);
    }

    ImGui::Separator();
    ImGui::Checkbox("Draw octree", &lodDrawOctree);
    ImGui::BeginDisabled(!lodDrawOctree);
    ImGui::Checkbox("Leaves only", &lodDrawOctreeLeavesOnly);
    ImGui::SliderInt("Min level drawn", &lodDrawOctreeMinLevel, 0, PLANET_MAX_LOD);
    ImGui::EndDisabled();
    ImGui::Checkbox("Draw face bounds", &lodDrawFaceBounds);

    ImGui::Separator();
    ImGui::Text("Roots: %zu", m_lodTree.root_indices().size());

    const uint32_t residentNodes = m_lodTree.store().used_blocks() * PlanetLodStore::BLOCK_SIZE;
    ImGui::Text("Node blocks used: %u / %u (%u nodes)",
                m_lodTree.store().used_blocks(), m_lodTree.store().capacity() / PlanetLodStore::BLOCK_SIZE,
                residentNodes);

    // The number the tuning actually turns on. Rendered is what the threshold buys and barely
    // moves with the view distance; resident is everything kept alive on top of it. A ratio in
    // the tens means the buffer is full of geometry nobody draws, which is a reclamation problem
    // to take to the backstop margin, not a threshold problem
    if (m_lodBuffers) {
        const LodTraversalStats& stats = m_lodBuffers->last_stats();

        ImGui::Text("Rendered nodes: %u", stats.renderedNodes);
        if (stats.renderedNodes > PlanetLodGpuBuffers::MAX_RENDER) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "over queue, geometry dropped");
        }

        if (stats.renderedNodes > 0) {
            const float ratio = static_cast<float>(residentNodes) / static_cast<float>(stats.renderedNodes);
            ImGui::Text("Resident / rendered: %.1fx", ratio);
        }

        ImGui::Text("Requests last frame: %u", stats.requests);
        if (stats.requestOverflow > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "+%u dropped", stats.requestOverflow);
        }
    }

    // Pinned at the cap is a backlog working itself off, not an oscillation: every request over
    // budget is rearmed and comes back, so the request count stays high while it drains
    ImGui::Text("Jobs in flight: %u / %u (tokens open: %zu)",
                m_lodTree.jobs_in_flight(), m_lodTree.max_jobs_in_flight(), m_lodJobs.open_count());

    // The count alone is not a symptom: a subdivision publishes only once all eight children are
    // back, and the pools return them a few at a time, so a busy pipeline legitimately carries
    // many partly satisfied entries. Only an entry that stops making progress is a leak
    ImGui::Text("Pending subdivisions: %zu (%zu child links)",
                m_lodTree.pending_subdivisions(), m_lodTree.pending_children());

    const uint64_t oldestPending = m_lodTree.oldest_pending_age();
    ImGui::Text("Oldest pending: %llu frames", static_cast<unsigned long long>(oldestPending));
    if (oldestPending > STRANDED_PENDING_FRAMES) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "stranded");
    }

    // A subdivision only strands when a job never comes back, so this is never a tuning problem:
    // any non zero value means work is being lost somewhere between the tree and the pools
    if (m_lodTree.stranded_dropped() > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                           "Stranded subdivisions dropped: %u", m_lodTree.stranded_dropped());
    }
    if (m_mesherRejectsLastFrame > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                           "Mesher rejected %u tasks this frame", m_mesherRejectsLastFrame);
    }

    ImGui::Text("Merged on GPU request: %u total", m_lodTree.collapsed_by_gpu());
    if (ImGui::IsItemClicked()) m_lodTree.reset_collapse_stats();
    ImGui::Text("Merged by backstop: %u this frame", m_lodCollapsedLastFrame);

    if (m_mesher) ImGui::Text("Mesher queue: %zu", m_mesher->queued_task_count());

    ImGui::End();

    // Everything about the geometry arena lives in its own panel
    m_bufferDebugger.draw("Voxel Buffer Debug", *m_chunkBuffer, m_meshBySlot, m_coordBySlot);
}

void PlanetSurfaceTerrainRenderer::debug_draw() {
    if (lodDrawFaceBounds) {
        // Extent of one cube face, as arc length on the sphere it will eventually wrap onto.
        // The flat phase keeps that same footprint so the scale stays honest.
        const float halfExtent = m_genConfig.radius * glm::quarter_pi<float>();
        const float y = -cameraWorldPos.y;

        const glm::vec3 corners[4] = {
            {-halfExtent - cameraWorldPos.x, y, -halfExtent - cameraWorldPos.z},
            {halfExtent - cameraWorldPos.x, y, -halfExtent - cameraWorldPos.z},
            {halfExtent - cameraWorldPos.x, y, halfExtent - cameraWorldPos.z},
            {-halfExtent - cameraWorldPos.x, y, halfExtent - cameraWorldPos.z},
        };

        constexpr glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            DebugDraw::Line(corners[i], corners[(i + 1) % 4], white);
        }
    }

    if (!lodDrawOctree) return;

    for (const uint32_t rootIndex: m_lodTree.root_indices()) {
        debug_draw_node(rootIndex);
    }
}

void PlanetSurfaceTerrainRenderer::debug_draw_node(uint32_t nodeIndex) {
    const PlanetLodStore &store = m_lodTree.store();
    const GpuNode &node = store.at(nodeIndex);

    if (node.flags & NODE_EMPTY) return;

    const PlanetNodeCoord coord = node_coord(node);
    const bool hasChildren = node_has_children(node);

    if (static_cast<int>(coord.level) >= lodDrawOctreeMinLevel && (!hasChildren || !lodDrawOctreeLeavesOnly)) {
        // Mirror of lod_node_corner() in planet_lod_common.glsl: node coords map straight onto
        // world axes, u to x, alt to y, v to z, and the debug buffer is camera relative
        const float nodeSize = static_cast<float>(CHUNK_SIZE) * static_cast<float>(1u << coord.level);
        const glm::vec3 boundsMin =
                glm::vec3(static_cast<float>(coord.u), static_cast<float>(coord.alt), static_cast<float>(coord.v))
                * nodeSize - cameraWorldPos;

        const int colorIndex = std::min(static_cast<int>(coord.level), LOD_LEVEL_COLOR_COUNT - 1);
        DebugDraw::Aabb(boundsMin, boundsMin + glm::vec3(nodeSize), LOD_LEVEL_COLORS[colorIndex]);
    }

    if (!hasChildren) return;

    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        if ((node.childMask & (1u << i)) == 0u) continue;
        debug_draw_node(node.childPtr + i);
    }
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

    ecs.emplace<PlanetSurfaceTerrainRenderer>();
    ecs.get_mut<PlanetSurfaceTerrainRenderer>()->init(ecs);
}

void PlanetSurfaceTerrainRenderer::update_lod(nvrhi::CommandListHandle cmd, const Camera3d &camera) {
    VOXEL_ZONE_N("PlanetLod-Update");

    // 1. What the GPU asked for MAX_FRAMES_IN_FLIGHT frames ago.
    m_lodTree.ingest_requests(m_lodBuffers->read_requests(m_frameIndex));

    // 2. Drop the subdivisions that have stopped waiting on anything real. Nothing else can free
    //    them, and each one is a node the traversal will never ask about again.
    m_lodTree.sweep_stranded(STRANDED_PENDING_FRAMES);

    // 3. Keep the root disc centred on the player. The face is hardcoded for the flat terrain
    //    phase: a single face is all there is, and crossing a cube edge is a later problem.
    const float rootSize = static_cast<float>(CHUNK_SIZE) * static_cast<float>(1u << LOD_ROOT_LEVEL);
    const auto rootU = static_cast<int32_t>(std::floor(cameraWorldPos.x / rootSize));
    const auto rootV = static_cast<int32_t>(std::floor(cameraWorldPos.z / rootSize));
    m_lodTree.update_roots(PosX, rootU, rootV);

    const auto extent = m_backend->get_swapchain_extent();
    const float effectiveThreshold = lodFreezeSubdivision ? 1e30f : lodSubdivisionThreshold;

    // 4. Backstop sweep. The merges that matter come from the traversal as LOD_REQ_MERGE and
    //    were already applied by ingest_requests() above; this only reclaims the subtrees the
    //    shader never reaches a verdict on, the ones it culls before deciding anything.
    //
    //    The shader subdivides when nodeSize / distance * pixelScale exceeds the threshold, so
    //    the distance at which it stops wanting children is nodeSize * pixelScale / threshold.
    //    pixelScale is read straight out of the projection: proj[1][1] is 1 / tan(fovY / 2).
    m_pixelScale = 0.5f * static_cast<float>(extent.height) * camera.projectionMatrix[1][1];
    const float keepFactor = m_pixelScale / effectiveThreshold * effective_backstop();
    m_lodCollapsedLastFrame = m_lodTree.collapse_distant(cameraWorldPos, keepFactor);

    // 5. Publish the node changes the tree just made, then run the walk.
    m_lodBuffers->upload_dirty(cmd, m_lodTree.store());
    m_lodTree.store().clear_dirty();

    m_lodBuffers->reset_counters(cmd);
    m_lodBuffers->seed_roots(cmd, m_lodTree.root_indices());

    PlanetLodUBO lodUbo{};
    lodUbo.viewProj = camera.projectionMatrix * camera.viewMatrix;
    lodUbo.cameraWorldPos = cameraWorldPos;
    lodUbo.viewportSize = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    // Freezing puts the threshold out of reach of the 1e9 the traversal reports for a node
    // crossing the near plane, so even the root the camera stands in stops asking to be split
    lodUbo.subdivisionThreshold = effectiveThreshold;
    lodUbo.mergeThreshold = effectiveThreshold / std::max(1.0f, lodMergeHysteresis);
    lodUbo.maxRenderDistance = camera.farClip;

    // Straight from the buffers the traversal writes into, so the shader can never believe in a
    // capacity that does not exist
    lodUbo.maxRenderEntries = PlanetLodGpuBuffers::MAX_RENDER;
    lodUbo.maxRequestEntries = PlanetLodGpuBuffers::MAX_REQUESTS;

    m_lodTraverser.traverse(cmd, lodUbo, static_cast<uint32_t>(m_lodTree.root_indices().size()));

    m_lodBuffers->snapshot_requests(cmd, m_frameIndex);

    // 6. Turn the render queue into compacted draw commands. The dispatch covers the whole queue
    //    capacity, since only the GPU knows how many nodes were actually selected.
    //    planet_lod_emit_draws.comp reads the queue capacity back out of its own thread count, so
    //    the dispatch has to cover it exactly rather than merely reach it.
    static_assert(PlanetLodGpuBuffers::MAX_RENDER % EMIT_DRAWS_GROUP_SIZE == 0,
                  "MAX_RENDER must tile the emit draws workgroup exactly");

    constexpr uint32_t zero = 0;
    cmd->writeBuffer(m_chunkBuffer->get_culled_draw_count_buffer(), &zero, sizeof(zero));

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
    m_ubo.planetRadius = m_genConfig.radius;
    m_ubo.cameraWorldPos = glm::vec4(cameraWorldPos, 0.0f);

    auto* vkCmd = static_cast<VkCommandBuffer>(
        commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));

    // The upload batcher records raw vkCmdCopyBuffer into the command list nvrhi is holding, and
    // a copy inside a render pass is illegal. Another OnStore pass may well have left one open,
    // and nvrhi cannot know it has to close it because it never sees these commands. clearState()
    // is what ends it; the pass sets its own state up from scratch below anyway.
    commandList->clearState();

    // Order matters here. Reclaiming first is what lets a slot released a few frames ago serve
    // an allocation now. poll_jobs() then allocates draw slots and queues their faces, the flush
    // sends them, and only then does update_lod() tell the GPU those nodes have geometry. Any
    // other order publishes a node whose faces are still a frame away.
    reclaim_retired_geometry();
    poll_jobs();
    m_meshUploader.flush(vkCmd);

    commandList->writeBuffer(m_uboBuffer, &m_ubo, sizeof(SurfaceSurfaceUBO));

    update_lod(commandList, camera);

    auto extent = m_backend->get_swapchain_extent();

    VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "Render Planet Buffer");
    VoxelBuffer &buf = *m_chunkBuffer;

    auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(extent.width, extent.height)))
            .setFramebuffer(m_backend->get_current_framebuffer())
            .addBindingSet(m_frameBindingSet) // Set 0
            .addBindingSet(m_oubBindingSet) // Set 1
            .addBindingSet(m_faceBindingSet) // Set 2
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
