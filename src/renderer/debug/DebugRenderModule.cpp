#include "DebugRenderModule.h"

#include <imgui.h>

#include "DebugDrawRenderer.h"
#include "ImGuiManager.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"
#include "platform/PlatformState.h"
#include "renderer/Renderer.h"
#include "renderer/TracyVulkanIntegration.h"

using namespace vp;

void DebugRenderModule::init_renderers(flecs::world &ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *gameState = ecs.get_mut<GameState>();

    m_imgui = std::make_unique<ImGuiManager>();
    m_imgui->init(ecs.get<PlatformState>()->window->window, renderer->backend.get());

    m_debugDraw = std::make_unique<DebugDrawRenderer>(
        renderer->backend.get(),
        gameState->resourceSystem.get()
    );
}

void DebugRenderModule::register_systems(flecs::world &ecs) {
    ecs.system("ImGui-BeginFrame")
            .kind(flecs::OnLoad)
            .run([this](flecs::iter &) {
                m_imgui->begin_frame();
            });


    ecs.system<const Renderer, Camera3d>("DebugDrawRenderer-Render")
            .term_at(0).singleton()
            .kind(flecs::OnStore)
            .each([this](const Renderer &renderer, Camera3d &camera) {
                if (!renderer.frameContext.frameActive) return;
                VOXEL_ZONE_N("DebugDrawRenderer-Render");
                m_debugDraw->render(renderer.frameContext.commandList, camera, *renderer.backend);
            });

    ecs.system<Renderer>("ImGui-Render")
            .kind(flecs::OnStore)
            .each([this](flecs::entity e, Renderer &renderer) {
                VOXEL_ZONE_N("ImGuiManager-Render");
                auto &ctx = renderer.frameContext;
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
