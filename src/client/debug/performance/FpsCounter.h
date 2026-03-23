#pragma once
#include <array>
#include "client/debug/IDebugPanel.h"

class FpsCounter : public IDebugPanel {
public:
    FpsCounter() {
        is_visible = true;
    }

    ImGuiWindowFlags window_flags() const override {
        return ImGuiWindowFlags_NoMove
               | ImGuiWindowFlags_NoResize
               | ImGuiWindowFlags_NoTitleBar
               | ImGuiWindowFlags_NoCollapse
               | ImGuiWindowFlags_AlwaysAutoResize
               | ImGuiWindowFlags_NoSavedSettings;
    }

    void pre_render() override;
    void render(flecs::world &ecs) override;

    const std::string name() const override { return "FPS Counter"; }
    const std::string category() const override { return "Performance"; }

private:
    float m_currentFPS = 0.0f;
    std::array<float, 120> m_fpsHistory = {};
    size_t m_fpsHistoryIndex = 0;
};
