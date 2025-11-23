#pragma once
#include <flecs.h>

struct PlatformModule {
    PlatformModule(flecs::world& ecs);
};

void shutdown_platform(flecs::world& ecs);
