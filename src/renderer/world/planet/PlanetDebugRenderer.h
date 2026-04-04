#pragma once

#include <flecs.h>

#include "core/world/world_components.h"
#include "renderer/rendering_components.h"

class PlanetDebugRenderer {
public:
    static void Register(flecs::world& ecs);
};
