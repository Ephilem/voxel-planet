#pragma once
#include <array>

#include "IImGuiDebugModule.h"

class FpsCounter : public IImGuiDebugModule {
public:
    void register_ecs(flecs::world &ecs) override;

    const char* get_name() const override { return "FPS Counter"; }
    const char* get_category() const override { return "Performance"; }
    bool is_visible() const override { return m_visible; }
    void set_visible(bool visible) override { m_visible = visible; }

private:
    float m_currentFPS = 0.0f;
    std::array<float, 120> m_fpsHistory = {};
    size_t m_fpsHistoryIndex = 0;
    bool m_visible = true;
};
