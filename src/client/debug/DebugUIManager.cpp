#include "DebugUIManager.h"

#include "core/TracyIntegration.h"
#include "imgui.h"
#include "platform/inputs/input_state.h"
#include <unordered_map>

#include "renderer/Renderer.h"

void DebugUIManager::tick(flecs::world& ecs) {
    render_menu_bar();
    render_panels(ecs);
}

void DebugUIManager::Register(flecs::world& ecs) {
    ecs.set<DebugUIManager>(DebugUIManager{});
    auto* debugUI = ecs.get_mut<DebugUIManager>();

    ecs.system("DebugUISystem").kind(flecs::PostUpdate).run([debugUI](flecs::iter& it) {
        VOXEL_ZONE_N("DebugUISystem-Tick");
        auto world = it.world();
        debugUI->tick(world);
    });

    ecs.system<Renderer, InputActionState>("OpenDebugMenuBarSystem")
        .kind(flecs::OnUpdate)
        .singleton()
        .each([debugUI](Renderer&, InputActionState& inputState) {
            VOXEL_ZONE_N("DebugUISystem-HandleMenuBarToggle");
            if (inputState.is_action_pressed(ActionInputType::DebugMenuBar)) {
                debugUI->m_menuBarVisible = !debugUI->m_menuBarVisible;
            }
        });
}

void DebugUIManager::render_menu_bar() {
    if (!m_menuBarVisible)
        return;
    if (!ImGui::BeginMainMenuBar())
        return;

    std::unordered_map<std::string, std::vector<IDebugPanel*>> by_category;
    for (auto& p : m_panels)
        by_category[p->category()].push_back(p.get());

    for (auto& [category, panels] : by_category) {
        if (ImGui::BeginMenu(category.c_str())) {
            for (auto* panel : panels)
                ImGui::MenuItem(panel->name().c_str(), nullptr, &panel->is_visible);
            ImGui::EndMenu();
        }
    }

    ImGui::EndMainMenuBar();
}

void DebugUIManager::render_panels(flecs::world& ecs) {
    for (auto& panel : m_panels) {
        if (!panel->is_visible)
            continue;

        panel->pre_render();

        if (ImGui::Begin(panel->name().c_str(), &panel->is_visible, panel->window_flags())) {
            panel->render(ecs);
        }
        ImGui::End();
    }
}