#include "PlanetSurfaceTerrainRenderer.h"

#include "PlanetChunkMesher.h"
#include "client/player/player_components.h"
#include "client/world/planet/planet_client_components.h"
#include "core/GameState.h"
#include "core/main_components.h"
#include "core/TracyIntegration.h"
#include "core/debug/DebugDraw.h"
#include "core/log/Logger.h"
#include "core/world/planet/PlanetChunkManager.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_utils.h"
#include "core/world/spatial/spatial_components.h"
#include "core/world/spatial/spatial_utils.h"
#include "renderer/Renderer.h"
#include "renderer/TracyVulkanIntegration.h"
#include "renderer/world/VoxelTextureManager.h"

using namespace vp;

PlanetSurfaceTerrainRenderer::~PlanetSurfaceTerrainRenderer() {
    if (m_backend)
        destroy();
}

void PlanetSurfaceTerrainRenderer::init(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();

    m_backend = renderer->backend.get();
    m_resourceSystem = gameState->resourceSystem.get();
    m_textureManager = ecs.get_mut<VoxelTextureManager>();
    m_chunkBuffers.reserve(16);

    init_gpu();

    // Systems
    ecs.system<const SurfaceAnchorComp, const GlobalTransform>("PlanetSurface-UpdateUBO")
            .kind(flecs::PreUpdate)
            .with<const PlanetComp>().parent().and_().with<const Grid>().parent()
            .each([this](flecs::entity anchor, const SurfaceAnchorComp a, const GlobalTransform gTrs) {
                flecs::entity planet = anchor.parent();
                const PlanetComp* planetComp = planet.get<PlanetComp>();
                const Grid* grid = planet.get<Grid>();
                glm::dvec3 anchorWorldPos = get_hp_position(anchor);

                const LocalFloatingOrigin &lfo = grid->localOrigin;
                glm::dvec3 foPos = glm::dvec3(lfo.cell) * grid->cellSize + glm::dvec3(lfo.translation);

                m_ubo.planetRadius = planetComp->radius;
                m_ubo.anchorCameraPos = glm::vec4(gTrs.pos, 0);

                m_ubo.anchorFacePos = glm::vec4(
                    float(a.gridCenter.x),
                    float(a.gridCenter.y),
                    float(static_cast<int>(a.face)),
                    float(planetComp->radius));

                set_anchor_information(a);
            });

    ecs.system<VoxelChunkMesh, const SurfaceChunkCoord>("PlanetSurface-UploadMesh")
            .kind(flecs::PreStore)
            .with<const SurfaceAnchorComp>().parent()
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>()
            .each([this](flecs::entity e, VoxelChunkMesh &mesh, const SurfaceChunkCoord &coord) {
                const auto *renderer = e.world().get<Renderer>();
                if (!renderer) return;
                system_upload_chunk_mesh(renderer, mesh, coord);
                e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Clean>();
            });

    ecs.system<Renderer>("PlanetSurface-Render")
            .kind(flecs::OnStore)
            .each([this](flecs::entity e, Renderer &renderer) {
                if (!renderer.frameContext.frameActive) return;
                e.world().each<Camera3d>([&](flecs::entity, Camera3d &camera) {
                    render(renderer.frameContext.commandList, camera, *renderer.backend);
                });
            });

    // ecs.system<const VoxelChunk, const PlanetChunkCoord>("PlanetSurface-InitializeChunkMesh")
    //         .kind(flecs::OnStore)
    //         .without<VoxelChunkMesh>()
    //         .each([this](flecs::entity e, const VoxelChunk &chunk, const PlanetChunkCoord &coord) {
    //             system_initialize_chunk_mesh(e, chunk, coord);
    //         });

    ecs.observer<VoxelChunkMesh>("PlanetSurface-Cleanup")
            .event(flecs::OnRemove)
            .each([this](flecs::entity, VoxelChunkMesh &mesh) {
                if (!mesh.is_allocated() || m_chunkBuffers.empty()) return;
                int idx = mesh.bufferIndex;
                m_meshUploader.enqueue_free(mesh.drawSlotIndex, &m_chunkBuffers[idx]);
                m_chunkBuffers[idx].free(mesh);
            });

    // draw a cube of 1x1 a the player feet
    ecs.system<const PlanetUpVector>("PlanetSurface-CubeTemoin")
            .kind(flecs::OnStore)
            .with<PlayerClient>()
            .with<Camera3d>()
            .each([](flecs::entity e, const PlanetUpVector& upVector) {
                // Build orientation from anchor if available, else use PlanetUpVector
                glm::vec3 up = glm::normalize(upVector.up);
                glm::vec3 absUp = glm::abs(up);
                glm::vec3 helper = (absUp.x < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 fwd   = glm::normalize(glm::cross(helper, up));
                glm::vec3 right = glm::normalize(glm::cross(up, fwd));

                // Center: 2 units below player (in planet-up direction), 3 units in front
                glm::vec3 center = -up * 2.f + fwd * 3.f;

                // Half-extents along each axis
                glm::vec3 hu = up    * 0.5f;
                glm::vec3 hf = fwd   * 0.5f;
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
                DebugDraw::Line(c[0], c[1], col); DebugDraw::Line(c[1], c[2], col);
                DebugDraw::Line(c[2], c[3], col); DebugDraw::Line(c[3], c[0], col);
                // Top face
                DebugDraw::Line(c[4], c[5], col); DebugDraw::Line(c[5], c[6], col);
                DebugDraw::Line(c[6], c[7], col); DebugDraw::Line(c[7], c[4], col);
                // Verticals
                DebugDraw::Line(c[0], c[4], col); DebugDraw::Line(c[1], c[5], col);
                DebugDraw::Line(c[2], c[6], col); DebugDraw::Line(c[3], c[7], col);
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
}

void PlanetSurfaceTerrainRenderer::destroy() {
    m_backend->device->waitForIdle();
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

void PlanetSurfaceTerrainRenderer::system_upload_chunk_mesh(const Renderer *renderer, VoxelChunkMesh &mesh,
                                                            const SurfaceChunkCoord &coord) {
    VOXEL_VK_NVRHI_ZONE(renderer->backend->tracyVkCtx, renderer->frameContext.commandList, "GPU Upload Planet Chunk");

    TerrainOUB oub{};

    SurfaceChunkOUB chunkOUB{};
    chunkOUB.coord = {coord.localU, coord.localV, coord.alt, 0};

    static_assert(sizeof(SurfaceChunkOUB) <= sizeof(TerrainOUB));
    std::memcpy(&oub, &chunkOUB, sizeof(SurfaceChunkOUB));

    if (m_chunkBuffers.empty())
        create_buffer();

    // Remesh: already allocated, just reallocate face region
    if (mesh.is_allocated()) {
        int idx = mesh.bufferIndex;
        if (idx >= 0 && idx < static_cast<int>(m_chunkBuffers.size())) {
            if (m_chunkBuffers[idx].reallocate(mesh)) {
                m_meshUploader.enqueue(mesh, oub, &m_chunkBuffers[idx]);
                return;
            }
            m_chunkBuffers[idx].free(mesh);
        }
    }

    for (size_t i = 0; i < m_chunkBuffers.size(); i++) {
        if (!m_chunkBuffers[i].allocate(mesh)) continue;
        mesh.bufferIndex = static_cast<uint32_t>(i);
        m_meshUploader.enqueue(mesh, oub, &m_chunkBuffers[i]);
        return;
    }

    // No space in any buffer, create a new one and retry
    create_buffer();
    system_upload_chunk_mesh(renderer, mesh, coord);
}

void PlanetSurfaceTerrainRenderer::system_initialize_chunk_mesh(flecs::entity e, const VoxelChunk &mesh,
                                                                const PlanetChunkCoord &coord) {
    VOXEL_ZONE_N("Initialize Chunk in Renderer");
    e.set<VoxelChunkMesh>({}).add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();

    // const auto *cm = e.world().get<PlanetChunkManager>();
    // if (!cm) return;
    //
    // static constexpr std::array<glm::ivec3, 6> neighborOffsets = {
    //     {
    //         {1, 0, 0}, {-1, 0, 0},
    //         {0, 1, 0}, {0, -1, 0},
    //         {0, 0, 1}, {0, 0, -1}
    //     }
    // };
    //
    // for (const auto &offset: neighborOffsets) {
    //     glm::ivec3 neighborPos = glm::ivec3(coord) + offset;
    //     flecs::entity neighbor = cm->(neighborPos);
    //     if (neighbor == flecs::entity::null() || !neighbor.has<VoxelChunkMesh>()) continue;
    //
    //     if (neighbor.has<VoxelChunkMeshState, voxel_chunk_mesh_state::WaitingForNeighbors>()) {
    //         if (cm->can_mesh(neighborPos)) {
    //             neighbor.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
    //         }
    //     } else {
    //         neighbor.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
    //     }
    // }
}

// --- ECS ---

void PlanetSurfaceTerrainRenderer::Register(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();
    if (!renderer || !gameState) {
        LOG_ERROR("PlanetSurfaceTerrainRenderer", "Missing Renderer or GameState");
        return;
    }

    VoxelTextureManager::Register(ecs);
    PlanetChunkMesher::Register(ecs);

    ecs.set<PlanetSurfaceTerrainRenderer>({});
    ecs.get_mut<PlanetSurfaceTerrainRenderer>()->init(ecs);
}

void PlanetSurfaceTerrainRenderer::render(nvrhi::CommandListHandle commandList,
                                          Camera3d &camera,
                                          VulkanBackend &backend) {
    m_ubo.view = camera.viewMatrix;
    m_ubo.projection = camera.projectionMatrix;
    m_ubo.farPlane = camera.farClip;

    auto *vkCmd = static_cast<VkCommandBuffer>(
        commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
    m_meshUploader.flush(vkCmd);

    commandList->writeBuffer(m_uboBuffer, &m_ubo, sizeof(SurfaceSurfaceUBO));

    auto extent = m_backend->get_swapchain_extent();

    for (size_t i = 0; i < m_chunkBuffers.size(); i++) {
        VOXEL_VK_NVRHI_ZONE(backend.tracyVkCtx, commandList, "Render Planet Buffer");
        auto &buf = m_chunkBuffers[i];
        uint32_t drawCount = buf.get_unculled_draw_count();
        if (drawCount == 0) continue;

        auto graphicsState = nvrhi::GraphicsState()
                .setPipeline(m_pipeline)
                .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                    nvrhi::Viewport(extent.width, extent.height)))
                .setFramebuffer(m_backend->get_current_framebuffer())
                .addBindingSet(m_frameBindingSet) // Set 0
                .addBindingSet(m_oubBindingSets[i]) // Set 1
                .addBindingSet(m_faceBindingSets[i]) // Set 2
                .addBindingSet(m_textureManager->get_binding_set()) // Set 3
                .setIndirectParams(buf.get_chunk_cull_data_buffer());
        commandList->setGraphicsState(graphicsState);

        auto *vkCullData = static_cast<VkBuffer>(
            buf.get_chunk_cull_data_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer));
        vkCmdDrawIndirect(
            vkCmd,
            vkCullData,
            offsetof(VoxelChunkCullData, drawArgs),
            buf.get_unculled_draw_count(),
            sizeof(VoxelChunkCullData));
    }
}
