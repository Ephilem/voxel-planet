#pragma once

#include "DebugDrawRenderer.h"
#include "ImGuiManager.h"

#include "core/ecs/BaseModule.h"

namespace vp::renderer {
class DebugRenderModule : public core::BaseModule<DebugRenderModule> {
public:
    DebugRenderModule(flecs::world& ecs) : BaseModule(ecs) {
        init_renderers(ecs);
        register_all(ecs);
    }

private:
    std::unique_ptr<ImGuiManager> m_imgui;
    std::unique_ptr<DebugDrawRenderer> m_debugDraw;

    void init_renderers(flecs::world& ecs);

    void register_systems(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp::renderer
