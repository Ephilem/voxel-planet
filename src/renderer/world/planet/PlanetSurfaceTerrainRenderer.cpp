#include "PlanetSurfaceTerrainRenderer.h"

#include "PlanetChunkMesher.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "core/debug/DebugDraw.h"
#include "core/log/Logger.h"
#include "core/world/planet/PlanetChunkManager.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_utils.h"
#include "core/world/spatial/spatial_components.h"
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

    init_gpu();

    // Systems
    ecs.system<const PlanetComp, const Grid>("PlanetSurface-UpdateFO")
            .kind(flecs::PreUpdate)
            .each([this](flecs::entity planet, const PlanetComp &p, const Grid &grid) {
                const LocalFloatingOrigin &lfo = grid.localOrigin;
                glm::dvec3 foPos = glm::dvec3(lfo.cell) * grid.cellSize + glm::dvec3(lfo.translation);
                set_fo_position(glm::vec3(foPos), p.radius);
            });

    ecs.system<VoxelChunkMesh, const PlanetChunkCoord>("PlanetSurface-UploadMesh")
            .kind(flecs::PreStore)
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>()
            .each([this](flecs::entity e, VoxelChunkMesh &mesh, const PlanetChunkCoord &coord) {
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

    ecs.system<const VoxelChunk, const PlanetChunkCoord>("PlanetSurface-InitializeChunkMesh")
            .kind(flecs::OnStore)
            .without<VoxelChunkMesh>()
            .each([this](flecs::entity e, const VoxelChunk &chunk, const PlanetChunkCoord &coord) {
                system_initialize_chunk_mesh(e, chunk, coord);
            });

    ecs.observer<VoxelChunkMesh>("PlanetSurface-Cleanup")
            .event(flecs::OnRemove)
            .with<PlanetChunkCoord>()
            .each([this](flecs::entity, VoxelChunkMesh &mesh) {
                if (!mesh.is_allocated() || m_chunkBuffers.empty()) return;
                int idx = mesh.bufferIndex;
                m_meshUploader.enqueue_free(mesh.drawSlotIndex, &m_chunkBuffers[idx]);
                m_chunkBuffers[idx].free(mesh);
            });

    ecs.system<const PlanetChunkCoord>("PlanetSurface-DebugNormals")
      .kind(flecs::OnStore)
      .with<VoxelChunkMesh>()
      .each([](flecs::entity e, const PlanetChunkCoord& coord) {
          const auto* renderer = e.world().get<Renderer>();
          const auto* planet   = e.parent().get<PlanetComp>();
          if (!renderer || !planet) return;

          // Recalcule l'origine du chunk en planet-relative
          double u = coord.x * CHUNK_SIZE / (double)planet->radius;
          double v = coord.y * CHUNK_SIZE / (double)planet->radius;
          glm::dvec3 dir = glm::normalize(face_to_cube_dir(coord.face, u, v));
          double r = planet->radius + (double)coord.altitude * CHUNK_SIZE;
          glm::vec3 origin = glm::vec3(dir * r);

          glm::vec3 up      = glm::normalize(origin);
          glm::vec3 ref     = (glm::abs(glm::dot(up, glm::vec3(0,1,0))) > 0.99f)
                              ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
          glm::vec3 right   = glm::normalize(glm::cross(ref, up));
          glm::vec3 forward = glm::normalize(glm::cross(up, right));

          const auto* grid = e.parent().get<Grid>();
          glm::vec3 fo = grid ? glm::vec3(
              glm::dvec3(grid->localOrigin.cell) * grid->cellSize
              + glm::dvec3(grid->localOrigin.translation)) : glm::vec3(0);
          glm::vec3 drawOrigin = (origin - fo);
          glm::vec3 centerDrawOrigin = drawOrigin + (up + right + forward) * (0.5f * CHUNK_SIZE);

          DebugDraw::Arrow(centerDrawOrigin, up,      8.0f, {0,1,0,1}); // up    = vert
          DebugDraw::Arrow(centerDrawOrigin, right,   8.0f, {1,0,0,1}); // right = rouge
          DebugDraw::Arrow(centerDrawOrigin, forward, 8.0f, {0,0,1,1}); // fwd   = bleu

          // draw cube that represent the chunk
          DebugDraw::Aabb(
              drawOrigin,
              drawOrigin + (up + right + forward) * glm::vec3(CHUNK_SIZE)
          , {1,1,0,1});
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
        .setByteSize(sizeof(PlanetSurfaceUBO))
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
    renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
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
                                                            const PlanetChunkCoord &coord) {
    VOXEL_VK_NVRHI_ZONE(renderer->backend->tracyVkCtx, renderer->frameContext.commandList, "GPU Upload Planet Chunk");

    TerrainOUB oub{};
    PlanetChunkOUB chunkOUB{
        .coord = glm::ivec4(static_cast<int>(coord.face), coord.x, coord.y, coord.altitude)
    };
    static_assert(sizeof(PlanetChunkOUB) <= sizeof(TerrainOUB));
    std::memcpy(&oub, &chunkOUB, sizeof(PlanetChunkOUB));

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

    commandList->writeBuffer(m_uboBuffer, &m_ubo, sizeof(PlanetSurfaceUBO));

    auto extent = m_backend->get_swapchain_extent();

    for (size_t i = 0; i < m_chunkBuffers.size(); i++) {
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
