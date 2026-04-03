#pragma once

#include <flecs.h>

#include "core/ecs/BaseModule.h"

namespace vp::platform {

class PlatformModule : public core::BaseModule<PlatformModule> {
public:
    PlatformModule(flecs::world& ecs) : BaseModule(ecs) {
        register_all(ecs);
        init(ecs);
    }

private:
    void init(flecs::world& ecs);

    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<PlatformModule>;
};

} // namespace vp::platform
