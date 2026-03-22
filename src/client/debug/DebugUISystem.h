#pragma once

#include <memory>
#include <vector>

#include "IDebugPanel.h"

class DebugUISystem {
public:
    DebugUISystem() = default;
    DebugUISystem(DebugUISystem&&) = default;
    DebugUISystem& operator=(DebugUISystem&&) = default;
    DebugUISystem(const DebugUISystem&) = delete;
    DebugUISystem& operator=(const DebugUISystem&) = delete;

    template<typename T, typename... Args>
    void add_panel(Args&&... args) {
        m_panels.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    static void Register(flecs::world& ecs);

private:
    void tick(flecs::world& ecs);
    void render_menu_bar();
    void render_panels(flecs::world& ecs);

    std::vector<std::unique_ptr<IDebugPanel>> m_panels;
    bool m_menuBarVisible = true;
};