#pragma once

#include <flecs.h>

#include "utils/common/BaseModule.h"

namespace vp {
class SpatialModule : public utils::BaseModule<SpatialModule> {
public:
    SpatialModule(flecs::world& ecs) : BaseModule(ecs) {}

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<SpatialModule>;
};
} // namespace vp