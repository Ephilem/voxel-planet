//
// Created by raph on 24/11/25.
//

#include "ImGuiDebugModuleManager.h"

#include "FpsCounter.h"
#include "imgui.h"
#include "LogConsole.h"
#include "renderer/Renderer.h"

void ImGuiDebugModuleManager::register_all_ecs(flecs::world &ecs) {
    for (auto& module : m_modules) {
        module->register_ecs(ecs);
    }
}

void ImGuiDebugModuleManager::Register(flecs::world &ecs) {
    auto* renderer = ecs.get_mut<Renderer>();
    renderer->debugModuleManager = std::make_unique<ImGuiDebugModuleManager>();
    renderer->debugModuleManager->register_module<LogConsole>();
    renderer->debugModuleManager->register_module<FpsCounter>();

    renderer->debugModuleManager->register_all_ecs(ecs);
}
