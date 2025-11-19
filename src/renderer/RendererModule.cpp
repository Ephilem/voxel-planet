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
                    renderer.backend->update_framebuffer_for_current_image();

                    auto commandList = renderer.backend->commandList;
                    commandList->open();

                    renderer.imguiManager->begin_frame();

                    // Set clear values
                    nvrhi::Color clearColor = nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f);
                    commandList->clearTextureFloat(renderer.backend->framebuffer->getDesc().colorAttachments[0].texture, nvrhi::AllSubresources, clearColor);
                    commandList->clearDepthStencilTexture(renderer.backend->depthBuffer, nvrhi::AllSubresources, true, 1.0f, true, 0);

                    // Begin render pass
                    commandList->beginMarker("MainRenderPass");

                    // Set pipeline and draw triangle
                    commandList->setGraphicsPipeline(renderer.backend->graphicsPipeline);
                    commandList->setFramebuffer(renderer.backend->framebuffer);

                    nvrhi::Viewport viewport;
                    viewport.minX = 0.0f;
                    viewport.minY = 0.0f;
                    viewport.maxX = static_cast<float>(renderer.backend->swapchain->get_width());
                    viewport.maxY = static_cast<float>(renderer.backend->swapchain->get_height());
                    viewport.minZ = 0.0f;
                    viewport.maxZ = 1.0f;
                    commandList->setViewports(&viewport, 1);

                    nvrhi::DrawArguments drawArgs;
                    drawArgs.setVertexCount(3);
                    commandList->draw(drawArgs);

                    auto commandBuffer = renderer.backend->frames[renderer.backend->currentFrame].commandBuffer;
                    renderer.imguiManager->end_frame(commandBuffer);

                    commandList->endMarker();
                    commandList->close();

                    // Execute the NVRHI command list on the Vulkan command buffer
                    renderer.backend->nvrhiDevice->executeCommandList(commandList);

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

