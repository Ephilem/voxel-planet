#include "RendererModule.h"

#include "Renderer.h"

#include "core/GameState.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "Camera3dSystems.h"
#include "rendering_components.h"
#include "core/TracyIntegration.h"
#include "core/world/world_components.h"
#include "core/world/ChunkManager.h"
#include "debug/ImGuiManager.h"
#include "debug/LogConsole.h"
#include "nvrhi/utils.h"


#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#endif


RendererModule::RendererModule(flecs::world& ecs) {
    auto* platform = ecs.get<PlatformState>();
    if (!platform || !platform->window) {
        throw std::runtime_error("RendererModule: PlatformModule must be initialized before RendererModule");
    }

    ecs.component<Renderer>();

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(platform->window->window, RenderParameters{platform->window->width, platform->window->height}),
        .imguiManager = std::make_unique<ImGuiManager>(),
    });

    ecs.system<Renderer>("BeginFrameSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            VOXEL_ZONE_N("Renderer-BeginFrame");
            FrameContext& ctx = renderer.frameContext;
            ctx.frameActive = false;

            if (!renderer.backend) return;

            if (renderer.backend->begin_frame(ctx.commandList)) {
                ctx.commandList->open();

                nvrhi::utils::ClearColorAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 0, nvrhi::Color(0.1f, 0.1f, 0.4f, 1.0f));
                nvrhi::utils::ClearDepthStencilAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 1.0f, 0);

                ctx.frameActive = true;
            }
        });

    // Initialize chunk meshes for any VoxelChunk that doesn't have a mesh yet,
    // and mark already-loaded neighbors dirty so they re-mesh with the new chunk's border data.
    // Running in OnUpdate guarantees all previous-frame deferred ops are committed, so
    // neighbor.has<VoxelChunkMesh>() is reliable (unlike an OnSet observer which fires during
    // the OnStore merge where the VoxelChunkMesh from InitializeChunkMeshSystem may not be visible).
    ecs.system<const VoxelChunk, const ChunkCoordinate>("InitializeChunkMeshSystem")
        .kind(flecs::OnUpdate)
        .without<VoxelChunkMesh>()
        .each([](flecs::entity e, const VoxelChunk& chunk, const ChunkCoordinate& coord) {
            e.set<VoxelChunkMesh>({})
             .add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();

            // Mark already-uploaded neighbors dirty so they re-mesh with correct border data.
            const auto* chunkManager = e.world().get<ChunkManager>();
            if (!chunkManager) return;

            static constexpr std::array<glm::ivec3, 6> neighborOffsets = {{
                {1, 0, 0}, {-1, 0, 0},
                {0, 1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1}
            }};

            for (const auto& offset : neighborOffsets) {
                glm::ivec3 neighborPos = glm::ivec3(coord) + offset;
                flecs::entity neighbor = chunkManager->get_chunk_entity(neighborPos);
                if (neighbor != flecs::entity::null() && neighbor.has<VoxelChunkMesh>()) {
                    neighbor.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
                }
            }
        });

    VoxelTerrainRenderer::Register(ecs);
    ImGuiManager::Register(ecs);
    ImGuiDebugModuleManager::Register(ecs);

    Camera3dSystems::Register(ecs);

    ecs.system<Renderer>("EndFrameSystem")
        .kind(flecs::PostFrame)
        .each([](flecs::entity e, Renderer& renderer) {
            VOXEL_ZONE_N("Renderer-EndFrame");
            FrameContext& ctx = renderer.frameContext;
            if (!ctx.frameActive || !ctx.commandList) return;

#ifdef TRACY_ENABLE
            if (renderer.backend->tracyVkCtx) {
                VkCommandBuffer vkCmd = static_cast<VkCommandBuffer>(
                    ctx.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)
                );
                TracyVkCollect(renderer.backend->tracyVkCtx, vkCmd);
            }
#endif

            ctx.commandList->close();
            nvrhi::CommandListHandle cmdList = ctx.commandList;
            renderer.backend->device->executeCommandLists(&cmdList, 1);

            renderer.backend->present();

            ctx.frameActive = false;
        });

    ecs.observer<PlatformState>()
        .event<WindowResizeEvent>()
        .run([](flecs::iter& it) {
            auto* evt = it.param<WindowResizeEvent>();
            auto* renderer = it.world().get_mut<Renderer>();

            if (renderer && renderer->backend) {
                renderer->backend->handle_resize(evt->width, evt->height);
            }
        });
}

void shutdown_renderer(flecs::world& ecs) {
    LOG_INFO("RendererModule", "Shutting down...");
    auto* renderer = ecs.get_mut<Renderer>();
    if (renderer) {
        if (renderer->voxelTerrainRenderer) {
            renderer->voxelTerrainRenderer.reset();
        }
        // TODO please find a better way to do this
        if (auto* vtm = ecs.get_mut<VoxelTextureManager>()) {
            vtm->release_resources();
        }
        if (renderer->debugModuleManager) {
            renderer->debugModuleManager.reset();
        }
        if (renderer->imguiManager) {
            renderer->imguiManager.reset();
        }
        renderer->frameContext.commandList = nullptr;
        if (renderer->backend) {
            renderer->backend.reset();
        }
    }
}
