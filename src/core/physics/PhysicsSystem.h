#pragma once

#include <flecs.h>

namespace vp::core {
class PhysicsSystem {
public:
    PhysicsSystem() = default;
    ~PhysicsSystem() = default;

    static void Register(flecs::world& ecs);

private:
    void init(flecs::world& ecs);

    void apply_velocity(flecs::iter& it);
};
} // namespace vp::core
