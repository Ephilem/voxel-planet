//
// Created by raph on 24/11/25.
//

#include "ImGuiDebugModuleManager.h"

#include "FpsCounter.h"
#include "imgui.h"
#include "LogConsole.h"
#include "VoxelBufferVisualizer.h"
#include "WorldF3Info.h"
#include "renderer/Renderer.h"
#include <map>

#include "platform/inputs/input_state.h"

void ImGuiDebugModuleManager::register_all_ecs(flecs::world &ecs) {
    for (auto& module : m_modules) {
        module->register_ecs(ecs);
    }
}

void ImGuiDebugModuleManager::draw_menu_bar() {
    if (!m_menuBarVisible) return;

    if (ImGui::BeginMainMenuBar()) {
        // Group modules by category
        std::map<std::string, std::vector<IImGuiDebugModule*>> categorizedModules;
        for (auto& module : m_modules) {
            categorizedModules[module->get_category()].push_back(module.get());
        }

        // Draw each category as a menu
        for (auto& [category, modules] : categorizedModules) {
            if (ImGui::BeginMenu(category.c_str())) {
                for (auto* module : modules) {
                    bool visible = module->is_visible();
                    if (ImGui::MenuItem(module->get_name(), nullptr, &visible)) {
                        module->set_visible(visible);
                    }
                }
                ImGui::EndMenu();
            }
        }

        ImGui::EndMainMenuBar();
    }
}

void ImGuiDebugModuleManager::Register(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    renderer->debugModuleManager = std::make_unique<ImGuiDebugModuleManager>();
    renderer->debugModuleManager->register_module<LogConsole>();
    renderer->debugModuleManager->register_module<FpsCounter>();
    renderer->debugModuleManager->register_module<VoxelBufferVisualizer>();
    renderer->debugModuleManager->register_module<WorldF3Info>();

    renderer->debugModuleManager->register_all_ecs(ecs);

    ecs.system<Renderer>("DrawDebugMenuBarSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            if (renderer.debugModuleManager) {
                renderer.debugModuleManager->draw_menu_bar();
            }
        });

    ecs.system<Renderer, InputActionState>("OpenDebugMenuBarSystem")
        .kind(flecs::OnUpdate)
        .singleton()
        .each([](flecs::entity e, Renderer& renderer, InputActionState& debugMenuAction) {
            if (renderer.debugModuleManager && debugMenuAction.is_action_pressed(ActionInputType::DebugMenuBar)) {
                bool currentVisibility = renderer.debugModuleManager->is_menu_bar_visible();
                renderer.debugModuleManager->set_menu_bar_visible(!currentVisibility);
            }
        });
}
