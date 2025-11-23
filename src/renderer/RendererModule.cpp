#include "RendererModule.h"

#include "Renderer.h"

#include "core/Application.h"
#include "core/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "managers/ImGuiManager.h"
#include "nvrhi/utils.h"


RendererModule::RendererModule(flecs::world& ecs) {
    auto* app = ecs.get<Application>();
    if (!app || !app->window) {
        throw std::runtime_error("RendererModule: Application must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(app->window->window, RenderParameters{app->window->width, app->window->height}),
        .imguiManager = std::make_unique<ImGuiManager>()
    });

    auto PostStore = ecs.entity("PostStore")
        .add(flecs::Phase)
        .depends_on(flecs::OnStore);

    ImGuiManager::Register(ecs);

    ecs.system<Renderer>("BeginFrameSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            ctx.frameActive = false;
            ctx.commandList = nullptr;

            if (!renderer.backend) return;

            if (renderer.backend->begin_frame()) {
                ctx.commandList = renderer.backend->device->createCommandList();
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
            renderer.backend->device->executeCommandLists(&ctx.commandList, 1);

            renderer.backend->present();

            ctx.frameActive = false;
            ctx.commandList = nullptr;
        });

    ecs.observer<Application>()
        .event<WindowResizeEvent>()
        .run([](flecs::iter& it) {
            auto* evt = it.param<WindowResizeEvent>();
            auto* renderer = it.world().get_mut<Renderer>();

            if (renderer && renderer->backend) {
                renderer->backend->handle_resize(evt->width, evt->height);
            }
        });
}

