#pragma once
#include <array>
#include "client/debug/IDebugPanel.h"

class FpsCounter : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;
    const std::string name() const override { return "FPS Counter"; }
    const std::string category() const override { return "Performance"; }

private:
    float m_currentFPS = 0.0f;
    std::array<float, 120> m_fpsHistory = {};
    size_t m_fpsHistoryIndex = 0;
};
