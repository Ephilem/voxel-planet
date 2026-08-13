#pragma once

#include "utils/common/BaseModule.h"
#include <flecs.h>

namespace vp {

class InputModule : public utils::BaseModule<InputModule> {
public:
    InputModule(flecs::world& ecs) : BaseModule(ecs) {}

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);

    void register_pipelines(flecs::world& ecs) {}

    void register_submodules(flecs::world& ecs) {}

    void register_entities(flecs::world& ecs) {}

    friend class BaseModule<InputModule>;
};

} // namespace vp
