#pragma once

#include <flecs.h>

struct RendererModule {
    RendererModule(flecs::world& ecs);
};

void shutdown_renderer(flecs::world& ecs);
