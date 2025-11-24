#include "RendererModule.h"

#include "Renderer.h"

#include "core/GameState.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "managers/ImGuiManager.h"
#include "debug/LogConsole.h"
#include "nvrhi/utils.h"


RendererModule::RendererModule(flecs::world& ecs) {
    auto* platform = ecs.get<PlatformState>();
    if (!platform || !platform->window) {
        throw std::runtime_error("RendererModule: PlatformModule must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(platform->window->window, RenderParameters{platform->window->width, platform->window->height}),
        .imguiManager = std::make_unique<ImGuiManager>()
    });

    auto PostStore = ecs.entity("PostStore")
        .add(flecs::Phase)
        .depends_on(flecs::OnStore);

    ImGuiManager::Register(ecs);
    ImGuiDebugModuleManager::Register(ecs);

    ecs.system<Renderer>("BeginFrameSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            ctx.frameActive = false;

            if (!renderer.backend) return;

            if (renderer.backend->begin_frame(ctx.commandList)) {
                ctx.commandList->open();

                nvrhi::utils::ClearColorAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 0, nvrhi::Color(0.1f, 1.0f, 0.1f, 1.0f));

                ctx.frameActive = true;
            }
        });

    ecs.system<Renderer>("EndFrameSystem")
        .kind(PostStore)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            if (!ctx.frameActive || !ctx.commandList) return;

            ctx.commandList->close();
            nvrhi::CommandListHandle cmdList = ctx.commandList;
            renderer.backend->device->executeCommandLists(&cmdList, 1);

            renderer.backend->present();

            ctx.frameActive = false;
            // Cycle to next frame slot
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
    std::cout << "RendererModule: Shutting down..." << std::endl;
    auto* renderer = ecs.get_mut<Renderer>();
    if (renderer) {
        std::cout << "RendererModule: Cleaning up Vulkan backend before window destruction..." << std::endl;
        if (renderer->debugModuleManager) {
            renderer->debugModuleManager.reset();
        }
        if (renderer->imguiManager) {
            renderer->imguiManager.reset();
        }
        if (renderer->backend) {
            renderer->backend.reset();
        }
    }
}
