#include "RendererModule.h"

#include "Renderer.h"

#include "core/Application.h"
#include "core/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "managers/ImGuiManager.h"


RendererModule::RendererModule(flecs::world& ecs) {
    auto* app = ecs.get<Application>();
    if (!app || !app->window) {
        throw std::runtime_error("RendererModule: Application must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(app->window->window, RenderParameters{app->window->width, app->window->height}),
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

                    renderer.backend->end_frame_and_present();
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

