#pragma once

#include "client/debug/IDebugPanel.h"

class PlayerPanel : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Player"; }
    const std::string category() const override { return "Entities"; }

private:
    float m_teleportPos[3] = {0.f, 120.f, 0.f};
};
