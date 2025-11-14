#pragma once
#include <flecs.h>

struct CoreModule {
    CoreModule(flecs::world& ecs);
    ~CoreModule();
};