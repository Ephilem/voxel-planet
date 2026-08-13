#pragma once

#include <flecs.h>

#include "utils/common/BaseModule.h"

namespace vp {

class PlatformModule : public utils::BaseModule<PlatformModule> {
public:
    PlatformModule(flecs::world& ecs) : BaseModule(ecs) { init(ecs); }

private:
    void init(flecs::world& ecs);

    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<PlatformModule>;
};

} // namespace vp