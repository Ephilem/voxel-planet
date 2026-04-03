#pragma once
#include <flecs.h>

#include "ecs/BaseModule.h"

namespace vp::core {
class CoreModule : public BaseModule<CoreModule> {
public:
    CoreModule(flecs::world& ecs) : BaseModule(ecs) { register_all(ecs); }

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<CoreModule>;
};
} // namespace vp::core
