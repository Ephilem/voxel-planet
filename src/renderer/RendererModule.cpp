#include "RendererModule.h"

#include <iostream>
#include <nvrhi/utils.h>


#include "camera/Camera3dModule.h"
#include "debug/DebugRenderModule.h"
#include "world/planet/PlanetRendererModule.h"

#include "Renderer.h"
#include "vulkan/VulkanBackend.h"

#include "platform/PlatformState.h"
#include "platform/events.h"

#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "core/world/world_components.h"


#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#endif

using namespace vp;

void RendererModule::register_components(flecs::world &ecs) {
    auto* platform = ecs.get<PlatformState>();
    if (!platform || !platform->window) {
        throw std::runtime_error("RendererModule: PlatformModule must be initialized before RendererModule");
    }

    ecs.component<Renderer>();
    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(platform->window->window, RenderParameters{platform->window->width, platform->window->height}),
    });
}

void RendererModule::register_systems(flecs::world &ecs) {
    ecs.system<Renderer>("Renderer-BeginFrameSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            VOXEL_ZONE_N("Renderer-BeginFrame");
            FrameContext& ctx = renderer.frameContext;
            ctx.frameActive = false;

            if (!renderer.backend) return;

            if (renderer.backend->begin_frame(ctx.commandList)) {
                ctx.commandList->open();

                nvrhi::utils::ClearColorAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
                nvrhi::utils::ClearDepthStencilAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 1.0f, 0);

                nvrhi::TextureHandle currentTexture = renderer.backend->get_current_texture();
                ctx.commandList->setTextureState(
                  currentTexture,
                  nvrhi::TextureSubresourceSet(0, 1, 0, 1),
                  nvrhi::ResourceStates::RenderTarget
                );
                ctx.commandList->commitBarriers();

                ctx.frameActive = true;
            }
        });

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

            {
                VOXEL_ZONE_N("Run commands");
                ctx.commandList->close();
                nvrhi::CommandListHandle cmdList = ctx.commandList;
                renderer.backend->device->executeCommandLists(&cmdList, 1);
            }

            renderer.backend->present();

            ctx.frameActive = false;
        });

    ecs.observer<PlatformState>()
        .event<WindowResizeEvent>()
        .run([](flecs::iter& it) {
            VOXEL_ZONE_N("PlatformModule-HandleResize");
            auto* evt = it.param<WindowResizeEvent>();
            auto* renderer = it.world().get_mut<Renderer>();

            if (renderer && renderer->backend) {
                renderer->backend->handle_resize(evt->width, evt->height);
            }
        });
}

void RendererModule::register_pipelines(flecs::world &ecs) {
}

void RendererModule::register_submodules(flecs::world &ecs) {
    ecs.import<Camera3dModule>();
    ecs.import<PlanetRendererModule>();
    ecs.import<DebugRenderModule>();
}

void RendererModule::register_entities(flecs::world &ecs) {
}

// void vp::shutdown_renderer(flecs::world& ecs) {
//     LOG_INFO("RendererModule", "Shutting down...");
//     auto* renderer = ecs.get_mut<Renderer>();
//     if (renderer) {
//         // renderer->renderPasses.clear();
//         // TODO please find a better way to do this
//         if (auto* vtm = ecs.get_mut<VoxelTextureManager>()) {
//             vtm->release_resources();
//         }
//         renderer->frameContext.commandList = nullptr;
//         if (renderer->backend) {
//             renderer->backend.reset();
//         }
//     }
// }
