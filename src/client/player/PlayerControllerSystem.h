#pragma once

#include <flecs.h>

namespace vp::client {
struct PlayerControllerSystem {
    static void Register(flecs::world& ecs);
};
} // namespace vp::client
