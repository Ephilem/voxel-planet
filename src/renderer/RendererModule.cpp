#include "RendererModule.h"

#include <nvrhi/utils.h>

#include "debug/DebugRenderModule.h"

#include "Renderer.h"
#include "render_phases.h"
#include "vulkan/VulkanBackend.h"

#include "platform/events.h"
#include "platform/PlatformState.h"

#include "core/TracyIntegration.h"

#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#endif

namespace vp::renderer {
void RendererModule::register_components(flecs::world& ecs) {
    auto* platform = ecs.try_get<platform::PlatformState>();
    if (!platform || !platform->window) {
        throw std::runtime_error("RendererModule: PlatformModule must be initialized before RendererModule");
    }

    ecs.component<phases::RenderExtract>().add(flecs::Phase).depends_on(flecs::PreStore);
    ecs.component<phases::RenderSubmit>().add(flecs::Phase).depends_on<phases::RenderExtract>();
    ecs.component<phases::RenderOverlay>().add(flecs::Phase).depends_on<phases::RenderSubmit>();
    ecs.component<phases::RenderPresent>().add(flecs::Phase).depends_on<phases::RenderOverlay>();

    ecs.component<Renderer>().add(flecs::Singleton);
    ecs.component<RenderView>().add(flecs::Singleton);

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(platform->window->window,
                                                   RenderParameters{platform->window->width, platform->window->height}),
    });
}

void RendererModule::register_systems(flecs::world& ecs) {
    ecs.system<Renderer>("Renderer-BeginFrameSystem").kind(flecs::PreStore).each([](Renderer& renderer) {
        VOXEL_ZONE_N("Renderer-BeginFrame");
        FrameContext& ctx = renderer.frameContext;
        ctx.frameActive = false;

        if (!renderer.backend)
            return;

        if (renderer.backend->begin_frame(ctx.commandList)) {
            ctx.commandList->open();

            nvrhi::utils::ClearColorAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 0,
                                               nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            nvrhi::utils::ClearDepthStencilAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(),
                                                      0.0f, 0);

            nvrhi::TextureHandle currentTexture = renderer.backend->get_current_texture();
            ctx.commandList->setTextureState(currentTexture, nvrhi::TextureSubresourceSet(0, 1, 0, 1),
                                             nvrhi::ResourceStates::RenderTarget);
            ctx.commandList->commitBarriers();

            ctx.frameActive = true;
        }
    });

    ecs.system<Renderer>("EndFrameSystem").kind<phases::RenderPresent>().each([](Renderer& renderer) {
        VOXEL_ZONE_N("Renderer-EndFrame");
        FrameContext& ctx = renderer.frameContext;
        if (!ctx.frameActive || !ctx.commandList)
            return;

#ifdef TRACY_ENABLE
        if (renderer.backend->tracyVkCtx) {
            VkCommandBuffer vkCmd =
                static_cast<VkCommandBuffer>(ctx.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
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

    // Tracy frame boundary, right after present. A separate system so frames without an active
    // render frame (minimized window, swapchain resize) are still marked
    ecs.system("Renderer-FrameMark").kind<phases::RenderPresent>().run([](flecs::iter&) { VOXEL_FRAME_MARK; });

    ecs.observer<platform::PlatformState>().event<platform::WindowResizeEvent>().run([](flecs::iter& it) {
        VOXEL_ZONE_N("PlatformModule-HandleResize");
        auto* evt = it.param<platform::WindowResizeEvent>();
        auto* renderer = it.world().try_get_mut<Renderer>();

        if (renderer && renderer->backend) {
            renderer->backend->handle_resize(evt->width, evt->height);
        }
    });
}

void RendererModule::register_pipelines(flecs::world& ecs) {
}

void RendererModule::register_submodules(flecs::world& ecs) {
    ecs.import<DebugRenderModule>();
}

void RendererModule::register_entities(flecs::world& ecs) {
    ecs.emplace<RenderView>();
}
} // namespace vp::renderer