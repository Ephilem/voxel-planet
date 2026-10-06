#pragma once

#include <flecs.h>

#include "core/ecs/BaseModule.h"

namespace vp::core {

class PlanetModule : public BaseModule<PlanetModule> {
public:
    PlanetModule(flecs::world& ecs) : BaseModule<PlanetModule>(ecs) { register_all(ecs); }

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<PlanetModule>;
};
} // namespace vp::core
