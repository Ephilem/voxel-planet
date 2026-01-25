#include "RendererModule.h"

#include "Renderer.h"

#include "core/GameState.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "Camera3dSystems.h"
#include "rendering_components.h"
#include "core/main_components.h"
#include "core/world/world_components.h"
#include "debug/ImGuiManager.h"
#include "debug/LogConsole.h"
#include "nvrhi/utils.h"
#include "platform/inputs/input_state.h"


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

    // Initialize chunk meshes for any VoxelChunk that doesn't have a mesh yet
    ecs.system<const VoxelChunk>("InitializeChunkMeshSystem")
        .kind(flecs::OnUpdate)
        .without<VoxelChunkMesh>()
        .each([](flecs::entity e, const VoxelChunk& chunk) {
            e.set<VoxelChunkMesh>({})
             .add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
        });

    VoxelTerrainRenderer::Register(ecs);
    ImGuiManager::Register(ecs);
    ImGuiDebugModuleManager::Register(ecs);

    Camera3dSystems::Register(ecs);

    ecs.system<Renderer>("EndFrameSystem")
        .kind(flecs::PostFrame)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            if (!ctx.frameActive || !ctx.commandList) return;

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
