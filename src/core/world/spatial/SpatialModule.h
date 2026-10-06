#pragma once

#include <flecs.h>

#include "core/ecs/BaseModule.h"

namespace vp::core {
class SpatialModule : public BaseModule<SpatialModule> {
public:
    SpatialModule(flecs::world& ecs) : BaseModule(ecs) { register_all(ecs); }

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<SpatialModule>;
};
} // namespace vp::core
