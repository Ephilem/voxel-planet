#pragma once

#include <memory>
#include <vector>

#include "IDebugPanel.h"
#include "core/log/Logger.h"

class DebugUIManager {
public:
    DebugUIManager() = default;
    DebugUIManager(DebugUIManager&&) = default;
    DebugUIManager& operator=(DebugUIManager&&) = default;
    DebugUIManager(const DebugUIManager&) = delete;
    DebugUIManager& operator=(const DebugUIManager&) = delete;

    template<typename T, typename... Args>
    void add_panel(Args&&... args) {
        LOG_TRACE("DebugUIManager", "Adding panel: {}", typeid(T).name());
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
