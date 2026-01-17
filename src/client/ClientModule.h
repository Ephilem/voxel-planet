#pragma once

#include <flecs.h>

/**
 * Responsible for client-side high-level systems and components
 */
struct ClientModule {
    ClientModule(flecs::world& ecs);
};

void shutdown_client(flecs::world& ecs);