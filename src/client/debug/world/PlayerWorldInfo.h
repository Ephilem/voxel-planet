#pragma once
#include "../IDebugPanel.h"

class PlayerWorldInfo : public IDebugPanel {
public:
    PlayerWorldInfo() { is_visible = true; }

    ImGuiWindowFlags window_flags() const override {
        return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    }

    void pre_render() override;
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "World Info"; }

    const std::string category() const override { return "World"; }
};
