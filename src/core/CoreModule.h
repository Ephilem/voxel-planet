#pragma once
#include <flecs.h>

struct CoreModule {
    CoreModule(flecs::world& ecs);
    ~CoreModule();
};

void shutdown_core(flecs::world& ecs);