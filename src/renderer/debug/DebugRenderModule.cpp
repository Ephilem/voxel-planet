#include "DebugRenderModule.h"

#include <imgui.h>

#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "DebugDrawRenderer.h"
#include "ImGuiManager.h"
#include "platform/PlatformState.h"
#include "renderer/Renderer.h"
#include "renderer/render_phases.h"
#include "renderer/TracyVulkanIntegration.h"

namespace vp::renderer {
void DebugRenderModule::init_renderers(flecs::world& ecs) {
    auto* renderer = ecs.try_get_mut<Renderer>();
    auto* gameState = ecs.try_get_mut<core::GameState>();

    m_imgui = std::make_unique<ImGuiManager>();
    m_imgui->init(ecs.try_get<platform::PlatformState>()->window->window, renderer->backend.get());

    m_debugDraw = std::make_unique<DebugDrawRenderer>(renderer->backend.get(), gameState->resourceSystem.get());
}

void DebugRenderModule::register_systems(flecs::world& ecs) {
    ecs.system("ImGui-BeginFrame").kind(flecs::OnLoad).run([this](flecs::iter&) { m_imgui->begin_frame(); });

    // RenderOverlay: after every 3D draw of RenderSubmit. Debug lines first, then ImGui on top of everything
    ecs.system<const Renderer, RenderView>("DebugDrawRenderer-Render")
       .kind<phases::RenderOverlay>()
       .each([this](const Renderer& renderer, RenderView& view) {
           if (!renderer.frameContext.frameActive)
               return;
           VOXEL_ZONE_N("DebugDrawRenderer-Render");
           m_debugDraw->render(renderer.frameContext.commandList, view);
       });

    ecs.system<Renderer>("ImGui-Render").kind<phases::RenderOverlay>().each([this](Renderer& renderer) {
        VOXEL_ZONE_N("ImGuiManager-Render");
        auto& ctx = renderer.frameContext;
        if (!ctx.frameActive || !ctx.commandList) {
            // need to close the begin frame first
            ImGui::EndFrame();
            return;
        }
        VOXEL_VK_NVRHI_ZONE(renderer.backend->tracyVkCtx, ctx.commandList, "RenderImGui-Render");

        VkCommandBuffer vkCmdBuf = ctx.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

        if (vkCmdBuf) {
            m_imgui->render(vkCmdBuf);
        }
    });
}
} // namespace vp::renderer
