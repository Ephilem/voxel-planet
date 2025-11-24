#pragma once
#include <array>

#include "IImGuiDebugModule.h"

class FpsCounter : public IImGuiDebugModule {
public:
    void register_ecs(flecs::world &ecs) override;

private:
    float currentFPS = 0.0f;
    std::array<float, 120> fpsHistory = {};
    size_t fpsHistoryIndex = 0;
};
