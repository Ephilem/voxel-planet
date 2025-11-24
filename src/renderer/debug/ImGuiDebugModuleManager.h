#pragma once
#include <vector>
#include <flecs.h>
#include <memory>

#include "IImGuiDebugModule.h"

class ImGuiDebugModuleManager {
    std::vector<std::unique_ptr<IImGuiDebugModule>> m_modules;

public:
    static void Register(flecs::world& ecs);

    void register_all_ecs(flecs::world& ecs);

    void register_module(std::unique_ptr<IImGuiDebugModule> module) {
        m_modules.push_back(std::move(module));
    }

    template<typename T, typename... Args>
    void register_module(Args&&... args) {
        m_modules.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }
};