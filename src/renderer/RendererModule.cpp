#include "RendererModule.h"

#include "Renderer.h"
#include "core/Application.h"
#include "vulkan/VulkanBackend.h"
#include <iostream>

RendererModule::RendererModule(flecs::world& ecs) {
    auto* app = ecs.get<Application>();
    if (!app || !app->window) {
        throw std::runtime_error("RendererModule: Application must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(app->window->window)
    });

    // Add render system that gets called each frame
    ecs.system<Renderer>()
        .each([](Renderer& renderer) {
            if (renderer.backend) {
                renderer.backend->render();
            }
        });
}

