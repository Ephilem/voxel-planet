#pragma once
#include "IDebugPanel.h"

class WorldF3Info : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;
    const std::string name() const override { return "World F3 Info"; }
    const std::string category() const override { return "Debug"; }
};
