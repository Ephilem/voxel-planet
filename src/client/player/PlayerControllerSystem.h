#pragma once

#include <flecs.h>

struct PlayerControllerSystem {
    static void Register(flecs::world& ecs);
};
