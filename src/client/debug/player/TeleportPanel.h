#pragma once

#include "client/debug/IDebugPanel.h"

class TeleportPanel : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Teleport Player"; }
    const std::string category() const override { return "Player"; }

private:
    float pos[3] = {0.f, 120.f, 0.f};
};
