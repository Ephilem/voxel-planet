#pragma once

#include "DebugDrawRenderer.h"
#include "ImGuiManager.h"

#include "utils/common/BaseModule.h"

namespace vp {
class DebugRenderModule : public utils::BaseModule<DebugRenderModule> {
public:
    DebugRenderModule(flecs::world& ecs) : BaseModule(ecs) { init_renderers(ecs); }

private:
    std::unique_ptr<ImGuiManager> m_imgui;
    std::unique_ptr<DebugDrawRenderer> m_debugDraw;

    void init_renderers(flecs::world& ecs);

    void register_systems(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp
