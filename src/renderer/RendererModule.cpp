#include "RendererModule.h"

#include "Renderer.h"

#include "core/Application.h"
#include "core/events.h"

#include "vulkan/VulkanBackend.h"
#include "vulkan/VulkanPipeline.h"
#include "vulkan/VulkanRenderPass.h"

#include <iostream>

#include "managers/ImGuiManager.h"


RendererModule::RendererModule(flecs::world& ecs) {
    auto* app = ecs.get<Application>();
    if (!app || !app->window) {
        throw std::runtime_error("RendererModule: Application must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(app->window->window),
        .imguiManager = std::make_unique<ImGuiManager>()
    });

    // Init ImGui
    auto *renderer = ecs.get_mut<Renderer>();
    if (renderer && renderer->backend) {
        renderer->imguiManager->init(app->window->window, renderer->backend.get());
    }

    // Add render system that gets called each frame
    ecs.system<Renderer>()
        .each([](Renderer& renderer) {
            if (renderer.backend) {
                if (renderer.backend->begin_frame()) {
                    auto commandBuffer = renderer.backend->frames[renderer.backend->currentFrame].commandBuffer;
                    renderer.imguiManager->begin_frame();

                    renderer.backend->renderPass->use(commandBuffer);

                    renderer.backend->disp.cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.backend->pipeline->handle);
                    renderer.backend->disp.cmdDraw(commandBuffer, 3, 1, 0, 0);

                    renderer.imguiManager->end_frame(commandBuffer);
                    renderer.backend->renderPass->stopUse(commandBuffer);
                    renderer.backend->end_frame();
                }
            }
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

