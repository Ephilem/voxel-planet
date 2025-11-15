#include "RendererModule.h"

#include "Renderer.h"
#include "core/Application.h"
#include "vulkan/VulkanBackend.h"
#include <iostream>

#include "vulkan/VulkanPipeline.h"
#include "vulkan/VulkanRenderPass.h"

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
                if (renderer.backend->begin_frame()) {
                    auto commandBuffer = renderer.backend->commandBuffers[renderer.backend->currentFrame];
                    renderer.backend->renderPass->use(commandBuffer);

                    renderer.backend->disp.cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.backend->pipeline->handle);
                    renderer.backend->disp.cmdDraw(commandBuffer, 3, 1, 0, 0);

                    renderer.backend->renderPass->stopUse(commandBuffer);
                    renderer.backend->end_frame();
                }
            }
        });
}

