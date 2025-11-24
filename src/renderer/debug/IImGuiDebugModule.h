#pragma once
#include <flecs.h>

class IImGuiDebugModule {
public:
    virtual ~IImGuiDebugModule() = default;

    // virtual void draw() = 0;

    virtual void register_ecs(flecs::world& ecs) = 0;
};
